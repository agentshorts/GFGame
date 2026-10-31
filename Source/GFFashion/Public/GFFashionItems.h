#pragma once

// Maps a fashion item type (Data/fashion/fashion_brands_v1.json) onto EGFOutfitSlot.
// The names written into DT_Items.OutfitSlot must match RowNameForSlot.
// Keep the type table in sync with TYPE_SLOT in Tools/gen_fashion_items.py.

#include "CoreMinimal.h"
#include "GFFashionTypes.h"

struct GFFASHION_API FGFFashionItems
{
	/** Brand-list type -> slot. False for a type this game does not wear. */
	static bool SlotForItemType(const FString& ItemType, EGFOutfitSlot& OutSlot);

	/** DT_Items.OutfitSlot text -> slot. False for None and for anything else. */
	static bool SlotFromRowName(FName SlotName, EGFOutfitSlot& OutSlot);

	/** The CSV token for a slot. */
	static FName RowNameForSlot(EGFOutfitSlot Slot);
};
