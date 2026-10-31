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

/**
 * DT_ArenaSpells. One old damage or heal point is 8% of the target's max HP (FGFArenaMath::PointFraction).
 * Alt columns are the second fire of the same spell (SparkBolt's shred, BloomHeal's bigger sip).
 * Row name = spell id. Gear, the wipe shot, and terrain-as-a-spell stay out of this table.
 */
USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaSpellRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") int32 Cost = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") int32 AltCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") int32 DamagePoints = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") int32 AltDamagePoints = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") float HealFraction = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") float AltHealFraction = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") EGFArenaStatus Status = EGFArenaStatus::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") float StatusSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") EGFArenaStatus AltStatus = EGFArenaStatus::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") float AltStatusSeconds = 0.f;
	/** 0 = one target. Above 0 = radius in cm around that target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") float Radius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") float Duration = 0.f;
	/** EmberSpice and BloomHeal touch allies. RallyWhistle touches every ally. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") bool bAllies = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") bool bPush = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") bool bLaneBlock = false;
	/** Slow Time windows before this spell can be cast again. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") int32 WindowCooldown = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spell") float AttackMul = 1.f;
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
	TMap<FName, FGFArenaSpellRow> Spells;
	/** Row order from DT_ArenaSpells. */
	TArray<FName> SpellOrder;

	static const FGFArenaData& Get();
	/** Abilities a dino of this level can use (2 until level 3). */
	int32 AbilityCountFor(FName InSpecies, int32 Level) const;
	const FGFDinoAbilityRow* Ability(FName InSpecies, int32 SlotIndex) const;
	const FGFArenaRewardRow* RewardFor(int32 Stars) const;
	/** Band row for a value band 1..4; no wager = the Medium row (a friendly fight). */
	const FGFWagerBandRow* BandRow(int32 Band) const;
	TArray<FName> SpeciesIds() const;
	const FGFArenaSpellRow* Spell(FName Id) const;
};

USTRUCT(BlueprintType)
struct GFARENA_API FGFArenaUnit
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	/** 0 = the players' side, 1 = opponents. */
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
	/** Fed a favourite before the fight: +15 % damage, first ability ready. Once per match. */
	UPROPERTY() bool bWellFed = false;
	/** Rented dinos quit instead of fainting. The sim still counts them as down. */
	UPROPERTY() bool bRental = false;
	UPROPERTY() FName Family;
	/** Wary or hostile rentals will not take a rider. */
	UPROPERTY() bool bControlBlocked = false;
	/** EmberSpice. 1 = no buff. AttackMulUntil is sim time. */
	UPROPERTY() float AttackMul = 1.f;
	UPROPERTY() float AttackMulUntil = 0.f;
	/** Set for a moment after a dash so a hit can root the dasher. */
	UPROPERTY() float DashUntil = 0.f;
	/** 1 = high ground, -1 = mire, 0 = flat. Weights live on the scorer; v1 leaves this at 0. */
	UPROPERTY() float Terrain = 0.f;
	/** Brain intent, held between the 0.25 s decisions. Slot -1 = basic attack. 8+ = spell index. */
	UPROPERTY() int32 IntentTarget = INDEX_NONE;
	UPROPERTY() int32 IntentSlot = -1;
	UPROPERTY() bool bIntentRetreat = false;
	UPROPERTY() bool bIntentSpent = false;
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
	bool bRental = false;
	FName Family;
	bool bControlBlocked = false;
};

/** A rental that hit 0 HP. It counts as down for the bout and posts a quit. */
struct GFARENA_API FGFArenaQuit
{
	FString OwnerKey;
	FName Family;
	FName Species;
	int32 UnitId = 0;
};

/** A lane plugged by StoneWall. LaneY is the lane centre (-350, 0 or 350). Until is sim time. */
struct GFARENA_API FGFArenaWall
{
	float LaneY = 0.f;
	float Until = 0.f;
};

/** One brain choice, logged so a replay can take the same choices. */
struct GFARENA_API FGFArenaPlan
{
	float Time = 0.f;
	int32 UnitId = 0;
	int32 Slot = -1;
	int32 TargetId = INDEX_NONE;
	bool bRetreat = false;
};

