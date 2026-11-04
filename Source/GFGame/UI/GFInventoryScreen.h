#pragma once

// The bag screen (Tab or I): layout, tooltip text and the pick-and-drop cursor. Pure, so the canvas HUD and the
// tests share one answer. Moves it produces go to AGFPlayerState::RequestHotbarMove; the server has the last word.

#include "CoreMinimal.h"
#include "GFHUDFormat.h"
#include "GFHotbarRules.h"
#include "GFInventoryView.h"

struct FGFItemRow;

namespace GFHUD
{
	constexpr float InvLeft = 24.f;
	/** Below the spotted banner, which stays up while the bag is open. */
	constexpr float InvTop = FieldTop + SpottedHeight + 12.f;
	constexpr float InvCell = 52.f;
	constexpr float InvGap = 6.f;
	constexpr int32 InvMaxColumns = 8;
	constexpr int32 InvMinColumns = 3;
	constexpr float InvPad = 16.f;
	constexpr float InvHeaderH = 30.f;
	constexpr float InvTabH = 26.f;
	constexpr float InvFooterH = 30.f;
	/** Space kept between the panel and the hotbar band. */
	constexpr float InvAboveHotbar = 12.f;
	constexpr float TooltipW = 260.f;
	constexpr float TooltipLineH = 18.f;
	constexpr float TooltipPad = 8.f;
}

struct GFGAME_API FGFInventoryLayout
{
	FGFHudRect Panel;
	FGFHudRect Close;
	/** One per EGFInventoryTab, left to right. */
	TArray<FGFHudRect> Tabs;
	/** One page of grid cells, row by row. */
	TArray<FGFHudRect> Cells;
	FGFHudRect Prev;
	FGFHudRect Next;
	int32 Columns = 0;
	int32 Rows = 0;
};

/**
 * Panel on the left, under the spotted banner and above the hotbar band. Columns shrink before the panel would
 * reach the interact prompt or the quest tracker (both stay up while the bag is open); at least InvMinColumns
 * and one row, so a canvas under about 900 by 500 can overlap.
 */
GFGAME_API FGFInventoryLayout GFLayoutInventory(float CanvasW, float CanvasH);

/** Index of the first rect containing (X, Y), or INDEX_NONE. */
GFGAME_API int32 GFHudHitTest(const TArray<FGFHudRect>& Rects, float X, float Y);
GFGAME_API bool GFHudContains(const FGFHudRect& Rect, float X, float Y);

/** Tooltip box next to the cursor, flipped left or up to stay on the canvas. */
GFGAME_API FGFHudRect GFPlaceTooltip(float MouseX, float MouseY, int32 Lines, float CanvasW, float CanvasH);

/**
 * Tooltip lines: name; tab, rarity and tier; how many you have; what it does; value; the how-to.
 * Row may be null (a block without an item row).
 */
GFGAME_API TArray<FString> GFFormatInventoryTooltip(const FGFInventoryEntry& Entry, const FGFItemRow* Row, const FString& DisplayName);

/** "Page 2 of 3", or empty for a single page. */
GFGAME_API FString GFFormatInventoryPage(int32 Page, int32 PageCount);

/** What the cursor is over when a button goes down or up. */
struct GFGAME_API FGFInvTarget
{
	enum class EKind : uint8
	{
		Nothing,
		Grid,
		Slot
	};

	EKind Kind = EKind::Nothing;
	/** Grid: the entry id. Slot: what the slot holds (None if empty). */
	FName Id;
	int32 Slot = INDEX_NONE;

	static FGFInvTarget Nothing() { return FGFInvTarget(); }
	static FGFInvTarget Grid(FName InId);
	static FGFInvTarget BarSlot(int32 InSlot, FName InId);
	bool SameAs(const FGFInvTarget& Other) const;
};

/**
 * Drag and click in one: press on a grid item or a full slot picks it up. Releasing somewhere else drops it there
 * (a drag). Releasing where it was pressed keeps holding it, and the next press drops it (click, then click).
 * Dropping a bar item on the grid or on nothing takes it off the bar.
 */
enum class EGFInvCursorPhase : uint8
{
	/** Nothing held, button up. */
	Idle,
	/** Button down on an item: a drag, or the first half of a click. */
	Dragging,
	/** Clicked: the item rides the cursor with the button up until the next press. */
	Holding,
	/** The press that dropped a held item (or a number key mid-drag). Its release does nothing. */
	Dropping
};

struct GFGAME_API FGFInventoryCursor
{
	TOptional<FGFHotbarMove> Press(const FGFInvTarget& Target);
	/** Every left button release while the bag is up. Off any bag box, pass FGFInvTarget::Nothing(). */
	TOptional<FGFHotbarMove> Release(const FGFInvTarget& Target);
	/** Number key k while the bag is open: the held item, else the hovered one, goes to slot k. */
	TOptional<FGFHotbarMove> NumberKey(int32 Slot, const FGFInvTarget& Hovered);
	/** Back to Idle with nothing held, from any phase. */
	void Cancel();

	EGFInvCursorPhase GetPhase() const { return Phase; }
	bool IsHolding() const { return Phase == EGFInvCursorPhase::Dragging || Phase == EGFInvCursorPhase::Holding; }
	FName GetHeld() const { return IsHolding() ? Held : FName(); }
	int32 GetFromSlot() const { return IsHolding() ? FromSlot : INDEX_NONE; }
	bool IsButtonDown() const { return Phase == EGFInvCursorPhase::Dragging || Phase == EGFInvCursorPhase::Dropping; }

private:
	/** The move for letting go of the held item over Target. Clears the payload; the caller sets the phase. */
	TOptional<FGFHotbarMove> DropOn(const FGFInvTarget& Target);
	void ClearPayload();

	EGFInvCursorPhase Phase = EGFInvCursorPhase::Idle;
	/** Dragging or Holding only. */
	FName Held;
	/** INDEX_NONE when it came from the grid. */
	int32 FromSlot = INDEX_NONE;
	FGFInvTarget PressedOn;
};
