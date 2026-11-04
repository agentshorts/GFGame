#pragma once

// The hotbar: nine slots along the bottom of the screen. Pure rules shared by the HUD, the character and the
// server, so the wheel, the number keys and the default fill are one tested thing.

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "GFHotbarRules.generated.h"

/** A placeable block and the item it spends (AGFPlayerState::ItemForBlock). */
struct GFCORE_API FGFHotbarBlock
{
	FName BlockId;
	FName ItemId;
};

/**
 * A bar the player arranged in the inventory screen. Saved with the character (FGFPlayerProgress::Hotbar).
 * Until bArranged is set the bar follows FGFHotbarRules::DefaultSlots, so older saves keep the automatic fill.
 */
USTRUCT(BlueprintType)
struct GFCORE_API FGFHotbarLayout
{
	GENERATED_BODY()

	/** SlotCount ids once arranged. None is an empty slot; it can also hold Fists or any item, not only blocks. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Hotbar")
	TArray<FName> Slots;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Hotbar")
	bool bArranged = false;
};

UENUM()
enum class EGFHotbarMoveKind : uint8
{
	/** Put Id in Slot. If Id is already on the bar, the two slots trade places. Id None clears. */
	Assign,
	/** Slot and Other trade contents. */
	Swap,
	Clear
};

/** One hotbar edit from the inventory screen. Sent to the server, which checks it and applies it. */
USTRUCT()
struct GFCORE_API FGFHotbarMove
{
	GENERATED_BODY()

	UPROPERTY()
	EGFHotbarMoveKind Kind = EGFHotbarMoveKind::Assign;

	UPROPERTY()
	int32 Slot = 0;

	/** Swap only. */
	UPROPERTY()
	int32 Other = 0;

	/** Assign only. */
	UPROPERTY()
	FName Id;

	static FGFHotbarMove Assign(int32 InSlot, FName InId);
	static FGFHotbarMove Swap(int32 A, int32 B);
	static FGFHotbarMove Clear(int32 InSlot);
	bool operator==(const FGFHotbarMove& Rhs) const { return Kind == Rhs.Kind && Slot == Rhs.Slot && Other == Rhs.Other && Id == Rhs.Id; }
};

UENUM()
enum class EGFHotbarMoveResult : uint8
{
	Ok,
	/** Valid but already true (dropping an item on the slot that holds it). Nothing is saved. */
	NoChange,
	BadSlot,
	/** Not a DT_Items row, a placeable block, Fists or None. */
	UnknownItem,
	/** The player has none of it (free build lets any placeable block through). */
	NotOwned,
	/** Too many moves this second. */
	Busy,
	/** No server answer yet (client bookkeeping only). */
	Pending
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

	/** The bar to show: the arranged slots (padded to SlotCount), or Default while nothing was arranged. */
	static TArray<FName> Resolve(const FGFHotbarLayout& Layout, const TArray<FName>& Default);

	/** Applies Move to Slots (padded to SlotCount first). Ownership is not checked here; see CheckId. */
	static EGFHotbarMoveResult Apply(TArray<FName>& Slots, const FGFHotbarMove& Move);

	/**
	 * May a player put Id on their bar? None and Fists always. Otherwise it must be known (an item row or a
	 * placeable block) and owned, or a placeable block in free build.
	 */
	static EGFHotbarMoveResult CheckId(FName Id, bool bKnown, bool bPlaceable, int64 Owned, bool bFreeBuild);

	/** Most moves the server takes from one player per second. */
	static constexpr int32 MaxMovesPerSecond = 20;
};
