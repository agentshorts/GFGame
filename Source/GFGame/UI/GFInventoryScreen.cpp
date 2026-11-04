#include "GFInventoryScreen.h"
#include "GFItemTypes.h"

FGFInventoryLayout GFLayoutInventory(float CanvasW, float CanvasH)
{
	using namespace GFHUD;
	FGFInventoryLayout Out;
	if (CanvasW <= 1.f || CanvasH <= 1.f)
	{
		return Out;
	}
	// Keep clear of the interact prompt under the crosshair and the quest tracker on the right.
	const float RightLimit = FMath::Min(CanvasW * 0.5f - InteractWidth * 0.5f - 8.f, CanvasW - FieldMargin - TrackerWidth - 8.f);
	const float Room = RightLimit - InvLeft - InvPad * 2.f + InvGap;
	Out.Columns = FMath::Clamp(FMath::FloorToInt(Room / (InvCell + InvGap)), InvMinColumns, InvMaxColumns);

	const float Bottom = GFHudHotbarTop(CanvasH) - InvAboveHotbar;
	const float GridTop = InvTop + InvHeaderH + InvTabH + 10.f;
	const float GridRoom = Bottom - GridTop - InvFooterH - InvPad + InvGap;
	Out.Rows = FMath::Max(1, FMath::FloorToInt(GridRoom / (InvCell + InvGap)));

	const float GridW = Out.Columns * InvCell + (Out.Columns - 1) * InvGap;
	const float GridH = Out.Rows * InvCell + (Out.Rows - 1) * InvGap;
	Out.Panel = { InvLeft, InvTop, GridW + InvPad * 2.f, (GridTop - InvTop) + GridH + InvPad + InvFooterH };
	Out.Close = { Out.Panel.Right() - InvPad - 24.f, InvTop + 4.f, 24.f, 22.f };

	const float TabGap = 4.f;
	const float TabW = (GridW - TabGap * (FGFInventoryView::TabCount - 1)) / FGFInventoryView::TabCount;
	for (int32 Index = 0; Index < FGFInventoryView::TabCount; ++Index)
	{
		Out.Tabs.Add({ InvLeft + InvPad + Index * (TabW + TabGap), InvTop + InvHeaderH, TabW, InvTabH - 4.f });
	}
	for (int32 Row = 0; Row < Out.Rows; ++Row)
	{
		for (int32 Col = 0; Col < Out.Columns; ++Col)
		{
			Out.Cells.Add({ InvLeft + InvPad + Col * (InvCell + InvGap), GridTop + Row * (InvCell + InvGap), InvCell, InvCell });
		}
	}
	const float FooterY = GridTop + GridH + 8.f;
	Out.Prev = { InvLeft + InvPad, FooterY, 64.f, 22.f };
	Out.Next = { Out.Panel.Right() - InvPad - 64.f, FooterY, 64.f, 22.f };
	return Out;
}

bool GFHudContains(const FGFHudRect& Rect, float X, float Y)
{
	return !Rect.IsEmpty() && X >= Rect.X && X < Rect.Right() && Y >= Rect.Y && Y < Rect.Bottom();
}

