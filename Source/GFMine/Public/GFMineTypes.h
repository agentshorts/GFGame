#pragma once

// Pebble Mine (PLAN.md M8): a crew of shy pebblings (unlocked by her first tame) mines the room each one is
// assigned to, fills its satchel, and carries the haul home when told (or by itself with Auto-Haul).
// Upgrades are bought with materials. While she's away the crew keeps gathering at a reduced, capped rate.
// Everything here is pure and deterministic; AGFMineDirector runs it on the server.
#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Engine/DataTable.h"
#include "GFItemTypes.h"
#include "GFRewards.h"
#include "GFModeTypes.h"
#include "GFMineTypes.generated.h"

/** DT_MineRooms: row name = room id. */
USTRUCT(BlueprintType)
struct GFMINE_API FGFMineRoomRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Upgrade that opens the room (None = open from the start). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine") FName NeedsUpgrade;
	/** One satchel-load's dig (rolled each work cycle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine") TArray<FGFRewardEntry> Yields;
};

/** DT_MineUpgrades: one row per level. */
USTRUCT(BlueprintType)
struct GFMINE_API FGFMineUpgradeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine") FName Upgrade;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine") int32 Level = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine") TArray<FGFItemStack> Cost;
};

namespace GFMineUpgrades
{
	static const FName Picks(TEXT("Picks"));             // faster digging
	static const FName Satchels(TEXT("Satchels"));       // bigger loads
	static const FName Rails(TEXT("Rails"));             // quicker trips
	static const FName CrewQuarters(TEXT("CrewQuarters")); // more pebblings
	static const FName Tunnels(TEXT("Tunnels"));         // opens the Crystal Gallery
	static const FName Lanterns(TEXT("Lanterns"));       // opens the Glow Grotto
	static const FName AutoHaul(TEXT("AutoHaul"));       // automation: full satchels go home by themselves
	static const FName NightShift(TEXT("NightShift"));   // longer offline cap
}

/** Numbers that follow from the upgrades. */
struct GFMINE_API FGFMineStats
{
	float WorkSeconds = 60.f;
	float TravelSeconds = 20.f;
	int32 CarryCap = 12;
	int32 CrewCap = 2;
	bool bAutoHaul = false;
	float OfflineCapHours = 4.f;
	float OfflineRate = 0.5f;

	static FGFMineStats For(const FGFMineCrew& Crew);
	static constexpr float BaseWorkSeconds = 60.f;
	static constexpr float BaseTravelSeconds = 20.f;
	/** Offline value ceiling per capped hour, whole crew. */
	static constexpr int32 OfflineValuePerHour = 150;
};

struct GFMINE_API FGFMineData
{
	TMap<FName, FGFMineRoomRow> Rooms;
	TArray<FGFMineUpgradeRow> Upgrades;
	static const FGFMineData& Get();
	const FGFMineRoomRow* Room(FName Id) const { return Rooms.Find(Id); }
	const FGFMineUpgradeRow* Upgrade(FName Id, int32 Level) const;
	int32 MaxLevel(FName Id) const;
};

/** Offline gathering result. */
struct GFMINE_API FGFMineOffline
{
	TArray<FGFItemStack> Items;
	float Hours = 0.f;
	int32 Value = 0;
};

UENUM()
enum class EGFMineOrder : uint8
{
	Ok,
	Locked,      // no tame yet
	Startled,    // came in too fast: it hides
	Hiding,
	CrewFull,
	NoPebbling,
	RoomLocked,
	UnknownRoom,
	NothingToHaul,
	MaxLevel,
	UnknownUpgrade
};

struct GFMINE_API FGFMineRules
{
	/** Approaching faster than this (cm/s) startles a pebbling. */
	static constexpr float SneakSpeed = 200.f;
	static constexpr float HideSeconds = 5.f;

	static EGFMineOrder CheckCapture(const FGFPlayerProgress& Progress, float ApproachSpeed);
	/** Adds a pebbling (named in order from an original list). */
	static FGFPebbling& AddPebbling(FGFMineCrew& Crew);
	static bool RoomOpen(const FGFMineCrew& Crew, FName Room);
	/** Sends it to Room (a satchel with something in it goes home first). */
	static EGFMineOrder Assign(FGFMineCrew& Crew, const FGuid& Id, FName Room);
	/** Orders the haul home (one pebbling, or all with an invalid id). */
	static EGFMineOrder Haul(FGFMineCrew& Crew, const FGuid& Id);
	/** Called back to the mouth (its satchel comes home too). */
	static EGFMineOrder Recall(FGFMineCrew& Crew, const FGuid& Id);
	/** Advances every pebbling by Dt. Hauls that reach home are added to OutDelivered. True if any state changed. */
	static bool Tick(FGFMineCrew& Crew, float Dt, FRandomStream& Rng, TArray<FGFItemStack>& OutDelivered);
	/** The upgrade's next level row (null when maxed / unknown). */
	static const FGFMineUpgradeRow* NextUpgrade(const FGFMineCrew& Crew, FName Upgrade, EGFMineOrder& OutWhy);
	/**
	 * Offline gathering for GapSeconds away: only assigned pebblings, capped at OfflineCapHours, at OfflineRate of
	 * the online dig rate, and never worth more than 150 per capped hour. Negative gaps (clock moved back) give 0.
	 * Deterministic for the same Seed.
	 */
	static FGFMineOffline Offline(const FGFMineCrew& Crew, double GapSeconds, int32 Seed, const UDataTable* Items);
	/** Expected value of one load from Room (item values from DT_Items). */
	static float ExpectedLoadValue(FName Room, const UDataTable* Items);
	/** Best-case online value per minute for this crew setup (every slot in the best open room, no travel). */
	static float MaxOnlineValuePerMinute(const FGFMineCrew& Crew, const UDataTable* Items);
};
