#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFRunwayShow.h"
#include "GFRunwayStage.generated.h"

class AGFRunwayPlayerLink;
class APlayerController;

/** Answer to a show request, sent back to the asking client. */
UENUM(BlueprintType)
enum class EGFRunwayRequestResult : uint8
{
	Ok,
	/** Only the server can do that (clients go through AGFRunwayPlayerLink). */
	NotAuthority,
	UnknownTheme,
	NoPlayers,
	/** A show is already running (or none is, for outfit and ready). */
	WrongPhase,
	NotInShow,
	/** The outfit failed FGFOutfit::Validate (unknown piece, wrong slot, undyeable piece, bad colour). */
	InvalidOutfit,
	/** Not enough dye items for the dyed pieces; nothing was charged and the outfit was not changed. */
	NotEnoughDye,
	/** The player's ledger account can't be read yet (still joining). */
	NoAccount,
	/** Only the host can start a show. */
	NotHost
};

UENUM(BlueprintType)
enum class EGFRunwayRequest : uint8
{
	Start,
	Outfit,
	Ready
};

/** What every machine sees of the show (the server's FGFRunwayShow, replicated). */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFRunwayStageState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") EGFRunwayPhase Phase = EGFRunwayPhase::Idle;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") FName ThemeId;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") FGuid ShowId;
	/** Whole seconds left in this phase (rounded up, so it replicates once a second, not every frame). */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") int32 PhaseSecondsLeft = 0;
	/** Contestant on the runway while Walking, else INDEX_NONE. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") int32 WalkerIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") TArray<FGFRunwayContestant> Contestants;
};

/** A show reward the ledger hasn't taken yet (player still joining, say). Retried with the same TxId. */
USTRUCT()
struct GFFASHION_API FGFRunwayPendingReward
{
	GENERATED_BODY()

	UPROPERTY() FGuid TxId;
	UPROPERTY() FString PlayerKey;
	UPROPERTY() TArray<FGFItemStack> Stacks;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnRunwayPhase, EGFRunwayPhase, Phase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGFOnRunwayStateChanged);

/**
 * The fashion show in a level. Server-authoritative: only the server runs FGFRunwayShow, charges dyes and mints
 * rewards; the state replicates to everyone. Clients act through their own AGFRunwayPlayerLink (spawned here for
 * every player controller), whose server RPCs land in the Handle* functions with the player key taken from the
 * connection's player state, never from the client.
 *
 * Economy: dyeing a slot burns the dye items (FGFDyes) from the player's ledger account with TxId
 * show + player + slot, so the same slot is charged at most once per show. Rewards are minted with TxId
 * show + player; if the ledger refuses (no account yet) they stay pending here and are retried with the same TxId.
 * Nothing is ever paid outside the ledger.
 */
UCLASS()
class GFFASHION_API AGFRunwayStage : public AActor
{
	GENERATED_BODY()

public:
	AGFRunwayStage();

	/** The first stage in World (or null). */
	static AGFRunwayStage* Find(const UWorld* World);

	/**
	 * Spawns the greybox runway on this stage: theme spots, the wardrobe rack, and the
	 * start, finish and hub arches. The site faces with this stage, so the start arch
	 * sits down the catwalk from the dressing end. The hub director calls this when the
	 * world is L_Runway. Safe to call again.
	 */
	void EnsurePlayableSite();

	/** Server: starts a show for everyone whose character is ready. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "GF|Fashion")
	EGFRunwayRequestResult StartShow(FName ThemeId);
	/** Server: starts a show for given player keys (lobbies, tests). */
	EGFRunwayRequestResult StartShowFor(FName ThemeId, const TArray<FString>& PlayerKeys, int32 Seed);
	/** Server: a player's outfit. Validates it and charges dyes for newly dyed slots (all or nothing). */
	EGFRunwayRequestResult HandleSetOutfit(const FString& PlayerKey, const FGFOutfit& Outfit);
	EGFRunwayRequestResult HandleSetReady(const FString& PlayerKey, bool bReady);

	/** Server: advances the show, pays out on results, retries pending rewards (Tick calls this). */
	void Step(float DeltaSeconds);
	/** Server: retries rewards the ledger refused. Returns how many were paid now. */
	int32 RetryPendingRewards();
	const TArray<FGFRunwayPendingReward>& GetPendingRewards() const { return PendingRewards; }

	/** Tests: use this ledger instead of the game state's (both must outlive the stage). */
	void SetLedgerOverride(FGFTransferLedger* InLedger, IGFLedgerAccounts* InAccounts) { LedgerOverride = InLedger; AccountsOverride = InAccounts; }

