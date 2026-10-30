#pragma once

// Dino Arena (PLAN.md M6): data rows, the pure auto-battle sim, Slow Time and possession rules.
// No world: the server's AGFArenaDirector runs this, and the tests drive it directly.
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Math/RandomStream.h"
#include "GFRewards.h"
#include "GFArenaTypes.generated.h"

UENUM(BlueprintType)
enum class EGFArenaRole : uint8
{
	Skirmisher,
	Assassin,
	Tank,
	Bruiser
};

UENUM(BlueprintType)
enum class EGFAbilityTarget : uint8
{
	Self,
	Enemy,
	EnemyArea,
	AllyArea
};

/** Statuses (index into FGFArenaUnit::Status). */
UENUM(BlueprintType)
enum class EGFArenaStatus : uint8
{
	None,
	Wet,
	Stun,
	Root,
	Burn,
	Shred,
	Taunt,
	Guard,
	Haste,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class EGFArenaAI : uint8
{
	Easy,
	Normal,
	Champion
};

UENUM(BlueprintType)
enum class EGFArenaOutcome : uint8
{
	None,
	Team0Won,
	Team1Won,
	Draw
};

/** DT_ArenaSpecies: base fight stats per species (level 1). Row name = species. */
USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaSpeciesRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena") EGFArenaRole Role = EGFArenaRole::Bruiser;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena") float HP = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena") float Attack = 20.f;
	/** Seconds between basic attacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena") float AttackInterval = 1.f;
	/** cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena") float Range = 180.f;
	/** cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena") float Speed = 350.f;
};

/** DT_DinoAbilities: 3 per species (PLAN.md M6 table). Row name = ability id. */
USTRUCT(BlueprintType)
struct GFARENA_API FGFDinoAbilityRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") FName Species;
	/** 1..3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") int32 Slot = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") float Cooldown = 8.f;
	/** cm: how close the target must be (Enemy), or the area radius (EnemyArea / AllyArea). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") float Range = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") EGFAbilityTarget Target = EGFAbilityTarget::Enemy;
	/** Damage = Attack x DamageMult (0 = no damage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") float DamageMult = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") EGFArenaStatus Status = EGFArenaStatus::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") float StatusSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") EGFArenaStatus SelfStatus = EGFArenaStatus::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") float SelfStatusSeconds = 0.f;
	/** Fraction of max HP healed (self, or every ally in the area). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") float HealFraction = 0.f;
	/** Combo: x BonusMult damage when the target has this status. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") EGFArenaStatus BonusVsStatus = EGFArenaStatus::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") EGFArenaStatus BonusVsStatus2 = EGFArenaStatus::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") float BonusMult = 1.f;
	/** Leaps next to the target first (Pounce, Charge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") bool bDash = false;
	/** Dino level needed (ability 3 unlocks at 3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ability") int32 UnlockLevel = 1;
};

/** DT_ArenaRewards: the free reward roll every match gets, by stars (row Stars0..Stars3). Materials only. */
USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaRewardRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward") int32 Stars = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward") TArray<FGFRewardEntry> Rolls;
};

/** DT_WagerBands: stake band -> opponent difficulty (PLAN.md M6 table). Row name = band name. */
USTRUCT(BlueprintType)
struct GFARENA_API FGFWagerBandRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 1 Small .. 4 Huge (FGFEconomy::ValueBand). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wager") int32 Band = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wager") int32 LevelDelta = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wager") EGFArenaAI AI = EGFArenaAI::Normal;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wager") int32 Claws = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wager") int32 RivalSpells = 0;
};

/** Every arena table, loaded once (CSV fallback, FGFDataTables). */
struct GFARENA_API FGFArenaData
{
	TMap<FName, FGFArenaSpeciesRow> Species;
	/** Per species, slots 1..3 in order. */
	TMap<FName, TArray<FGFDinoAbilityRow>> Abilities;
	TMap<FName, FName> AbilityIds; // "Species.Slot" -> row name (UI / logs)
	TArray<FGFArenaRewardRow> Rewards;
	TArray<FGFWagerBandRow> Bands;

	static const FGFArenaData& Get();
	/** Abilities a dino of this level can use (2 until level 3). */
	int32 AbilityCountFor(FName InSpecies, int32 Level) const;
	const FGFDinoAbilityRow* Ability(FName InSpecies, int32 SlotIndex) const;
	const FGFArenaRewardRow* RewardFor(int32 Stars) const;
	/** Band row for a value band 1..4; no wager = the Medium row (a friendly fight). */
	const FGFWagerBandRow* BandRow(int32 Band) const;
	TArray<FName> SpeciesIds() const;
};

USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaUnit
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	/** 0 = her side (players), 1 = opponents. */
	UPROPERTY() int32 Team = 0;
	UPROPERTY() FString OwnerKey;
	UPROPERTY() FGuid CompanionId;
	UPROPERTY() FName Species;
	UPROPERTY() EGFArenaRole Role = EGFArenaRole::Bruiser;
	UPROPERTY() int32 Level = 1;
	/** 0..2 front row, 3..5 back row. */
	UPROPERTY() int32 Spot = 0;
	UPROPERTY() float MaxHP = 1.f;
	UPROPERTY() float HP = 1.f;
	UPROPERTY() float Attack = 1.f;
	UPROPERTY() float AttackInterval = 1.f;
	UPROPERTY() float Range = 100.f;
	UPROPERTY() float Speed = 300.f;
	UPROPERTY() FVector2D Pos = FVector2D::ZeroVector;
	UPROPERTY() float AttackTimer = 0.f;
	UPROPERTY() TArray<float> Cooldowns;
	UPROPERTY() int32 AbilityCount = 2;
	UPROPERTY() TArray<float> Status;
	UPROPERTY() int32 TauntBy = INDEX_NONE;
	UPROPERTY() int32 LastAttacker = INDEX_NONE;
	UPROPERTY() bool bFainted = false;
	/** Fed a favourite before the fight: +15 % damage, first ability ready. */
	UPROPERTY() bool bWellFed = false;
	/** Player who is controlling it directly (empty = AI). */
	UPROPERTY() FString PossessedBy;
	UPROPERTY() FVector2D MoveInput = FVector2D::ZeroVector;
	UPROPERTY() bool bWantAttack = false;
	UPROPERTY() int32 WantAbility = INDEX_NONE;
	/** Slow Time command waiting to run as its first action after resume. */
	UPROPERTY() int32 CommandSlot = INDEX_NONE;
	UPROPERTY() int32 CommandTarget = INDEX_NONE;
	UPROPERTY() float RetreatTime = 0.f;
	UPROPERTY() float DecisionTimer = 0.f;
	/** Seconds in a row it could act but did nothing (stuck detection); MaxIdle is the worst stretch. */
	UPROPERTY() float IdleTime = 0.f;
	UPROPERTY() float MaxIdle = 0.f;
	UPROPERTY() int32 AbilitiesUsed = 0;

	bool Has(EGFArenaStatus S) const { return Status.IsValidIndex(static_cast<int32>(S)) && Status[static_cast<int32>(S)] > 0.f; }
	bool IsAlive() const { return !bFainted; }
	float HPFraction() const { return MaxHP > 0.f ? HP / MaxHP : 0.f; }
	int32 StatusMask() const;
};

/** A Slow Time order: one per dino, run in issue order when time resumes. */
USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaCommand
{
	GENERATED_BODY()

	UPROPERTY() FString IssuerKey;
	UPROPERTY() int32 UnitId = 0;
	/** 0-based ability index. */
	UPROPERTY() int32 Slot = 0;
	UPROPERTY() int32 TargetId = INDEX_NONE;
};

/** Slow Time (PLAN.md M6): one charge, 0.2x for up to 6 real seconds, 30 real-second cooldown from when it ends. */
USTRUCT(BlueprintType)
struct GFARENA_API FGFSlowTime
{
	GENERATED_BODY()

	static constexpr float Dilation = 0.2f;
	static constexpr double WindowSeconds = 6.0;
	static constexpr double CooldownSeconds = 30.0;

	UPROPERTY() bool bActive = false;
	UPROPERTY() double EndsAt = 0.0;
	UPROPERTY() double ReadyAt = 0.0;

	bool IsReady(double NowReal) const { return !bActive && NowReal >= ReadyAt; }
	bool TryActivate(double NowReal);
	/** True when the window just ran out (cooldown starts now). */
	bool Update(double NowReal);
	/** Second press, all commands issued, fight end, a leaving player: back to 1.0 now. */
	bool End(double NowReal);
	float CurrentDilation() const { return bActive ? Dilation : 1.f; }
	double Remaining(double NowReal) const { return bActive ? FMath::Max(0.0, EndsAt - NowReal) : 0.0; }
	double CooldownLeft(double NowReal) const { return bActive ? CooldownSeconds : FMath::Max(0.0, ReadyAt - NowReal); }
};

/** Who goes into a fight. */
struct GFARENA_API FGFArenaEntrant
{
	FString OwnerKey;
	FGuid CompanionId;
	FName Species;
	int32 Level = 1;
	int32 Spot = 0;
	bool bWellFed = false;
};

/**
 * Deterministic auto-battle: same seed and inputs -> same fight. 2D arena (cm), team 0 on the left.
 * AI: SelectTarget (by role, taunts first) -> Approach -> basic attack / ability by utility -> Retreat (fragile
 * roles under 25 %) -> Fainted. Commanded and Possessed override the AI (PLAN.md M6).
 */
USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaSim
{
	GENERATED_BODY()

	static constexpr float MaxSeconds = 180.f;
	static constexpr float StepSeconds = 0.05f;
	static constexpr float HalfWidth = 1200.f;
	static constexpr float HalfHeight = 700.f;
	static constexpr float BurnFractionPerSec = 0.03f;

	UPROPERTY() TArray<FGFArenaUnit> Units;
	UPROPERTY() float Time = 0.f;
	UPROPERTY() int32 Seed = 0;
	UPROPERTY() EGFArenaOutcome Outcome = EGFArenaOutcome::None;
	UPROPERTY() int32 Combos = 0;
	EGFArenaAI TeamAI[2] = { EGFArenaAI::Normal, EGFArenaAI::Normal };

	FRandomStream Rng;
	/** Accumulated time not yet simulated (fixed steps). */
	float Pending = 0.f;

	void Init(int32 InSeed, EGFArenaAI Team0AI, EGFArenaAI Team1AI);
	/** Adds a dino with its species stats scaled by level (+10 % per level; Champion opponents +15 %). Returns its id. */
	int32 AddUnit(const FGFArenaData& Data, int32 Team, const FGFArenaEntrant& Who);
	static FVector2D SpotPosition(int32 Team, int32 Spot);

	/** Advances by Dt sim seconds in fixed steps. */
	void Advance(const FGFArenaData& Data, float Dt);
	void Step(const FGFArenaData& Data, float Dt);
	bool IsOver() const { return Outcome != EGFArenaOutcome::None; }
	/** Runs to the end (tests): returns the outcome. */
	EGFArenaOutcome RunToEnd(const FGFArenaData& Data);

	FGFArenaUnit* Find(int32 Id);
	const FGFArenaUnit* Find(int32 Id) const;
	int32 AliveCount(int32 Team) const;
	/** 1-3 for a win: the win, everyone still standing, under 90 s. 0 otherwise. */
	int32 StarsFor(int32 Team) const;

	// ---- Slow Time commands ----
	/** "Ok" or why not: NotOwner, Fainted, Locked, Cooldown, BadTarget. */
	FName CheckCommand(const FGFArenaData& Data, const FGFArenaCommand& Cmd) const;
	/** Runs the orders in issue order (each as that dino's first action). */
	void ApplyCommands(const FGFArenaData& Data, const TArray<FGFArenaCommand>& Cmds);

	// ---- Direct control ----
	/** Take over one of your own living dinos ("Ok", NotOwner, Fainted, Taken). */
	FName Possess(const FString& Key, int32 UnitId);
	/** Next living dino of yours after the current one; INDEX_NONE = back on the balcony. */
	int32 Cycle(const FString& Key);
	void Release(const FString& Key);
	int32 PossessedBy(const FString& Key) const;
	FName SetInput(const FString& Key, const FVector2D& Move, bool bAttack, int32 AbilitySlot);

private:
	void TickUnit(const FGFArenaData& Data, FGFArenaUnit& U, float Dt);
	int32 SelectTarget(const FGFArenaUnit& U) const;
	int32 NearestEnemy(const FGFArenaUnit& U, float MaxDist = TNumericLimits<float>::Max()) const;
	int32 CountEnemiesWithin(const FGFArenaUnit& U, float Radius) const;
	bool TryAbilities(const FGFArenaData& Data, FGFArenaUnit& U, int32 TargetId);
	bool UseAbility(const FGFArenaData& Data, FGFArenaUnit& U, int32 SlotIndex, int32 TargetId);
	bool CanUseOn(const FGFDinoAbilityRow& Row, const FGFArenaUnit& U, const FGFArenaUnit* T) const;
	void BasicAttack(FGFArenaUnit& U, FGFArenaUnit& T);
	void Damage(FGFArenaUnit& From, FGFArenaUnit& To, float Amount);
	void SetStatus(FGFArenaUnit& U, EGFArenaStatus S, float Seconds);
	bool MoveToward(FGFArenaUnit& U, const FVector2D& Goal, float StopAt, float Dt);
	void OnFainted(FGFArenaUnit& U);
	void CheckEnd();
};

/** Combat math, pure (PLAN.md GFGame.Combat.Math golden tests). */
struct GFARENA_API FGFArenaMath
{
	/** Damage dealt by Attack x Mult to a target with the given statuses. */
	static float Damage(float Attack, float Mult, bool bWellFed, bool bTargetShred, bool bTargetGuard, float ComboMult = 1.f);
	static float LevelScale(int32 Level) { return 1.f + 0.1f * (FMath::Max(1, Level) - 1); }
};
