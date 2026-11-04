#pragma once

// The hotbar: nine slots along the bottom of the screen. Pure rules shared by the HUD, the character and the
// server, so the wheel, the number keys and the default fill are one tested thing.

#include "CoreMinimal.h"
#include "Templates/Function.h"

/** A placeable block and the item it spends (AGFPlayerState::ItemForBlock). */
struct GFCORE_API FGFHotbarBlock
{
	FName BlockId;
	FName ItemId;
};

/** What a number key press means right now. */
enum class EGFNumberKeyUse : uint8
{
	Ignore,
	/** Keys 1 to 5 pick a class while the class choice is still open. */
	Path,
	Hotbar
};

struct GFCORE_API FGFHotbarRules
{
	static constexpr int32 SlotCount = 9;

	/** The bare-hands entry. Same name as GFAim::FistsId. */
	static FName FistsId();

	/** Any index into [0, SlotCount). */
	static int32 Wrap(int32 Index);

	/** Mouse wheel step. A current slot outside the bar starts from slot 0. */
	static int32 Cycle(int32 Current, int32 Direction);

	/**
	 * KeyIndex is 0 for the 1 key up to 8 for the 9 key. While bPathPending, keys inside the class list pick a
	 * class and the rest still select slots. Anything outside the bar is ignored.
	 */
	static EGFNumberKeyUse RouteNumberKey(int32 KeyIndex, bool bPathPending);

	/**
	 * The bar a player gets before arranging it: one block per item they can spend, in table order, then Fists in
	 * the last slot. When several blocks spend the same item, the block named after the item wins (Dirt over
	 * Grass). bFreeBuild ignores the counts. Always SlotCount entries; empty slots are None.
	 */
	static TArray<FName> DefaultSlots(const TArray<FGFHotbarBlock>& Placeable, TFunctionRef<int64(FName)> CountOf, bool bFreeBuild);

	/** Slot count label. Negative means "no count" (Fists, empty) and is blank. 1000 and up is "999+". */
	static FString FormatCount(int64 Count);

	/** Object path of the convention icon for an item: /Game/Art/Items/Textures/T_Icon_<Id>.T_Icon_<Id>. */
	static FString ConventionIconPath(FName ItemId);

	/** Up to two letters for a slot without an icon: "WoodPlank" is "WP", "Dirt" is "Di". */
	static FString ShortLabel(FName Id);
};
