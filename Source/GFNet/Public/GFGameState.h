#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "GFNetTypes.h"
#include "GFEconomy.h"
#include "GFGameState.generated.h"

class AGFPlayerState;
class AGFVoxelWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGFOnLandChanged);
/** Server: ServerClaimFaction succeeded (FactionId None means they left). Social records jail claim order from this. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnFactionChanged, AGFPlayerState* /*Player*/, FName /*FactionId*/);

/** Server only: a pack-up waiting for the guest to confirm they stored the blueprint. */
struct FGFPendingPackUp
{
	FName PlotId;
	FString TenantKey;
	int32 Revision = 0;
	int64 Hash = 0;
};

/** Server only: a guest asked to restore their build onto the same plot for free; waits for the host's OK. */
struct FGFPendingRestore
{
	FName PlotId;
	FString TenantKey;
	FString SourceWorldId;
	FName SourcePlotId;
};

/**
 * Shared, replicated world state (PLAN.md section 4.1). The world itself (blocks, garden, plots, rent) belongs to
 * the host; per-player things live on AGFPlayerState.
 */
UCLASS()
class GFNET_API AGFGameState : public AGameState
{
	GENERATED_BODY()

public:
	AGFGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	/** Stable id of the host's world (saved with it). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Net")
	FString HostWorldId;

	/** Cozy toggle: placing costs nothing and removing gives nothing. Off by default: blocks are items. */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "GF|Net")
	bool bFreeBuild = false;

	/** Server: max distance (cm) from the pawn to an edited cell. 0 = no limit (tests). */
	UPROPERTY(EditAnywhere, Category = "GF|Net")
	float MaxEditReach = 1200.f;

	/** Server only: everyone who has visited this world. */
	UPROPERTY()
	TMap<FString, FGFGuestRecord> GuestRecords;

	AGFPlayerState* FindPlayerByKey(const FString& PlayerKey) const;
	AGFVoxelWorld* GetVoxelWorld() const;

	/** What a brand new character gets, once. */
	static TArray<FGFItemStack> StarterKit();

	// ---------------- Economy (server only, PLAN.md section 2) ----------------

	/** Every material movement between players, letters and wagers (saved with the world). */
	UPROPERTY()
	FGFTransferLedger Ledger;

	UPROPERTY(EditAnywhere, Category = "GF|Data")
	TSoftObjectPtr<UDataTable> ItemTable;

	/** DT_Items (asset, or the CSV fallback). */
	UDataTable* GetItemTable() const;

	/**
	 * Ledger accounts are player keys. An online player's account is their player state's inventory (once their
	 * character is in); an offline guest's is the host's copy in GuestRecords (bumping its revision so it wins on
	 * rejoin). Anything else is an unknown account.
	 */
	IGFLedgerAccounts& Accounts();

	EGFTxResult ServerTransfer(const FGuid& TxId, const FString& From, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason = NAME_None);
	EGFTxResult ServerMint(const FGuid& TxId, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason = NAME_None);
	EGFTxResult ServerBurn(const FGuid& TxId, const FString& From, const TArray<FGFItemStack>& Stacks, FName Reason = NAME_None);
	EGFTxResult ServerDeposit(const FGuid& TxId, const FGuid& EscrowId, FName Kind, const FString& From, const TArray<FGFItemStack>& Stacks, FName Reason = NAME_None);
	EGFTxResult ServerRelease(const FGuid& TxId, const FGuid& EscrowId, const FString& To, FName Reason = NAME_None);
	EGFTxResult ServerRefund(const FGuid& TxId, const FGuid& EscrowId, FName Reason = NAME_None);

	/** Test / debug: ItemId over every account (online players, offline guests) plus open escrows. */
	int64 CountEverywhere(FName ItemId) const;

	// ---------------- Land: plots, rent, mail, blueprints (server-run, PLAN.md section 2a) ----------------
	/** Plots, tenants, rent and statuses. Replicated to everyone (Land Board, Rent Board, plot signs). */
	UPROPERTY(ReplicatedUsing = OnRep_Land, BlueprintReadOnly, Category = "GF|Land")
	FGFLandState Land;

	UPROPERTY(BlueprintAssignable, Category = "GF|Land")
	FGFOnLandChanged OnLandChanged;

	/** Server: guests may only edit inside their own plot. Turned off only by pure voxel / economy tests. */
	UPROPERTY(EditAnywhere, Category = "GF|Land")
	bool bEnforcePlotPermissions = true;

	/** Server only: every letter (saved with the world). Each player sees theirs in AGFPlayerState::Mailbox. */
	UPROPERTY()
	TArray<FGFMailLetter> Mail;

	/** Server only: the host's copy of every packed-up plot (saved with the world). */
	UPROPERTY()
	TArray<FGFPlotBlueprint> PackedBlueprints;

	/** Test / debug: whole days added to the host calendar. */
	int32 DebugDayOffset = 0;
	static constexpr float RentCheckSeconds = 5.f;
	static constexpr float BlueprintBackupSeconds = 300.f;

	/** Host calendar day used for rent. */
	int64 LandToday() const;
	/** Server: default layout (home + 2 rentable plots) if there is no land yet. */
	void EnsureLand();
	void ServerSetLandlord(const FString& Key);
	void MarkLandChanged();

	/** Server: one landlord request from a player. */
	EGFLandResult ServerHandleLandAction(AGFPlayerState* By, const FGFLandAction& Action);
	/** Server: accrue rent on every plot (day change) and auto-pay for online tenants. */
	void ServerUpdateRent();
	/** Server: a player's character is in. Rent owed is collected (capped at 3 periods), mailbox refreshed. */
	void ServerOnPlayerReady(AGFPlayerState* PS);
	EGFLandResult ServerPayRent(FName PlotId, const FString& PayerKey, bool bAuto);
	EGFLandResult ServerClaimLetter(const FString& Key, const FGuid& LetterId);
	/** Puts Stacks (from FromKey's account, may be empty) in a letter to ToKey. Returns the letter id (invalid on failure). */
	FGuid ServerSendLetter(EGFLetterKind Kind, const FString& FromKey, const FString& ToKey, const FString& Text, const TArray<FGFItemStack>& Stacks, FName PlotId, EGFLandResult& OutResult);
	void ServerRefreshMailbox(const FString& Key);
	void ServerRefreshAllMailboxes();
	const FGFMailLetter* FindLetter(const FGuid& LetterId) const;

	/** Autosave backup: every online tenant gets a fresh blueprint of their plot(s) if it changed. */
	void ServerBackupPlots();
	/** Updates PS's blueprints for every plot they rent (no write-back). True if anything changed. */
	bool ServerRefreshBlueprintsFor(AGFPlayerState* PS);
	EGFLandResult ServerPackUpPlot(FName PlotId);
	void ServerCheckPendingPackUps();
	bool HasPendingPackUp(FName PlotId) const;
	bool HasPendingRestore(FName PlotId) const;
	/** The player confirmed a character write-back (pack-ups wait for this). */
	void ServerOnCharacterAcked(AGFPlayerState* PS);

	// ---------------- Mini-game modes (server) ----------------
	EGFTxResult ServerForfeit(const FGuid& TxId, const FGuid& EscrowId, FName Reason = NAME_None);

	/** A mode director (IGFModeHandler) registers itself on the server; its saved blob is restored right away. */
	void RegisterModeHandler(UObject* Handler);
	void UnregisterModeHandler(UObject* Handler);
	IGFModeHandler* FindModeHandler(FName Mode) const;
	/** Routes a player's request: "Player" settings here, everything else to the mode's handler. */
	FGFModeResult ServerHandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request);
	void NotifyModesPlayerReady(AGFPlayerState* PS);
	void NotifyModesPlayerLeft(AGFPlayerState* PS);
	/** World save: ModeSaveBlobs refreshed from every live handler. */
	TMap<FName, FGFModeBlob> GatherModeStates() const;
	/** World load: replaces ModeSaveBlobs and pushes them into live handlers. */
	void RestoreModeStates(const TMap<FName, FGFModeBlob>& Blobs);

	/** Server only: persistent mode state by mode name (saved with the world). */
	TMap<FName, FGFModeBlob> ModeSaveBlobs;

	/** Recipe rows (DT_Recipes CSV): the pinned recipe protects its inputs from wagers. */
	static const struct FGFRecipeRow* FindRecipe(FName RecipeId);

	// ---------------- Factions (opaque ids; server) ----------------
	/** Leader per faction: the first player to claim it this session. Replicated to everyone. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Faction")
	TArray<FGFFactionLeader> FactionLeaders;
	/** Seats per faction (default 4; 0 = no cap). A world holds up to 10 players (GameSession MaxPlayers in
	 *  DefaultGame.ini) over 3 factions, so the fullest split is 4/3/3. */
	UPROPERTY(EditAnywhere, Category = "GF|Faction")
	int32 MaxPerFaction = 4;
	/** Server: PS joins FactionId (None = leaves). "Ok", "Full", "Same". The first claimer becomes its leader. */
	FName ServerClaimFaction(AGFPlayerState* PS, FName FactionId);
	FString FactionLeaderOf(FName FactionId) const;
	bool IsFactionLeader(const AGFPlayerState* PS) const;
	int32 FactionCount(FName FactionId) const;
	/** Server, session only: faction per player key, so a rejoin (new player state) keeps its faction. */
	TMap<FString, FName> SessionFactions;
	/** Server only. Fired after a successful claim, including leaving (None). */
	FGFOnFactionChanged OnFactionChanged;

	// ---------------- Chat ----------------
	/** All chat, newest last (last MaxChat). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Chat")
	TArray<FGFChatMessage> ChatLog;
	static constexpr int32 MaxChat = 64;
	static constexpr int32 MaxChatChars = 200;
	/** Server: one chat line from By. Every mode handler's ChatBlockReason is asked first. Returns "Ok" or why not. */
	FName ServerHandleChat(AGFPlayerState* By, const FString& Text);

	/** Server: any mode handler refusing this player key (bans). */
	bool IsJoinRefused(const FString& PlayerKey, FString& OutWhy) const;

	bool IsKnownPlayer(const FString& Key) const;
	FString DisplayNameOf(const FString& Key) const;

protected:
	UFUNCTION()
	void OnRep_Land();
	void SetTenant(FGFLandPlot& Plot, const FString& Key);
	void FinishPackUp(FName PlotId);
	EGFLandResult ApplyBlueprint(const FGFLandPlot& Plot, const FGFPlotBlueprint& Blueprint);
	void PruneMail();

	TArray<FGFPendingPackUp> PendingPackUps;
	TArray<FGFPendingRestore> PendingRestores;
	FTimerHandle RentTimer;
	FTimerHandle BackupTimer;

private:
	mutable TWeakObjectPtr<AGFVoxelWorld> CachedVoxel;
	TUniquePtr<IGFLedgerAccounts> AccountsImpl;
	TMap<FName, TWeakObjectPtr<UObject>> ModeHandlers;
	int32 NextChatSeq = 1;
};
