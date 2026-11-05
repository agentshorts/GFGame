#pragma once

// Side quest board, as data. Lives beside The Elder's Errand (GFQuestTypes.h) and does not touch it.
// Every player key owns one board: one progress entry per side line, plus at most one Active line.
// FGFSideQuestLine (GFSideQuestLine.h) is the only writer. UGFQuestSubsystem saves the boards in the "Quest" blob.
#include "CoreMinimal.h"
#include "GFItemTypes.h"
#include "GFSideQuestTypes.generated.h"

/** Side line id (PLACEHOLDER ids: VerdantRootward, RimeIceSpan, CinderEmberAscent). Plain FName in UPROPERTYs. */
using FGFSideQuestId = FName;

/** Where one side line stands for one player. Locked and Done stay on the board. */
UENUM(BlueprintType)
enum class EGFSideQuestStatus : uint8
{
	Locked,
	Available,
	Active,
	Done
};

/** What happened in the world. Each step names the one kind that advances it. */
UENUM(BlueprintType)
enum class EGFSideQuestEventKind : uint8
{
	TalkAccepted,
	TreeBroken,
	CrystalGathered,
	HopLanded,
	ClimbMantled,
	FerryArrived,
	HeldDelivered,
	TurnIn,
	HireSpawned
};

/** One side line on one board. StepId and Count are only meaningful while Active. */
USTRUCT(BlueprintType)
struct GFCORE_API FGFSideQuestProgress
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	FName LineId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	FName StepId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	int32 Count = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	EGFSideQuestStatus Status = EGFSideQuestStatus::Locked;

	bool operator==(const FGFSideQuestProgress& Other) const
	{
		return LineId == Other.LineId && StepId == Other.StepId && Count == Other.Count && Status == Other.Status;
	}
};

/** Every side line one player has seen, plus the single Active one (None when no line is Active). */
USTRUCT(BlueprintType)
struct GFCORE_API FGFSideQuestBoard
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	TArray<FGFSideQuestProgress> Lines;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Quest")
	FName ActiveLineId;

	const FGFSideQuestProgress* Find(FName LineId) const
	{
		return Lines.FindByPredicate([LineId](const FGFSideQuestProgress& P) { return P.LineId == LineId; });
	}

	FGFSideQuestProgress* Find(FName LineId)
	{
		return Lines.FindByPredicate([LineId](const FGFSideQuestProgress& P) { return P.LineId == LineId; });
	}

	const FGFSideQuestProgress* Active() const
	{
		return ActiveLineId.IsNone() ? nullptr : Find(ActiveLineId);
	}

	/** Nothing worth saving: no line Active or Done. Locked and Available are rebuilt from the errand rank on load. */
	bool IsDefault() const
	{
		if (!ActiveLineId.IsNone())
		{
			return false;
		}
		for (const FGFSideQuestProgress& P : Lines)
		{
			if (P.Status == EGFSideQuestStatus::Active || P.Status == EGFSideQuestStatus::Done)
			{
				return false;
			}
		}
		return true;
	}

	bool operator==(const FGFSideQuestBoard& Other) const
	{
		return ActiveLineId == Other.ActiveLineId && Lines == Other.Lines;
	}
};

/**
 * One fact for the board.
 * TalkAccepted: LineId is the offered line, TownId is the elder's town.
 * TurnIn: Stacks is the offer. If Stacks is empty, ItemId and Count are one stack.
 * HeldDelivered: ItemId is the held item, TownId is the elder's town.
 * Other kinds: Count is how many (0 counts as 1). TownId, when set, must be the Active line's town.
 * Score is free for adapters (for example a hop grade); QL-a does not read it.
 */
USTRUCT(BlueprintType)
struct GFCORE_API FGFSideQuestEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") EGFSideQuestEventKind Kind = EGFSideQuestEventKind::TalkAccepted;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") FString PlayerKey;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") FName LineId;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") FName TownId;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") FName ItemId;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") int32 Count = 0;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") int32 Score = 0;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Quest") TArray<FGFItemStack> Stacks;

	/** The turn-in offer. Empty when this is not a turn-in. */
	TArray<FGFItemStack> Offer() const
	{
		if (Kind != EGFSideQuestEventKind::TurnIn)
		{
			return {};
		}
		if (Stacks.Num() > 0)
		{
			return Stacks;
		}
		if (!ItemId.IsNone() && Count > 0)
		{
			return { FGFItemStack(ItemId, Count) };
		}
		return {};
	}
};

/** One saved board. FGFQuestSave::SideBoards holds an array of these next to the errand records. */
USTRUCT()
struct GFCORE_API FGFSideQuestBoardRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FString PlayerKey;
	UPROPERTY(SaveGame) FGFSideQuestBoard Board;
};