/**
 * Deterministic auto-battle: same seed and inputs -> same fight. 2D arena (cm), team 0 on the left.
 * The brain is pure C++ in this struct: every 0.25 s it scores a 1 s lookahead and picks one action.
 * Commanded and possessed dinos skip the brain. A StateTree, if one is added later, only plays the visuals.
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
	static constexpr float HotSandSeconds = 20.f;
	static constexpr float ControlLockSeconds = 2.f;
	static constexpr float DecisionSeconds = 0.25f;
	static constexpr float LookaheadSeconds = 1.f;
	static constexpr int32 SparkStart = 3;
	static constexpr int32 SparkCap = 8;
	static constexpr float SparkEverySeconds = 5.f;

	UPROPERTY() TArray<FGFArenaUnit> Units;
	UPROPERTY() float Time = 0.f;
	UPROPERTY() int32 Seed = 0;
	UPROPERTY() EGFArenaOutcome Outcome = EGFArenaOutcome::None;
	UPROPERTY() int32 Combos = 0;
	EGFArenaAI TeamAI[2] = { EGFArenaAI::Normal, EGFArenaAI::Normal };

	FRandomStream Rng;
	/** Accumulated time not yet simulated (fixed steps). */
	float Pending = 0.f;

	/** Shared by a side. Starts at SparkStart, +1 every SparkEverySeconds, cap SparkCap. */
	int32 Spark[2] = { SparkStart, SparkStart };
	float SparkBank[2] = { 0.f, 0.f };
	/** Sim time of the last HP loss. Hot sand starts when this sits still for HotSandSeconds. */
	float LastDamageTime = 0.f;
	/** Player key -> sim time when the next control switch is allowed. */
	TMap<FString, float> ControlReadyAt;
	TArray<FGFArenaWall> Walls;
	TArray<FGFArenaQuit> Quits;
	TArray<FName> Loadout0;
	TArray<FName> Loadout1;
	TMap<FName, int32> SpellLock0;
	TMap<FName, int32> SpellLock1;
	/** Player spells only land while this is set. Champion AI ignores it. */
	bool bSpellsOpen = false;
	/**
	 * Optional logs. A lookahead copy clears these so a fork cannot write the parent's tape.
	 * Not owned. Null unless a test or a recorder is listening.
	 */
	TArray<FGFArenaPlan>* PlanLog = nullptr;
	TArray<uint32>* HashLog = nullptr;
	bool bReplayPlans = false;
	int32 ReplayCursor = 0;

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
	/** "Ok", NotOwner, Fainted, Taken, Wary, or Lockout (2 s after a switch). */
	FName Possess(const FString& Key, int32 UnitId);
	/** Next living dino of yours after the current one; INDEX_NONE = back on the balcony. A lockout returns the current id. */
	int32 Cycle(const FString& Key);
	void Release(const FString& Key);
	int32 PossessedBy(const FString& Key) const;
	FName SetInput(const FString& Key, const FVector2D& Move, bool bAttack, int32 AbilitySlot);

	// ---- Spells and the brain ----
	void SetLoadout(int32 Team, const TArray<FName>& Spells);
	/** bFromPlayer requires the Slow Time window and a loadout entry. Champion AI passes false. */
	FName CastSpell(const FGFArenaData& Data, int32 Team, FName SpellId, int32 TargetId, bool bAlt, bool bFromPlayer);
	/** One window closed. RallyWhistle and the other window locks tick down. */
	void OnSlowWindowEnded();
	/** Best order for each living dino on Team that the brain would give. Slot -1 means "just attack". */
	void BuildGhostOrders(const FGFArenaData& Data, int32 Team, TArray<FGFArenaCommand>& Out);
	/** FNV mix of HP, position bits, statuses, spark and the outcome. Exact: replays must match with no tolerance. */
	uint32 StateHash() const;
	/** Milliseconds for one decision on UnitId. Tests use it against the decision budget. */
	double DebugDecisionMillis(const FGFArenaData& Data, int32 UnitId);

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
	void NoteHurt(FGFArenaUnit& To, float Amount, int32 AttackerId);
	void TickSpark(float Dt);
	void TickHotSand();
	bool RunBrain(const FGFArenaData& Data, FGFArenaUnit& U, float Dt);
	void ChooseIntent(const FGFArenaData& Data, FGFArenaUnit& U);
	void ExecuteIntent(const FGFArenaData& Data, FGFArenaUnit& U, float Dt, bool& bActed);
	void Project(const FGFArenaData& Data, float Horizon);
	float ScoreFor(int32 Team) const;
	float TerrainTerm(const FGFArenaUnit& U) const;
	bool DiesWithin(const FGFArenaUnit& U, float Seconds) const;
	TArray<FName>& LoadoutFor(int32 Team);
	const TArray<FName>& LoadoutFor(int32 Team) const;
	TMap<FName, int32>& SpellLockFor(int32 Team);
	int32 SpellLockOf(int32 Team, FName SpellId) const;
	float AttackOf(const FGFArenaUnit& U) const;
	bool WallBlocks(const FGFArenaUnit& U, float NextX) const;
	void RememberPlan(const FGFArenaUnit& U, int32 Slot, int32 TargetId, bool bRetreat);
	void RememberHash();
};

/** Combat math, pure (PLAN.md GFGame.Combat.Math golden tests). */
struct GFARENA_API FGFArenaMath
{
	/** Damage dealt by Attack x Mult to a target with the given statuses. */
	static float Damage(float Attack, float Mult, bool bWellFed, bool bTargetShred, bool bTargetGuard, float ComboMult = 1.f);
	/** 1 old point = 8% of MaxHP, then shred and guard. */
	static constexpr float PointFraction = 0.08f;
	static float SpellDamage(float MaxHP, int32 Points, bool bTargetShred, bool bTargetGuard);
	static float LevelScale(int32 Level) { return 1.f + 0.1f * (FMath::Max(1, Level) - 1); }
	static constexpr float HighGroundWeight = 2.8f;
	static constexpr float MireWeight = -0.8f;
};
