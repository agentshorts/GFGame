#pragma once

// The Elder's Errand, as data. No world, no actors. UGFQuestSubsystem applies a FGFQuestLine and copies the
// step, count, and rank onto the player state.
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFItemTypes.h"
#include "GFQuestTypes.generated.h"

/** One step of The Elder's Errand. The order is the order of the line. */
UENUM(BlueprintType)
enum class EGFQuestStep : uint8
{
	TakeCrystal,
	Escape,
	TalkToElder,
	HuntHide,
	HuntSkins,
	CraftLook,
	WinShow,
	Done
};

/** Faction standing. Leader is recorded when the show is won. The seat change is a later step. */
UENUM(BlueprintType)
enum class EGFFactionRank : uint8
{
	Outsider,
	Initiate,
	Member,
	Champion,
	Leader
};

/** What happened. Later pieces raise these. The line ignores anything that is not legal for the current step. */
UENUM(BlueprintType)
enum class EGFQuestEventKind : uint8
{
	CrystalTaken,
	HeatCleared,
	ElderAccepted,
	TurnIn,
	OutfitCrafted,
	OutfitWorn,
	ShowEntered,
	ShowPlaced
};

/** How Apply finished. Ignored means the event did not belong at this step. Refused means it did, and the rules said no. */
UENUM(BlueprintType)
enum class EGFQuestResult : uint8
{
	Ok,
	Ignored,
	Refused
};

/** Reason codes on FGFQuestApplyResult. */
struct GFCORE_API FGFQuestCodes
{
	static const FName Ok;
	static const FName OutOfOrder;
	static const FName Unknown;
	/** This town's elder will not offer the errand while the player still has heat there. */
	static const FName Heat;
	/** The offer was below the goal. Nothing was taken. */
	static const FName Short;
	static const FName WrongItem;
	static const FName WrongTown;
	static const FName NoFaction;
	/** Show place was not 1st, or the tier was below A. */
	static const FName LowScore;
	/** Already recorded (a second show entry while one is open). */
	static const FName Already;
};

/**
 * One player's place on the line.
 * Count is the beat inside the step: 0 until an outfit is crafted, then 1 until it is worn;
 * 0 until a show is entered, then 1 until it is placed. Turn-ins do not keep a partial count.
 * HeatFactions is the towns whose elders will refuse the offer.
 */
USTRUCT(BlueprintType)
struct GFCORE_API FGFQuestProgress
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	EGFQuestStep Step = EGFQuestStep::TakeCrystal;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	int32 Count = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	FName FactionId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	EGFFactionRank Rank = EGFFactionRank::Outsider;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	TArray<FName> HeatFactions;

	/** Set when an outfit is crafted, cleared when it is worn. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	FName CraftedItem;

	bool IsDefault() const
	{
		return Step == EGFQuestStep::TakeCrystal && Count == 0 && FactionId.IsNone()
			&& Rank == EGFFactionRank::Outsider && HeatFactions.Num() == 0 && CraftedItem.IsNone();
	}

	bool operator==(const FGFQuestProgress& Other) const
	{
		return Step == Other.Step && Count == Other.Count && FactionId == Other.FactionId && Rank == Other.Rank
			&& HeatFactions == Other.HeatFactions && CraftedItem == Other.CraftedItem;
	}
};

/**
 * One fact for the line.
 * TurnIn: Stacks is the offer. If Stacks is empty, ItemId and Count are one stack.
 * ShowPlaced: Count is the place (1 is first). Score is the tier (C = 0, B = 1, A = 2, S = 3).
 * CrystalTaken and ElderAccepted and HeatCleared: FactionId is the town.
 */
USTRUCT(BlueprintType)
struct GFCORE_API FGFQuestEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") EGFQuestEventKind Kind = EGFQuestEventKind::CrystalTaken;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") FString PlayerKey;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") FName FactionId;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") FName ItemId;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") int32 Count = 0;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") int32 Score = 0;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") TArray<FGFItemStack> Stacks;

	/** The turn-in offer. Empty when this is not a turn-in. */
	TArray<FGFItemStack> Offer() const;
};

/** What Apply did. Charge is the exact stacks to remove (empty when nothing is taken). */
struct GFCORE_API FGFQuestApplyResult
{
	EGFQuestResult Result = EGFQuestResult::Ignored;
	FName Reason;
	TArray<FGFItemStack> Charge;
	bool bClaimFaction = false;
	bool bBecameLeader = false;
};

/** One saved player. The world blob "Quest" holds an array of these. */
USTRUCT()
struct GFCORE_API FGFQuestRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FString PlayerKey;
	UPROPERTY(SaveGame) FGFQuestProgress Progress;
};

USTRUCT()
struct GFCORE_API FGFQuestSave
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) TArray<FGFQuestRecord> Records;
};

/**
 * DT_Quests.csv. Row name is an EGFQuestStep, or a town id (Rime, Verdant, Cinderwrights) when TownId is set.
 * Town rows list that town's two skins. Step rows are the line.
 */
USTRUCT(BlueprintType)
struct GFCORE_API FGFQuestRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FString Title;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FString Hint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") int32 Goal = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") TArray<FName> Items;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") EGFFactionRank RankOnComplete = EGFFactionRank::Outsider;
	/** Set on a town skin row. None on a step row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FName TownId;
};
