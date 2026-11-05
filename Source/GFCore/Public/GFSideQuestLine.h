#pragma once

// Pure rules for the side quest board. Apply is the only way side progress moves; Refresh only flips lines
// between Locked and Available from the player's errand standing. Mirrors FGFQuestLine.
//
// Rules:
//   One Active side line per player. TalkAccepted for a second line while one is Active: Refused OtherActive.
//   A line is offered (Available) when its town is the player's faction and the errand rank is at least
//   UnlockRank (default Member; ASK CANON on Champion). Outsiders and Initiates see no offer.
//   A line needs MinSteps steps before it is offered. Stub lines stay Locked until their content lands.
//   TalkAccepted activates the line at its first step. When that step advances on TalkAccepted, the accept
//   also completes it. Each later step advances on its own AdvanceKind until Count reaches Goal.
//   A later TalkAccepted step is a check-in: TalkAccepted for the Active line at that line's elder finishes it.
//   Unknown, out of order, or wrong town events come back Ignored or Refused and leave the board as it was.
//   A step with Items only counts events (or turn-ins) of those items. HeldDelivered needs a held item.
//   Locked and Done lines stay on the board. Done is final for v1.
#include "CoreMinimal.h"
#include "GFQuestTypes.h"
#include "GFSideQuestTypes.h"

class UWorld;
class UDataTable;

DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnSideQuestRaised, UWorld*, const FGFSideQuestEvent&);

/** Reason codes only the board uses. Shared ones (Ok, OutOfOrder, Unknown, Short, WrongItem, WrongTown) are FGFQuestCodes. */
struct GFCORE_API FGFSideQuestCodes
{
	/** The errand rank is below the line's UnlockRank. */
	static const FName RankLow;
	/** Another side line is Active. */
	static const FName OtherActive;
	/** The line is already Active or Done. */
	static const FName Already;
	/** The line has fewer than MinSteps steps (content not shipped yet). */
	static const FName NotReady;
};

struct GFCORE_API FGFSideQuestStepRule
{
	FName StepId;
	int32 Order = 0;
	FString Title;
	FString Hint;
	int32 Goal = 1;
	TArray<FName> Items;
	EGFSideQuestEventKind AdvanceKind = EGFSideQuestEventKind::TalkAccepted;
};

struct GFCORE_API FGFSideQuestLineRule
{
	/** Plan: each town line is 3 to 5 steps. */
	static constexpr int32 MinSteps = 3;

	FName LineId;
	FName TownId;
	FString Title;
	EGFFactionRank UnlockRank = EGFFactionRank::Member;
	int32 Sort = 0;
	/** Sorted by Order. */
	TArray<FGFSideQuestStepRule> Steps;

	bool IsPlayable() const { return Steps.Num() >= MinSteps; }
	int32 IndexOf(FName StepId) const;
	const FGFSideQuestStepRule* FindStep(FName StepId) const;
};

/** Side lines and their steps. Builtin matches DT_QuestLines.csv and DT_QuestSteps.csv. Load() prefers the CSVs. */
struct GFCORE_API FGFSideQuestCatalog
{
	TArray<FGFSideQuestLineRule> Lines;

	static const FGFSideQuestCatalog& Builtin();
	static FGFSideQuestCatalog Load();
	/** Lines table replaces Builtin when present. Step rows attach to their line; rows for unknown lines are skipped. */
	static FGFSideQuestCatalog FromTables(const UDataTable* LinesTable, const UDataTable* StepsTable);

	/**
	 * Town ids compare loosely: Cinderwright, Cinderwrights and Cinder are one town (new side data says
	 * Cinderwright; faction ids already in the engine say Cinderwrights). Other ids compare exactly. None never matches.
	 */
	static bool SameTown(FName A, FName B);
	static bool TryKind(FName Id, EGFSideQuestEventKind& Out);
	static FName KindId(EGFSideQuestEventKind Kind);

	const FGFSideQuestLineRule* FindLine(FName LineId) const;
	/** Sorts lines by Sort and each line's steps by Order. */
	void Normalize();
};

/** What Apply did. Charge is the exact stacks to remove (empty when nothing is taken). */
struct GFCORE_API FGFSideQuestApplyResult
{
	EGFQuestResult Result = EGFQuestResult::Ignored;
	FName Reason;
	TArray<FGFItemStack> Charge;
	/** The line went Active on this event (Elder Book SideAccept). */
	bool bAccepted = false;
	/** The line went Done on this event (Elder Book SideComplete). */
	bool bCompleted = false;
	/** The line this result is about (None when no line was found). */
	FName LineId;
};

/**
 * One player's board. Catalog null uses FGFSideQuestCatalog::Builtin().
 * FactionId and Rank are the player's errand standing; the owner copies them in before Refresh or Apply.
 */
struct GFCORE_API FGFSideQuestLine
{
	FGFSideQuestBoard Board;
	const FGFSideQuestCatalog* Catalog = nullptr;
	FName FactionId;
	EGFFactionRank Rank = EGFFactionRank::Outsider;

	FGFSideQuestApplyResult Apply(const FGFSideQuestEvent& Event);

	/** Adds a board entry for every catalog line and sets Locked / Available on lines that are neither Active nor Done. */
	void Refresh();

	/** The Active line's current step (null when no line is Active). */
	const FGFSideQuestStepRule* ActiveStep() const;

	/** Available test for one line, from standing alone. */
	static bool IsUnlocked(const FGFSideQuestLineRule& Line, FName FactionId, EGFFactionRank Rank);

private:
	const FGFSideQuestCatalog& ActiveRules() const;
	FGFSideQuestApplyResult ApplyAccept(const FGFSideQuestEvent& Event);
	FGFSideQuestApplyResult ApplyStep(const FGFSideQuestEvent& Event);
	/** Adds to the Active step and moves on when the goal is met. */
	void Advance(const FGFSideQuestLineRule& Line, FGFSideQuestProgress& Progress, int32 Add, FGFSideQuestApplyResult& Result);
};

/** Static multicast. Raise from anywhere. The quest subsystem on that world is the listener. */
struct GFCORE_API FGFSideQuestEvents
{
	static FGFOnSideQuestRaised& OnRaised();
	static void Raise(UWorld* World, const FGFSideQuestEvent& Event);

	static FGFSideQuestEvent TalkAccepted(const FString& PlayerKey, FName LineId, FName TownId);
	/** Count-style kinds (TreeBroken, CrystalGathered, CrystalTaken, HopLanded, ClimbMantled, FerryArrived, HireSpawned). */
	static FGFSideQuestEvent Progress(const FString& PlayerKey, EGFSideQuestEventKind Kind, FName TownId = NAME_None, int32 Count = 1);
	static FGFSideQuestEvent TurnIn(const FString& PlayerKey, const TArray<FGFItemStack>& Offer, FName TownId = NAME_None);
	static FGFSideQuestEvent HeldDelivered(const FString& PlayerKey, FName ItemId, FName TownId);
};
