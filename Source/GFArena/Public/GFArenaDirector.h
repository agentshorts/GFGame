#pragma once

// Server side of the Dino Arena (PLAN.md M6): placement, wagers in escrow, the fight, Slow Time, commands,
// possession and payouts. Players talk to it through AGFPlayerState::RequestMode (Mode "Arena").
//
// Verbs (FGFModeRequest.Verb):
//   Place      Ids = companion ids, Ints = spots 0..5 (up to 3)       AutoPlace   (best 3, tanks in front)
//   Wager      Stacks = her stake -> Stacks = counter-bet, Ints = [band, claws]
//   Suggest    -> Stacks = a ~10 % stake        Reroll (one free per fight)        NoWager
//   Fight      (stakes go into escrow, the opponent is built from the band)
//   SlowTime   (team-shared charge; pressing again ends it)   Command  Ints = [unit, ability 0..2, target unit]
//   Possess    Ints = [unit]   Cycle   Release  Input  Floats = [x, y], Ints = [attack 0/1, ability or -1]
//   HostWagersOff Ints = [0/1] (host only)
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFArenaTypes.h"
#include "GFModeTypes.h"
#include "GFArenaDirector.generated.h"

class AGFGameState;

UENUM(BlueprintType)
enum class EGFArenaPhase : uint8
{
	Idle,
	Fighting,
	Finished
};

/** What clients see of a dino in the fight. */
USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaUnitView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Arena") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") int32 Team = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") FString OwnerKey;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") FName Species;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") int32 Level = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") float HP = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") float MaxHP = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") FVector2D Pos = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") bool bFainted = false;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") FString PossessedBy;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") int32 StatusMask = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") TArray<float> Cooldowns;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") int32 AbilityCount = 0;
};

/** One player's pre-fight setup and, after the fight, their payout. */
USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Arena") FString PlayerKey;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") TArray<FGuid> CompanionIds;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") TArray<int32> Spots;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") TArray<FGFItemStack> Stake;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") TArray<FGFItemStack> Counter;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") bool bRerollUsed = false;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") FGuid EscrowId;
	/** Escrow still open (stake held). */
	UPROPERTY(BlueprintReadOnly, Category = "Arena") bool bEscrowOpen = false;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") int32 RerollSeed = 0;
	// After the fight:
	UPROPERTY(BlueprintReadOnly, Category = "Arena") int32 Stars = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") TArray<FGFItemStack> Rewards;
	/** What the stake did: "Won", "Lost", "Returned" or None (no wager). */
	UPROPERTY(BlueprintReadOnly, Category = "Arena") FName WagerResult;
	/** The stake and counter-bet as they were at the fight (for receipts). */
	UPROPERTY(BlueprintReadOnly, Category = "Arena") TArray<FGFItemStack> LastStake;
	UPROPERTY(BlueprintReadOnly, Category = "Arena") TArray<FGFItemStack> LastCounter;
};

UCLASS()
class GFARENA_API AGFArenaDirector : public AActor, public IGFModeHandler
{
	GENERATED_BODY()

public:
	AGFArenaDirector();

	static const FName ModeName;

	/** The director in this world (spawned on the server when missing and bSpawn). */
	static AGFArenaDirector* Get(UWorld* World, bool bSpawn = true);

	// IGFModeHandler
	virtual FName GetModeName() const override { return ModeName; }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual void OnModePlayerLeft(AGFPlayerState* PS) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- replicated state ----
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") EGFArenaPhase Phase = EGFArenaPhase::Idle;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") TArray<FGFArenaUnitView> Units;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") TArray<FGFArenaEntry> Entries;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") EGFArenaOutcome Outcome = EGFArenaOutcome::None;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") float FightTime = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") int32 Combos = 0;
	/** Slow Time, real seconds (the UI rings run on these, not on world time). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") bool bSlowTimeActive = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") float SlowTimeRemaining = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") float SlowTimeCooldown = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") int32 OpponentBand = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Arena") bool bHostWagersOff = false;

	// ---- server settings ----
	/** Fight speed multiplier (tests run fights fast). */
	float SimSpeed = 1.f;
	/**
	 * Slow Time also slows the whole world (SetGlobalTimeDilation). Only for the Colosseum map: never in the
	 * shared hub, where it would slow everyone (PLAN.md M6). Off by default.
	 */
	bool bApplyWorldDilation = false;
	/** Tests: a fixed match seed (0 = random). */
	int32 ForcedSeed = 0;

	/** Server: the sim (tests read it). */
	const FGFArenaSim& GetSim() const { return Sim; }
	/** Tests only: tweak the running fight (cooldowns, HP). */
	FGFArenaSim& EditSimForTests() { return Sim; }
	const FGFArenaEntry* FindEntry(const FString& Key) const;
	/** Server: ends the fight now as a draw (map exit, host quits): stakes go home. */
	void AbortFight();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	FGFArenaSim Sim;
	FGFSlowTime Slow;
	TArray<FGFArenaCommand> Queued;
	double LastRealTime = 0.0;
	int32 MatchCounter = 0;

	AGFGameState* GS() const;
	FGFArenaEntry& EntryFor(const FString& Key);
	FGFModeResult DoPlace(AGFPlayerState* By, const TArray<FGuid>& Ids, const TArray<int32>& Spots);
	FGFModeResult DoWager(AGFPlayerState* By, const TArray<FGFItemStack>& Stake, bool bReroll);
	FGFModeResult DoFight(AGFPlayerState* By);
	void FinishFight();
	void SettleWager(FGFArenaEntry& E, bool bWon, bool bLost);
	void EndSlowTime(double Now);
	void SetWorldDilation(float D);
	void RefreshViews();
	int32 BandOfEntries() const;
};
