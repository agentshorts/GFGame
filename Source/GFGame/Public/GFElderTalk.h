#pragma once

// What the elder menu's Talk tab can do. Pure: the buttons call these, and the tests call them with no world.
#include "CoreMinimal.h"
#include "GFQuestLine.h"

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
};
