#pragma once

// Material reward rolls shared by every mini-game (arena, Depths, mine, lab). Materials only (PLAN.md section 2).
#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "GFItemTypes.h"
#include "GFRewards.generated.h"

class UDataTable;

/** One possible drop: Chance (0..1) to give Min..Max of ItemId (before any multiplier). */
USTRUCT(BlueprintType)
struct GFCORE_API FGFRewardEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward") FName ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward") int32 Min = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward") int32 Max = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward") float Chance = 1.f;
};

struct GFCORE_API FGFRewards
{
	/** Rolls every entry once; amounts are scaled by Multiplier (rounded, at least 1 when it drops). Adds into Out (merged by id). */
	static void Roll(const TArray<FGFRewardEntry>& Entries, FRandomStream& Rng, float Multiplier, TArray<FGFItemStack>& Out);
	/** Adds Count of ItemId into Stacks (merging by id). */
	static void AddTo(TArray<FGFItemStack>& Stacks, FName ItemId, int32 Count);
	static void AddAll(TArray<FGFItemStack>& Stacks, const TArray<FGFItemStack>& More);
	static int32 CountOf(const TArray<FGFItemStack>& Stacks, FName ItemId);
	/** True if any stack is an item whose DT_Items row has Exclusive == Which. */
	static bool ContainsExclusive(const UDataTable* Items, const TArray<FGFItemStack>& Stacks, EGFExclusive Which);
	static bool ContainsExclusive(const UDataTable* Items, const TArray<FGFRewardEntry>& Entries, EGFExclusive Which);
};
