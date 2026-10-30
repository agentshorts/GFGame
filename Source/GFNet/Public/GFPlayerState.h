#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GFNetTypes.h"
#include "GFEconomy.h"
#include "GFTamer.h"
#include "GFPlayerState.generated.h"

class UGFInventoryComponent;
class AGFGameState;
class AGFVoxelWorld;
class AActor;

/**
 * Per-player replicated state (PLAN.md section 4.1). Owns the player's inventory (server-authoritative,
 * owner-only replication) and carries the player's requests to the server: block edits (predicted on the
 * client, validated and applied by the server), held item, and the character upload on join.
 */
UCLASS()
class GFNET_API AGFPlayerState : public APlayerState, public IGFTamer
{
	GENERATED_BODY()

public:
	AGFPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void OnRep_Owner() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Inventory")
	TObjectPtr<UGFInventoryComponent> Inventory;

	/** Stable per-player id (unique net id, or the player name as a fallback). Set by the server at login. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Net")
	FString PlayerKey;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Net")
	bool bIsHost = false;

	/** Name-tag / claim-flag colour slot. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Net")
	int32 ColorIndex = 0;

	/** What is "in hand" (taming reads this). Set through RequestSetHeldItem. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Net")
	FName HeldItem;

	/** True once the server has applied this player's character (upload or cached copy). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Net")
	bool bCharacterReady = false;

	UPROPERTY(Replicated)
	TArray<FGFCompanionEntry> Companions;

	/** Field Journal (PLAN.md M2): written by the server, replicated to its owner only, saved in the character. */
	UPROPERTY(Replicated)
	FGFDiscoveredDinoPreferences Journal;

	/** Share journal discoveries with friends within 30 m (both must have it on). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Taming")
	bool bShareJournal = true;

	/** Mailbox (PLAN.md section 2a): this player's letters, unclaimed first. Written by the server, owner-only. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Land")
	TArray<FGFMailLetter> Mailbox;

	/** Server copy of this player's plot blueprints (travels with the character, not replicated). */
	TArray<FGFPlotBlueprint> Blueprints;

	// ---------------- IGFTamer ----------------
	virtual FString GetTamerKey() const override { return PlayerKey; }
	virtual FName GetTamerHeldItem() const override { return HeldItem; }
	virtual APawn* GetTamerPawn() const override { return GetPawn(); }
	virtual bool IsTamerCrouching() const override;
	virtual bool SharesJournal() const override { return bShareJournal; }
	virtual FGFDiscoveredDinoPreferences* GetMutableJournal() override { return HasAuthority() ? &Journal : nullptr; }
	virtual void JournalChanged() override;

	// ---------------- Client-facing API (works on host and clients) ----------------

	/** Place (BlockId set) or remove (None) a block. Predicted locally on clients. Returns the edit id. */
	UFUNCTION(BlueprintCallable, Category = "GF|Net")
	int32 RequestEdit(FIntVector Cell, FName BlockId);

	UFUNCTION(BlueprintCallable, Category = "GF|Net")
	void RequestSetHeldItem(FName ItemId);

	/**
	 * Give materials to another player (by key). Returns the transaction id; the server applies each id at most
	 * once, so resending it (SendTransfer with the same id) is always safe. Result arrives in OnTransferResult.
	 */
	UFUNCTION(BlueprintCallable, Category = "GF|Economy")
	FGuid RequestTransfer(const FString& ToKey, const TArray<FGFItemStack>& Stacks);
	void SendTransfer(const FGuid& TxId, const FString& ToKey, const TArray<FGFItemStack>& Stacks);

	/** Offer the held food to a dino within reach. The server checks reach and inventory; the result arrives in OfferResults. */
	UFUNCTION(BlueprintCallable, Category = "GF|Taming")
	void RequestOfferFood(AActor* Dino);

	UFUNCTION(BlueprintCallable, Category = "GF|Taming")
	void RequestSetShareJournal(bool bShare);

	/** Client side: offer results in arrival order (tests and UI). */
	TArray<EGFOfferResult> OfferResults;

	/** Server: full offer handling (reach, item, bond, journal, burning the food, roster on tame). */
	EGFOfferResult ServerHandleOffer(AActor* Dino);
	/** Server: adds a tamed dino to this player's roster. Returns the companion id. */
	FGuid ServerAddCompanion(FName Species);

