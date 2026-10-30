#pragma once

// Overtime (the after-hours lab): a solo scavenging shift through a closed research lab. Each shift deals out
// junk piles (filing cabinets, break-room fridges, server racks, specimen tanks) from DT_LabJunk; searching one
// takes a few seconds and rolls its loot (the only source of the Lab exclusives). After ShiftSeconds the lab
// goes into overtime: loot is 1.5x, but at LockdownSeconds the doors seal and only 75 % comes home. Extracting
// before lockdown keeps everything; leaving or dropping mid-shift keeps the evacuation share (75 %).
// Junk becomes gear at the Lab Bench (DT_Recipes, Station = LabBench): Lab Treats, Calming Mist, goggles, coats, lanterns.
// Everything here is pure and deterministic; AGFLabDirector runs it on the server.
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

USTRUCT(BlueprintType)
struct GFLAB_API FGFLabPile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lab") FName Kind;
	UPROPERTY(BlueprintReadOnly, Category = "Lab") bool bSearched = false;
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
	/** Found this shift, not home yet. */
	UPROPERTY(BlueprintReadOnly, Category = "Lab") TArray<FGFItemStack> Unbanked;
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
	Busy
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
	static const FGFLabData& Get();
	const FGFLabJunkRow* Row(FName Kind) const { return Junk.Find(Kind); }
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
	static const FName Station;

	static bool IsOvertime(float Clock) { return Clock >= ShiftSeconds; }
	/** Deals PileCount piles by weight (deterministic for the seed). */
	static TArray<FGFLabPile> DealPiles(int32 Seed);
	static FGFLabRun StartRun(const FString& Key, int32 Seed);
	/** Starts searching pile Index. */
	static EGFLabOrder BeginSearch(FGFLabRun& Run, int32 Index);
	/** Advances the clock; a finished search rolls its loot (1.5x in overtime) into Unbanked. True on lockdown. */
	static bool Tick(FGFLabRun& Run, float Dt);
	/** What comes home: everything on Extract, floor(75 %) of each stack otherwise. */
	static TArray<FGFItemStack> Bank(const TArray<FGFItemStack>& Unbanked, EGFLabEnd How);
	/** Expected value of one search of Kind at Multiplier (DT_Items values). */
	static float ExpectedSearchValue(FName Kind, const UDataTable* Items, float Multiplier = 1.f);
	/** Expected value of a shift where every pile is searched before overtime, and value per minute of that shift. */
	static float ExpectedShiftValue(const UDataTable* Items);
	static float ExpectedValuePerMinute(const UDataTable* Items);
};