	/** Replicated view, valid on every machine. */
	const FGFRunwayStageState& GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "GF|Fashion")
	EGFRunwayPhase GetPhase() const { return State.Phase; }
	const FGFRunwayContestant* FindContestant(const FString& PlayerKey) const;
	/** Server only: the show itself. */
	const FGFRunwayShow& GetShow() const { return Show; }

	/** Phase lengths for the next show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Fashion") float DressingSeconds = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Fashion") float WalkSeconds = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Fashion") float JudgingSeconds = 4.f;

	/** Fires on every machine when the phase changes. */
	UPROPERTY(BlueprintAssignable, Category = "GF|Fashion")
	FGFOnRunwayPhase OnPhaseChanged;
	/** Fires on every machine when anything in the state changes. */
	UPROPERTY(BlueprintAssignable, Category = "GF|Fashion")
	FGFOnRunwayStateChanged OnStateChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

protected:
	UFUNCTION()
	void OnRep_State(const FGFRunwayStageState& Old);

	UPROPERTY(ReplicatedUsing = OnRep_State)
	FGFRunwayStageState State;

private:
	void SyncState();
	void PayOut();
	void EnsureLinks();
	bool LedgerReady() const;
	bool ReadAccount(const FString& Key, FGFInventory& Out) const;
	bool WasApplied(const FGuid& TxId) const;
	EGFTxResult Mint(const FGuid& TxId, const FString& To, const TArray<FGFItemStack>& Stacks);
	EGFTxResult Burn(const FGuid& TxId, const FString& From, const TArray<FGFItemStack>& Stacks);

	FGFRunwayShow Show;
	bool bPaid = true;
	float RetryTimer = 0.f;
	float LinkTimer = 0.f;
	TMap<FString, TArray<FGFOutfit>> Looks;
	TArray<FGFRunwayPendingReward> PendingRewards;
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<AGFRunwayPlayerLink>> Links;
	FGFTransferLedger* LedgerOverride = nullptr;
	IGFLedgerAccounts* AccountsOverride = nullptr;
};

/** A request answer as the client saw it. */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFRunwayRequestAnswer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") EGFRunwayRequest Request = EGFRunwayRequest::Start;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") EGFRunwayRequestResult Result = EGFRunwayRequestResult::Ok;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnRunwayAnswer, const FGFRunwayRequestAnswer&, Answer);

/**
 * One per player controller, owned by it and relevant only to its owner: the client's channel to the stage.
 * The player key used on the server always comes from the owning controller's player state.
 */
UCLASS(NotPlaceable)
class GFFASHION_API AGFRunwayPlayerLink : public AActor
{
	GENERATED_BODY()

public:
	AGFRunwayPlayerLink();

	/** This machine's own link (the one owned by a local player controller), or null until it has replicated. */
	static AGFRunwayPlayerLink* FindLocal(const UWorld* World);

	/** Host only: a guest's request is answered NotHost. */
	UFUNCTION(BlueprintCallable, Category = "GF|Fashion") void RequestStart(FName ThemeId) { ServerRequestStart(ThemeId); }
	UFUNCTION(BlueprintCallable, Category = "GF|Fashion") void RequestOutfit(const FGFOutfit& Outfit) { ServerSetOutfit(Outfit); }
	UFUNCTION(BlueprintCallable, Category = "GF|Fashion") void RequestReady(bool bReady) { ServerSetReady(bReady); }

	/** Server: the owner's player key (empty if unknown). */
	FString GetPlayerKey() const;
	/** True if this link belongs to the host (the only player who may start a show). */
	bool IsHost() const;

	/** Answers received so far (owning client). */
	const TArray<FGFRunwayRequestAnswer>& GetAnswers() const { return Answers; }

	UPROPERTY(BlueprintAssignable, Category = "GF|Fashion")
	FGFOnRunwayAnswer OnAnswer;

protected:
	UFUNCTION(Server, Reliable) void ServerRequestStart(FName ThemeId);
	UFUNCTION(Server, Reliable) void ServerSetOutfit(const FGFOutfit& Outfit);
	UFUNCTION(Server, Reliable) void ServerSetReady(bool bReady);
	UFUNCTION(Client, Reliable) void ClientAnswer(EGFRunwayRequest Request, EGFRunwayRequestResult Result);

private:
	AGFRunwayStage* GetStage() const;
	void Answer(EGFRunwayRequest Request, EGFRunwayRequestResult Result);

	TArray<FGFRunwayRequestAnswer> Answers;
};
