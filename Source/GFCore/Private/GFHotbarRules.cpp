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
