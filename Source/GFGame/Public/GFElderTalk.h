#pragma once

// What the elder menu's Talk tab can do. Pure: the buttons call these, and the tests call them with no world.
#include "CoreMinimal.h"
#include "GFQuestLine.h"
#include "GFSideQuestLine.h"

/** Whether the server is in a conversation with one elder. */
enum class EGFElderSession : uint8
{
	Closed,
	Open
};

enum class EGFElderSessionEvent : uint8
{
	Opened,
	Closed
};

struct GFGAME_API FGFElderSessionRules
{
	static EGFElderSession Transition(EGFElderSession From, EGFElderSessionEvent Event);
};

/** Accept and hand-in for The Elder's Errand. The quest line is still the only place the step changes. */
struct GFGAME_API FGFElderTalk
{
	enum class EAction : uint8
	{
		None,
		Accept,
		HandIn
	};

	static EAction ActionFor(EGFQuestStep Step);
	/** One accept. A second call leaves the line where it was. */
	static FGFQuestApplyResult Accept(FGFQuestLine& Line, const FString& PlayerKey, FName TownId);
	/** Every accepted item the player is holding. The line charges the goal and leaves the rest. */
	static TArray<FGFItemStack> OfferFromInventory(const FGFQuestProgress& Progress, const FGFQuestCatalog& Catalog, const FGFInventory& Inventory);
	static FString RefusalText(FName Reason, EGFQuestStep Step);

	// Side lines (QL-b). A second button under the errand one; the errand button and its rules do not change.

	/** What the side button does at this elder. HandIn covers TurnIn, HeldDelivered and a later TalkAccepted check-in. */
	enum class ESideAction : uint8
	{
		None,
		Accept,
		HandIn
	};

	/** The side line this elder talks about, and what the button does. Line and Step point into the catalog. */
	struct FSideOffer
	{
		ESideAction Action = ESideAction::None;
		FName LineId;
		const FGFSideQuestLineRule* Line = nullptr;
		/** The Active step (None while offering an accept). */
		const FGFSideQuestStepRule* Step = nullptr;
		int32 Count = 0;
	};

	/** Button for one step of an Active line. The first step is done by the accept, so it is never a hand-in. */
	static ESideAction SideActionFor(const FGFSideQuestLineRule& Line, const FGFSideQuestStepRule& Step);

	/**
	 * From what the client can see: the Active line fields and the Done list on the player state, plus the errand standing.
	 * An Active line of this elder's town shows its step (HandIn, or None while the step is done out in the world).
	 * An Active line of another town shows nothing here. With no Active line, the first unlocked, not Done line of
	 * this elder's town is offered (Accept).
	 */
	static FSideOffer SideOfferFor(const FGFSideQuestCatalog& Catalog, FName ActiveLineId, FName ActiveStepId, int32 ActiveCount,
		const TArray<FName>& DoneLineIds, FName FactionId, EGFFactionRank Rank, FName ElderTownId);
	/** Same, from the server's full board. */
	static FSideOffer SideOfferFor(const FGFSideQuestCatalog& Catalog, const FGFSideQuestBoard& Board, FName FactionId, EGFFactionRank Rank, FName ElderTownId);

	/** Every item of the step's list the player holds. The line charges the goal and leaves the rest. */
	static TArray<FGFItemStack> SideOfferFromInventory(const FGFSideQuestStepRule& Step, const FGFInventory& Inventory);

	/**
	 * The event the hand-in button raises for Offer (false when the offer is not a hand-in).
	 * HeldItem only counts when the inventory still has one; an empty hand is refused by the line as WrongItem.
	 */
	static bool SideHandInEvent(const FSideOffer& Offer, const FString& PlayerKey, FName ElderTownId, FName HeldItem, const FGFInventory& Inventory, FGFSideQuestEvent& Out);

	/** Generic toast for a side line refusal. */
	static FString SideRefusalText(FName Reason);
};