	/** Any landlord / mailbox / blueprint request (claim, pay rent, gift, claim letter, restore, her board tools). Returns the request id. */
	UFUNCTION(BlueprintCallable, Category = "GF|Land")
	int32 RequestLandAction(FGFLandAction Action);

	/** Client side: result per land request id, and toasts ("Rent paid", "You've got mail") in arrival order. */
	TMap<int32, EGFLandResult> LandResults;
	TArray<FString> LandNotices;

	/** Server: a toast for this player. */
	void NotifyLand(const FString& Notice);

	/** Client side: last result per transaction (tests and UI toasts). */
	TMap<FGuid, EGFTxResult> TransferResults;

	/** Sends this machine's character to the server once per session (no-op if not locally controlled). */
	void UploadLocalCharacterIfNeeded();

	bool IsLocallyOwned() const;

	// ---------------- Server ----------------

	/** Full server-side edit: permission/reach/inventory checks, then the voxel edit and item cost/refund. */
	bool ServerHandleEdit(const FIntVector& Cell, FName BlockId, FString* OutReason = nullptr, int32* OutChunkRevision = nullptr);
	bool ValidateEdit(const FIntVector& Cell, FName BlockId, FString& OutReason) const;
	/** Item a block costs to place and gives back when removed. */
	FName ItemForBlock(FName BlockId) const;

	void ServerAcceptCharacter(FGFCharacterData Data);
	FGFCharacterData BuildCharacterData() const;
	void ApplyCharacterData(const FGFCharacterData& Data);
	/** Write the character back to its owner's machine (RPC for guests, direct for the host). */
	void PushCharacterToOwner();
	/** Called on logout: keep the server's copy in the guest records. */
	void SnapshotToGuestRecord();

	int32 GetCharacterRevision() const { return CharacterRevision; }
	/** Highest character revision the owner confirmed storing. */
	int32 GetAckedRevision() const { return AckedRevision; }
	int32 GetEditsAccepted() const { return EditsAccepted; }
	int32 GetEditsRejected() const { return EditsRejected; }

	AGFGameState* GetGFGameState() const;
	AGFVoxelWorld* GetVoxelWorld() const;

	// ---------------- RPCs ----------------

	UFUNCTION(Server, Reliable)
	void ServerRequestEdit(FIntVector Cell, FName BlockId, int32 EditId);

	UFUNCTION(Client, Reliable)
	void ClientEditResult(int32 EditId, bool bAccepted, int32 ChunkRevision);

	UFUNCTION(Server, Reliable)
	void ServerSetHeldItem(FName ItemId);

	UFUNCTION(Server, Reliable)
	void ServerUploadCharacter(const FGFCharacterData& Data);

	UFUNCTION(Client, Reliable)
	void ClientStoreCharacter(const FGFCharacterData& Data);
	UFUNCTION(Server, Reliable)
	void ServerAckCharacter(int32 Revision);
	UFUNCTION(Server, Reliable)
	void ServerLandAction(const FGFLandAction& Action);
	UFUNCTION(Client, Reliable)
	void ClientLandResult(int32 RequestId, EGFLandResult Result);
	UFUNCTION(Client, Reliable)
	void ClientLandNotice(const FString& Notice);
	UFUNCTION(Server, Reliable)
	void ServerOfferFood(AActor* Dino);
	UFUNCTION(Client, Reliable)
	void ClientOfferResult(EGFOfferResult Result);
	UFUNCTION(Server, Reliable)
	void ServerSetShareJournal(bool bShare);

	UFUNCTION(Server, Reliable)
	void ServerTransferTo(FGuid TxId, const FString& ToKey, const TArray<FGFItemStack>& Stacks);
	UFUNCTION(Client, Reliable)
	void ClientTransferResult(FGuid TxId, EGFTxResult Result);

	/** Server: set once this player has logged out and been copied to the guest records. */
	bool bLoggedOut = false;

protected:
	UFUNCTION()
	void HandleInventoryChanged(FName ItemId, int32 NewCount);
	void ScheduleWriteBack();

	int32 NextEditId = 1;
	int32 CharacterRevision = 0;
	int32 AckedRevision = 0;
	int32 NextLandRequestId = 1;
	int32 EditsAccepted = 0;
	int32 EditsRejected = 0;
	bool bUploadedLocal = false;
	bool bWriteBackScheduled = false;
	FTimerHandle WriteBackTimer;
};
