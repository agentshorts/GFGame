#pragma once

// Paths: five classes, open-world finds, perks, one free respec, and the Big Grumble material gate.
// Owning and trading ignore PathTag. Run speed is never a perk.

#include "CoreMinimal.h"
#include "GFItemTypes.h"

class UDataTable;
class FRandomStream;

/** The saved class fields. The character progress stores the same four values. */
struct GFCORE_API FGFPathRecord
{
	EGFPath Path = EGFPath::None;
	bool bChosen = false;
	int32 Respecs = 0;
	int64 RespecUnix = 0;
};

enum class EGFPathLootSource : uint8
{
	OpenWorld,
	/** Crystal Gallery and Glow Grotto stay the shared back door. */
	PebbleMine,
	/** Hallow Night, Lab, and any other mode drop. */
	ModeExclusive,
	/** Common garden dyes. */
	Garden
};

struct GFCORE_API FGFPathCard
{
	EGFPath Path = EGFPath::None;
	FString Name;
	FString Blurb;
	FString IconId;
	bool bNamePending = false;
};

struct GFCORE_API FGFPathChange
{
	bool bOk = false;
	FName Code;
	/** 0 on the first pick and on the free respec. */
	int32 CostValue = 0;
};

struct GFCORE_API FGFPathRules
{
	static constexpr int32 ClassCount = 5;
	static constexpr int32 EndgamePaths = 3;
	static constexpr float OffPathRate = 0.05f;
	static constexpr int32 FreeRespecs = 1;
	static constexpr int64 RespecCooldownSeconds = 24 * 60 * 60;
	static constexpr int32 RespecCostValue = 40;

	static constexpr float DelverDigSpeed = 1.25f;
	static constexpr int32 DelverExtraDepth = 2;
	static constexpr float DelverHazardDamage = 0.5f;
	static constexpr float WranglerTameScale = 0.7f;
	static constexpr float WranglerWarningScale = 1.5f;
	static constexpr float WranglerRentScale = 0.8f;
	static constexpr float WranglerRepScale = 1.25f;
	static constexpr float ScavengerGlintScale = 2.f;
	/** Held until those modes leave hold. Every class, including Delver and Wrangler. */
	static constexpr int32 HeldCrewSlots = 0;
	static constexpr float HeldCartBonus = 0.f;

	static int32 SlotCount();
	static bool PathFromSlot(int32 Slot, EGFPath& OutPath);
	static int32 SlotOf(EGFPath Path);
	static bool IsNamed(EGFPath Path);
	static bool IsPlayable(EGFPath Path);
	static FString DisplayName(EGFPath Path);
	static FString IconId(EGFPath Path);
	static TArray<FGFPathCard> Cards();

	/** 1 for every class. Scavenger does not run faster, and neither does anyone else. */
	static float MoveSpeedMultiplier(EGFPath Path);
	static float DigSpeedMultiplier(EGFPath Path);
	static int32 ExtraDigLayers(EGFPath Path);
	static float HazardDamageMultiplier(EGFPath Path);
	static float TameFoodMultiplier(EGFPath Path);
	static float TameTimeMultiplier(EGFPath Path);
	static float WarningTimeMultiplier(EGFPath Path);
	static float RentMultiplier(EGFPath Path);
	static float RepGainMultiplier(EGFPath Path);
	static float GlintRangeMultiplier(EGFPath Path);
	static int32 CrewSlotBonus(EGFPath Path);
	static float CartBonus(EGFPath Path);

	/**
	 * True when this finder keeps a drop that already succeeded its own roll.
	 * On-path and untagged drops always stay. Off-path open-world drops stay at OffPathRate.
	 * Pebble Mine, mode exclusives and garden dyes always stay.
	 */
	static bool KeepsDrop(EGFPath Finder, EGFPath ItemPath, EGFPathLootSource Source, FRandomStream& Rng);
	static float KeepChance(EGFPath Finder, EGFPath ItemPath, EGFPathLootSource Source);

	/** Path never blocks holding, gifting, mail, wagers or rent. Account-bound rules live in the economy. */
	static bool CanHold(EGFPath Finder, EGFPath ItemPath);
	static bool CanTransfer(EGFPath From, EGFPath ItemPath);

	static int32 DistinctPaths(const TArray<FGFItemStack>& Stacks, const UDataTable* Items);
	static int32 DistinctPaths(const TArray<FGFInventory>& Bags, const UDataTable* Items);
	/** True when the pooled bags hold unique items from at least EndgamePaths classes. */
	static bool MeetsEndgame(const TArray<FGFInventory>& Bags, const UDataTable* Items);
	/**
	 * Removes one path-tagged item from each of EndgamePaths classes (lowest enum slot first).
	 * Each item comes out of whichever bag already holds it. Fails without changing the bags when the gate is short.
	 */
	static bool SpendEndgame(TArray<FGFInventory>& Bags, const UDataTable* Items);

	static FGFPathChange Evaluate(const FGFPathRecord& Record, EGFPath NewPath, int64 NowUnix, int32 AvailableValue);
	static void Commit(FGFPathRecord& Record, EGFPath NewPath, int64 NowUnix);

	/** Tradeable, not account-bound, value > 0. */
	static int32 TradeableValue(const UDataTable* Items, const FGFInventory& Bag);
	/** Removes value, cheapest first. No change when the bag cannot cover Cost. Cost <= 0 succeeds. */
	static bool PayValue(const UDataTable* Items, FGFInventory& Bag, int32 Cost);
};