int32 GFHudHitTest(const TArray<FGFHudRect>& Rects, float X, float Y)
{
	for (int32 Index = 0; Index < Rects.Num(); ++Index)
	{
		if (GFHudContains(Rects[Index], X, Y))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FGFHudRect GFPlaceTooltip(float MouseX, float MouseY, int32 Lines, float CanvasW, float CanvasH)
{
	using namespace GFHUD;
	FGFHudRect Box;
	Box.W = FMath::Min(TooltipW, FMath::Max(0.f, CanvasW - 8.f));
	Box.H = TooltipPad * 2.f + FMath::Max(1, Lines) * TooltipLineH;
	Box.X = MouseX + 18.f;
	Box.Y = MouseY + 18.f;
	if (Box.Right() > CanvasW - 4.f)
	{
		Box.X = MouseX - 12.f - Box.W;
	}
	if (Box.Bottom() > CanvasH - 4.f)
	{
		Box.Y = MouseY - 12.f - Box.H;
	}
	Box.X = FMath::Clamp(Box.X, 4.f, FMath::Max(4.f, CanvasW - 4.f - Box.W));
	Box.Y = FMath::Clamp(Box.Y, 4.f, FMath::Max(4.f, CanvasH - 4.f - Box.H));
	return Box;
}

namespace
{
	FString RarityName(EGFRarity Rarity)
	{
		switch (Rarity)
		{
		case EGFRarity::Uncommon: return TEXT("Uncommon");
		case EGFRarity::Rare: return TEXT("Rare");
		default: return TEXT("Common");
		}
	}

	FString UseLine(const FGFInventoryEntry& Entry, const FGFItemRow* Row)
	{
		if (Entry.bPlaceable)
		{
			return TEXT("Places a block (RMB)");
		}
		if (!Row)
		{
			return FString();
		}
		switch (Row->Category)
		{
		case EGFItemCategory::Treat: return TEXT("Held in hand: dinos notice it");
		case EGFItemCategory::Seed: return TEXT("Plant it in a garden plot");
		case EGFItemCategory::Outfit: return TEXT("Wear it from the wardrobe");
		case EGFItemCategory::Furniture: return TEXT("Decor for your plot");
		case EGFItemCategory::Tool: return TEXT("A gadget you can use");
		case EGFItemCategory::Material: return TEXT("Crafting and trade material");
		default: return FString();
		}
	}
}

TArray<FString> GFFormatInventoryTooltip(const FGFInventoryEntry& Entry, const FGFItemRow* Row, const FString& DisplayName)
{
	TArray<FString> Lines;
	Lines.Add(DisplayName.IsEmpty() ? FName::NameToDisplayString(Entry.Id.ToString(), false) : DisplayName);
	FString Kind = FGFInventoryView::TabName(Entry.Tab);
	if (Row)
	{
		Kind += FString::Printf(TEXT(", %s"), *RarityName(Row->Rarity));
		if (Row->Tier > 0)
		{
			Kind += FString::Printf(TEXT(", tier %d"), Row->Tier);
		}
	}
	Lines.Add(Kind);
	Lines.Add(Entry.Count < 0 ? FString(TEXT("Free build: no cost")) : FString::Printf(TEXT("You have %lld"), Entry.Count));
	const FString Use = UseLine(Entry, Row);
	if (!Use.IsEmpty())
	{
		Lines.Add(Use);
	}
	if (Row && Row->Value > 0)
	{
		Lines.Add(FString::Printf(TEXT("Value %d"), Row->Value));
	}
	Lines.Add(TEXT("Drag to the bar, or hover and press 1-9"));
	return Lines;
}

FString GFFormatInventoryPage(int32 Page, int32 PageCount)
{
	if (PageCount <= 1)
	{
		return FString();
	}
	return FString::Printf(TEXT("Page %d of %d"), FMath::Clamp(Page, 0, PageCount - 1) + 1, PageCount);
}

FGFInvTarget FGFInvTarget::Grid(FName InId)
{
	FGFInvTarget Out;
	Out.Kind = EKind::Grid;
	Out.Id = InId;
	return Out;
}

FGFInvTarget FGFInvTarget::BarSlot(int32 InSlot, FName InId)
{
	FGFInvTarget Out;
	Out.Kind = EKind::Slot;
	Out.Slot = InSlot;
	Out.Id = InId;
	return Out;
}

bool FGFInvTarget::SameAs(const FGFInvTarget& Other) const
{
	if (Kind != Other.Kind)
	{
		return false;
	}
	switch (Kind)
	{
	case EKind::Grid: return Id == Other.Id;
	case EKind::Slot: return Slot == Other.Slot;
	default: return true;
	}
}

TOptional<FGFHotbarMove> FGFInventoryCursor::DropOn(const FGFInvTarget& Target)
{
	TOptional<FGFHotbarMove> Move;
	if (Target.Kind == FGFInvTarget::EKind::Slot && FGFHotbarRules::Wrap(Target.Slot) == Target.Slot)
	{
		if (FromSlot != INDEX_NONE)
		{
			if (FromSlot != Target.Slot)
			{
				Move = FGFHotbarMove::Swap(FromSlot, Target.Slot);
			}
		}
		else
		{
			Move = FGFHotbarMove::Assign(Target.Slot, Held);
		}
	}
	else if (FromSlot != INDEX_NONE)
	{
		// Off the bar: back into the bag.
		Move = FGFHotbarMove::Clear(FromSlot);
	}
	Cancel();
	return Move;
}

TOptional<FGFHotbarMove> FGFInventoryCursor::Press(const FGFInvTarget& Target)
{
	if (IsHolding() && !bDown)
	{
		bSwallowRelease = true;
		return DropOn(Target);
	}
	Cancel();
	if (Target.Id.IsNone() || Target.Kind == FGFInvTarget::EKind::Nothing)
	{
		return {};
	}
	Held = Target.Id;
	FromSlot = Target.Kind == FGFInvTarget::EKind::Slot ? Target.Slot : INDEX_NONE;
	PressedOn = Target;
	bDown = true;
	return {};
}

TOptional<FGFHotbarMove> FGFInventoryCursor::Release(const FGFInvTarget& Target)
{
	if (bSwallowRelease)
	{
		bSwallowRelease = false;
		return {};
	}
	if (!bDown)
	{
		return {};
	}
	bDown = false;
	if (Target.SameAs(PressedOn))
	{
		// A click: keep holding it until the next press.
		return {};
	}
	return DropOn(Target);
}

TOptional<FGFHotbarMove> FGFInventoryCursor::NumberKey(int32 Slot, const FGFInvTarget& Hovered)
{
	if (Slot < 0 || Slot >= FGFHotbarRules::SlotCount)
	{
		return {};
	}
	if (IsHolding())
	{
		bDown = false;
		return DropOn(FGFInvTarget::BarSlot(Slot, NAME_None));
	}
	if (Hovered.Kind == FGFInvTarget::EKind::Grid && !Hovered.Id.IsNone())
	{
		return FGFHotbarMove::Assign(Slot, Hovered.Id);
	}
	if (Hovered.Kind == FGFInvTarget::EKind::Slot && Hovered.Slot != Slot && FGFHotbarRules::Wrap(Hovered.Slot) == Hovered.Slot)
	{
		return FGFHotbarMove::Swap(Hovered.Slot, Slot);
	}
	return {};
}

void FGFInventoryCursor::Cancel()
{
	Held = NAME_None;
	FromSlot = INDEX_NONE;
	PressedOn = FGFInvTarget();
	bDown = false;
}
