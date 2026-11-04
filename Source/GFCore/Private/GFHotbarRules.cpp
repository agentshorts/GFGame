#include "GFHotbarRules.h"
#include "GFPathRules.h"

FName FGFHotbarRules::FistsId()
{
	return FName(TEXT("Fists"));
}

int32 FGFHotbarRules::Wrap(int32 Index)
{
	return ((Index % SlotCount) + SlotCount) % SlotCount;
}

int32 FGFHotbarRules::Cycle(int32 Current, int32 Direction)
{
	if (Current < 0 || Current >= SlotCount)
	{
		return 0;
	}
	if (Direction == 0)
	{
		return Current;
	}
	return Wrap(Current + (Direction > 0 ? 1 : -1));
}

EGFNumberKeyUse FGFHotbarRules::RouteNumberKey(int32 KeyIndex, bool bPathPending)
{
	if (KeyIndex < 0 || KeyIndex >= SlotCount)
	{
		return EGFNumberKeyUse::Ignore;
	}
	if (bPathPending && KeyIndex < FGFPathRules::SlotCount())
	{
		return EGFNumberKeyUse::Path;
	}
	return EGFNumberKeyUse::Hotbar;
}

TArray<FName> FGFHotbarRules::DefaultSlots(const TArray<FGFHotbarBlock>& Placeable, TFunctionRef<int64(FName)> CountOf, bool bFreeBuild)
{
	TArray<FName> Items;
	TMap<FName, FName> BlockForItem;
	for (const FGFHotbarBlock& Block : Placeable)
	{
		if (Block.BlockId.IsNone())
		{
			continue;
		}
		const FName Item = Block.ItemId.IsNone() ? Block.BlockId : Block.ItemId;
		if (FName* Existing = BlockForItem.Find(Item))
		{
			if (Block.BlockId == Item)
			{
				*Existing = Block.BlockId;
			}
			continue;
		}
		BlockForItem.Add(Item, Block.BlockId);
		Items.Add(Item);
	}

	TArray<FName> Out;
	Out.Init(NAME_None, SlotCount);
	int32 Next = 0;
	for (const FName Item : Items)
	{
		if (Next >= SlotCount - 1)
		{
			break;
		}
		if (bFreeBuild || CountOf(Item) > 0)
		{
			Out[Next++] = BlockForItem.FindChecked(Item);
		}
	}
	Out[SlotCount - 1] = FistsId();
	return Out;
}

FString FGFHotbarRules::FormatCount(int64 Count)
{
	if (Count < 0)
	{
		return FString();
	}
	if (Count >= 1000)
	{
		return TEXT("999+");
	}
	return FString::Printf(TEXT("%lld"), Count);
}

FString FGFHotbarRules::ConventionIconPath(FName ItemId)
{
	if (ItemId.IsNone())
	{
		return FString();
	}
	const FString Name = FString(TEXT("T_Icon_")) + ItemId.ToString();
	return FString::Printf(TEXT("/Game/Art/Items/Textures/%s.%s"), *Name, *Name);
}

FString FGFHotbarRules::ShortLabel(FName Id)
{
	if (Id.IsNone())
	{
		return FString();
	}
	const FString S = Id.ToString();
	FString Caps;
	for (const TCHAR C : S)
	{
		if (FChar::IsUpper(C))
		{
			Caps.AppendChar(C);
			if (Caps.Len() == 2)
			{
				return Caps;
			}
		}
	}
	return S.Left(2);
}

FGFHotbarMove FGFHotbarMove::Assign(int32 InSlot, FName InId)
{
	FGFHotbarMove Move;
	Move.Kind = EGFHotbarMoveKind::Assign;
	Move.Slot = InSlot;
	Move.Id = InId;
	return Move;
}

FGFHotbarMove FGFHotbarMove::Swap(int32 A, int32 B)
{
	FGFHotbarMove Move;
	Move.Kind = EGFHotbarMoveKind::Swap;
	Move.Slot = A;
	Move.Other = B;
	return Move;
}

