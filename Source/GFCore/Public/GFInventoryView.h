#pragma once

// What the inventory screen lists: every block the player can place and every item stack they hold, sorted into
// tabs. Pure, so the server-free grid, tooltips and tests share one answer.

#include "CoreMinimal.h"
#include "GFHotbarRules.h"

class UDataTable;
struct FGFInventory;
struct FGFItemRow;

enum class EGFInventoryTab : uint8
{
	Blocks,
	Materials,
	Crystals,
	Skins,
	Other
};

/** One grid cell. Id is what goes on the hotbar (a block id for the Blocks tab, else the item id). */
struct GFCORE_API FGFInventoryEntry
{
	FName Id;
	/** The item it counts and spends. Same as Id except for blocks that drop another item (Grass spends Dirt). */
	FName ItemId;
	/** -1 when counts do not apply (free build blocks). */
	int64 Count = 0;
	EGFInventoryTab Tab = EGFInventoryTab::Other;
	bool bPlaceable = false;
};

struct GFCORE_API FGFInventoryView
{
	static constexpr int32 TabCount = 5;

	static FString TabName(EGFInventoryTab Tab);

	/** Skins: Skin_* rows and outfits. Crystals: the Glow material category. Materials: other materials. */
	static EGFInventoryTab TabForItem(FName ItemId, const FGFItemRow* Row);

	/**
	 * Blocks first (table order; only blocks whose item the player holds, or all of them in free build), then
	 * item stacks with a positive count in DT_Items order, unknown ids last by name. Block-category items that a
	 * placeable block already stands for are listed once, under Blocks.
	 */
	static TArray<FGFInventoryEntry> Build(const FGFInventory& Bag, const TArray<FGFHotbarBlock>& Placeable, const UDataTable* Items, bool bFreeBuild);

	static TArray<FGFInventoryEntry> Filter(const TArray<FGFInventoryEntry>& Entries, EGFInventoryTab Tab);

	/** Grid pages: how many pages Count cells need at PerPage cells a page (at least 1), and a clamped page. */
	static int32 PageCount(int32 Count, int32 PerPage);
	static int32 ClampPage(int32 Page, int32 Count, int32 PerPage);
};
