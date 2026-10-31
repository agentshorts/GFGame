#pragma once

// Overtime (the after-hours lab): a solo scavenging shift through a closed research lab. Each shift deals out
// junk piles (filing cabinets, break-room fridges, server racks, specimen tanks) from DT_LabJunk; searching one
// takes a few seconds and rolls its loot (the only source of the Lab exclusives, along with anomaly drops).
// After ShiftSeconds the lab goes into overtime: loot is 1.5x, but at LockdownSeconds the doors seal and only
// 75 % comes home. Extracting before lockdown keeps everything; leaving or dropping mid-shift keeps the
// evacuation share (75 %).
// Anomalies (DT_LabAnomalies) are short hostile events during the shift: a printer swarm jams the search,
// a floor eats unbanked junk, a server room hides a pile. Containing one is a timed order and rolls rare junk
// (including Anomaly Shard, which stays lab-exclusive) into the same unbanked pile, so the bank rules apply.
// Junk becomes gear at the Lab Bench (DT_Recipes, Station = LabBench): Lab Treats, Calming Mist, goggles, coats, lanterns.
// Everything here is pure and deterministic; AGFLabDirector runs it on the server. Greybox markers are tinted cubes.
#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Engine/DataTable.h"
#include "GFItemTypes.h"
#include "GFRewards.h"
#include "GFLabTypes.generated.h"

/** DT_LabJunk: row name = pile kind. */
USTRUCT(BlueprintType)
struct GFLAB_API FGFLabJunkRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") float SearchSeconds = 4.f;
	/** How often it shows up in a shift. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") int32 Weight = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") TArray<FGFRewardEntry> Loot;
};

/** What a live anomaly does to the shift until somebody contains it. */
UENUM(BlueprintType)
enum class EGFLabAnomalyEffect : uint8
{
	/** The pile being searched takes SearchTimeScale times as long (1.5 = +50 %). */
	JamSearch,
	/** One item leaves Unbanked every EatInterval seconds. */
	EatUnbanked,
	/** One unsearched pile can't be searched. */
	HidePile
};

/** DT_LabAnomalies: row name = anomaly kind. Placeholder rows, no art. */
USTRUCT(BlueprintType)
struct GFLAB_API FGFLabAnomalyRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Containing it takes this long (a timed order, like a search). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") float ContainSeconds = 4.f;
	/** How often the scheduler picks it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") int32 Weight = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") EGFLabAnomalyEffect Effect = EGFLabAnomalyEffect::JamSearch;
	/** JamSearch only. 1.5 means the search takes 50 % longer while the anomaly is live. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") float SearchTimeScale = 1.5f;
	/** EatUnbanked only. Seconds between bites. 0 = never bites. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") float EatInterval = 0.f;
	/** Rolled into Unbanked when containment finishes. Overtime is 1.5x, same as a search. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lab") TArray<FGFRewardEntry> Loot;
};

USTRUCT(BlueprintType)
struct GFLAB_API FGFLabPile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lab") FName Kind;
	UPROPERTY(BlueprintReadOnly, Category = "Lab") bool bSearched = false;
	/** A HidePile anomaly is covering it. Searching it fails until that anomaly is contained. */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") bool bHidden = false;
};

/** One scheduled anomaly on a shift. The whole list replicates with the run. */
USTRUCT(BlueprintType)
struct GFLAB_API FGFLabAnomaly
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lab") FName Kind;
	/** Shift clock when it wakes. Always in [AnomalyEarliest, LockdownSeconds). */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") float StartTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Lab") bool bActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "Lab") bool bContained = false;
	/** Pile a HidePile anomaly is covering. INDEX_NONE if it isn't covering one. */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") int32 TargetPile = INDEX_NONE;
};

/** One player's shift (replicated to everyone: friends can see each other's progress). */
USTRUCT(BlueprintType)
struct GFLAB_API FGFLabRun
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lab") FString Key;
	UPROPERTY(BlueprintReadOnly, Category = "Lab") TArray<FGFLabPile> Piles;
	/** Shift seconds so far. */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") float Clock = 0.f;
	/** Pile being searched (-1 = none) and seconds left on it. */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") int32 Searching = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Lab") float SearchLeft = 0.f;
	/** Found this shift, not home yet. Searches and anomaly containment both land here. */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") TArray<FGFItemStack> Unbanked;
	/** Seeded at the start of the shift. 1 or 2 of them. */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") TArray<FGFLabAnomaly> Anomalies;
	/** Anomaly being contained (-1 = none) and seconds left on it. */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") int32 Containing = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Lab") float ContainLeft = 0.f;
	UPROPERTY() int32 Seed = 0;
	UPROPERTY() int32 Rolls = 0;
};