FGFHotbarMove FGFHotbarMove::Clear(int32 InSlot)
{
	FGFHotbarMove Move;
	Move.Kind = EGFHotbarMoveKind::Clear;
	Move.Slot = InSlot;
	return Move;
}

namespace
{
	void PadSlots(TArray<FName>& Slots)
	{
		if (Slots.Num() > FGFHotbarRules::SlotCount)
		{
			Slots.SetNum(FGFHotbarRules::SlotCount);
		}
		while (Slots.Num() < FGFHotbarRules::SlotCount)
		{
			Slots.Add(NAME_None);
		}
	}

	bool InBar(int32 Slot)
	{
		return Slot >= 0 && Slot < FGFHotbarRules::SlotCount;
	}
}

TArray<FName> FGFHotbarRules::Resolve(const FGFHotbarLayout& Layout, const TArray<FName>& Default)
{
	TArray<FName> Out = Layout.bArranged ? Layout.Slots : Default;
	PadSlots(Out);
	return Out;
}

EGFHotbarMoveResult FGFHotbarRules::Apply(TArray<FName>& Slots, const FGFHotbarMove& Move)
{
	PadSlots(Slots);
	if (!InBar(Move.Slot) || (Move.Kind == EGFHotbarMoveKind::Swap && !InBar(Move.Other)))
	{
		return EGFHotbarMoveResult::BadSlot;
	}
	switch (Move.Kind)
	{
	case EGFHotbarMoveKind::Assign:
	{
		if (Slots[Move.Slot] == Move.Id)
		{
			return EGFHotbarMoveResult::NoChange;
		}
		if (!Move.Id.IsNone())
		{
			// One slot per id: an item already on the bar trades places with whatever was in the target.
			for (int32 Index = 0; Index < SlotCount; ++Index)
			{
				if (Index != Move.Slot && Slots[Index] == Move.Id)
				{
					Slots[Index] = Slots[Move.Slot];
				}
			}
		}
		Slots[Move.Slot] = Move.Id;
		return EGFHotbarMoveResult::Ok;
	}
	case EGFHotbarMoveKind::Swap:
		if (Slots[Move.Slot] == Slots[Move.Other])
		{
			return EGFHotbarMoveResult::NoChange;
		}
		Slots.Swap(Move.Slot, Move.Other);
		return EGFHotbarMoveResult::Ok;
	case EGFHotbarMoveKind::Clear:
		if (Slots[Move.Slot].IsNone())
		{
			return EGFHotbarMoveResult::NoChange;
		}
		Slots[Move.Slot] = NAME_None;
		return EGFHotbarMoveResult::Ok;
	default:
		return EGFHotbarMoveResult::BadSlot;
	}
}

bool FGFHotbarRules::ClearSpent(TArray<FName>& Slots, FName SpentItem, TFunctionRef<FName(FName)> ItemFor, TFunctionRef<bool(FName)> Keep)
{
	if (SpentItem.IsNone())
	{
		return false;
	}
	bool bChanged = false;
	for (FName& Slot : Slots)
	{
		if (Slot.IsNone() || Slot == FistsId() || Keep(Slot))
		{
			continue;
		}
		if (Slot == SpentItem || ItemFor(Slot) == SpentItem)
		{
			Slot = NAME_None;
			bChanged = true;
		}
	}
	return bChanged;
}

EGFHotbarMoveResult FGFHotbarRules::CheckId(FName Id, bool bKnown, bool bPlaceable, int64 Owned, bool bFreeBuild)
{
	if (Id.IsNone() || Id == FistsId())
	{
		return EGFHotbarMoveResult::Ok;
	}
	if (!bKnown)
	{
		return EGFHotbarMoveResult::UnknownItem;
	}
	if (Owned > 0 || (bFreeBuild && bPlaceable))
	{
		return EGFHotbarMoveResult::Ok;
	}
	return EGFHotbarMoveResult::NotOwned;
}