UENUM()
enum class EGFLabOrder : uint8
{
	Ok,
	NotInRun,
	AlreadyIn,
	BadPile,
	Searched,
	Busy,
	/** Contain index isn't a live anomaly. */
	BadAnomaly,
	/** That anomaly is already contained. */
	Contained,
	/** A HidePile anomaly is covering this pile. */
	Hidden
};

/** How a shift ended. */
UENUM()
enum class EGFLabEnd : uint8
{
	Extract,   // walked out before lockdown: keeps everything
	Lockdown,  // stayed too long: the doors seal, 75 % comes home
	Evacuate   // left or dropped mid-shift: 75 %
};

struct GFLAB_API FGFLabData
{
	TMap<FName, FGFLabJunkRow> Junk;
	TArray<FName> Kinds; // stable order for weighted picks
	TMap<FName, FGFLabAnomalyRow> Anomalies;
	TArray<FName> AnomalyKinds;
	static const FGFLabData& Get();
	const FGFLabJunkRow* Row(FName Kind) const { return Junk.Find(Kind); }
	const FGFLabAnomalyRow* Anomaly(FName Kind) const { return Anomalies.Find(Kind); }
};

struct GFLAB_API FGFLabRules
{
	static constexpr int32 PileCount = 8;
	static constexpr float ShiftSeconds = 90.f;
	static constexpr float LockdownSeconds = 135.f;
	static constexpr float OvertimeMultiplier = 1.5f;
	static constexpr float EvacuateKeep = 0.75f;
	/** Rough walk between piles, for the pacing estimate only. */
	static constexpr float WalkSeconds = 3.f;
	/** First anomaly of a shift is not scheduled before this. */
	static constexpr float AnomalyEarliest = 30.f;
	static constexpr int32 AnomalyMinCount = 1;
	static constexpr int32 AnomalyMaxCount = 2;
	static const FName Station;

	static bool IsOvertime(float Clock) { return Clock >= ShiftSeconds; }
	/** Deals PileCount piles by weight (deterministic for the seed). */
	static TArray<FGFLabPile> DealPiles(int32 Seed);
	/**
	 * 1 or 2 anomalies. Start times are in [AnomalyEarliest, LockdownSeconds) and early enough that containing
	 * one as soon as it wakes finishes before lockdown. Same seed, same list (chronological).
	 */
	static TArray<FGFLabAnomaly> ScheduleAnomalies(int32 Seed);
	static FGFLabRun StartRun(const FString& Key, int32 Seed);
	/** Starts searching pile Index. Fails with Hidden when an anomaly is covering it, Busy when containing. */
	static EGFLabOrder BeginSearch(FGFLabRun& Run, int32 Index);
	/** Starts containing anomaly Index. BadAnomaly, Contained, or Busy. */
	static EGFLabOrder BeginContain(FGFLabRun& Run, int32 Index);
	/**
	 * Advances the clock. Wakes anomalies, applies their effects, finishes a search or a containment
	 * (both roll into Unbanked, 1.5x in overtime). True on lockdown. Anomalies do not stay active at lockdown.
	 */
	static bool Tick(FGFLabRun& Run, float Dt);
	/** What comes home: everything on Extract, floor(75 %) of each stack otherwise. */
	static TArray<FGFItemStack> Bank(const TArray<FGFItemStack>& Unbanked, EGFLabEnd How);
	/** Expected value of one search of Kind at Multiplier (DT_Items values). */
	static float ExpectedSearchValue(FName Kind, const UDataTable* Items, float Multiplier = 1.f);
	/** Expected value of one containment at Multiplier, weighted by DT_LabAnomalies. */
	static float ExpectedAnomalyValue(const UDataTable* Items, float Multiplier = 1.f);
	/** Mean anomalies dealt per shift (uniform between the min and the max). */
	static float ExpectedAnomaliesPerShift() { return 0.5f * static_cast<float>(AnomalyMinCount + AnomalyMaxCount); }
	/** Expected value of a shift where every pile is searched before overtime (anomaly drops are not in this). */
	static float ExpectedShiftValue(const UDataTable* Items);
	/**
	 * Pile value plus anomaly drops, over pile time plus containment, per minute.
	 * Anomaly overtime is not in this (a player who clears them as they wake is mostly still in the shift).
	 */
	static float ExpectedValuePerMinute(const UDataTable* Items);
};
