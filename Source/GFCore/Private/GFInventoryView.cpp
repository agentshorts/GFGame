#include "GFInventoryView.h"
#include "GFEconomy.h"
#include "GFItemTypes.h"
#include "Engine/DataTable.h"

FString FGFInventoryView::TabName(EGFInventoryTab Tab)
{
	switch (Tab)
	{
	case EGFInventoryTab::Blocks: return TEXT("Blocks");
	case EGFInventoryTab::Materials: return TEXT("Materials");
	case EGFInventoryTab::Crystals: return TEXT("Crystals");
	case EGFInventoryTab::Skins: return TEXT("Skins");
	default: return TEXT("Other");
	}
}

EGFInventoryTab FGFInventoryView::TabForItem(FName ItemId, const FGFItemRow* Row)
{
	if (ItemId.ToString().StartsWith(TEXT("Skin_")) || (Row && Row->Category == EGFItemCategory::Outfit))
	{
		return EGFInventoryTab::Skins;
	}
	if (!Row)
	{
		return EGFInventoryTab::Other;
	}
	if (Row->MaterialCategory == EGFMaterialCategory::Glow)
	{
		return EGFInventoryTab::Crystals;
	}
	if (Row->Category == EGFItemCategory::Material)
	{
		return EGFInventoryTab::Materials;
	}
	return EGFInventoryTab::Other;
}

TArray<FGFInventoryEntry> FGFInventoryView::Build(const FGFInventory& Bag, const TArray<FGFHotbarBlock>& Placeable, const UDataTable* Items, bool bFreeBuild)
{
	TArray<FGFInventoryEntry> Out;
	TSet<FName> BlockIds;
	for (const FGFHotbarBlock& Block : Placeable)
	{
		if (Block.BlockId.IsNone() || BlockIds.Contains(Block.BlockId))
		{
			continue;
		}
		const int64 Count = Bag.GetCount(Block.ItemId);
		if (!bFreeBuild && Count <= 0)
		{
			continue;
		}
		BlockIds.Add(Block.BlockId);
		FGFInventoryEntry& Entry = Out.AddDefaulted_GetRef();
		Entry.Id = Block.BlockId;
		Entry.ItemId = Block.ItemId;
		Entry.Count = bFreeBuild ? -1 : Count;
		Entry.Tab = EGFInventoryTab::Blocks;
		Entry.bPlaceable = true;
	}

	TMap<FName, int32> TableOrder;
	if (Items)
	{
		const TArray<FName> Names = Items->GetRowNames();
		for (int32 Index = 0; Index < Names.Num(); ++Index)
		{
			TableOrder.Add(Names[Index], Index);
		}
	}
	TArray<FGFItemStack> Stacks;
	for (const FGFItemStack& Stack : Bag.Items)
	{
		if (Stack.IsValid())
		{
			Stacks.Add(Stack);
		}
	}
	Stacks.Sort([&TableOrder](const FGFItemStack& A, const FGFItemStack& B)
	{
		const int32* IA = TableOrder.Find(A.ItemId);
		const int32* IB = TableOrder.Find(B.ItemId);
		if (IA && IB)
		{
			return *IA < *IB;
		}
		if (IA || IB)
		{
			return IA != nullptr;
		}
		return A.ItemId.LexicalLess(B.ItemId);
	});
	for (const FGFItemStack& Stack : Stacks)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, Stack.ItemId);
		if (Row && Row->Category == EGFItemCategory::Block && BlockIds.Contains(Stack.ItemId))
		{
			continue;
		}
		FGFInventoryEntry& Entry = Out.AddDefaulted_GetRef();
		Entry.Id = Stack.ItemId;
		Entry.ItemId = Stack.ItemId;
		Entry.Count = Stack.Count;
		Entry.Tab = TabForItem(Stack.ItemId, Row);
	}
	return Out;
}

TArray<FGFInventoryEntry> FGFInventoryView::Filter(const TArray<FGFInventoryEntry>& Entries, EGFInventoryTab Tab)
{
	return Entries.FilterByPredicate([Tab](const FGFInventoryEntry& Entry) { return Entry.Tab == Tab; });
}

int32 FGFInventoryView::PageCount(int32 Count, int32 PerPage)
{
	if (PerPage <= 0 || Count <= 0)
	{
		return 1;
	}
	return (Count + PerPage - 1) / PerPage;
}

int32 FGFInventoryView::ClampPage(int32 Page, int32 Count, int32 PerPage)
{
	return FMath::Clamp(Page, 0, PageCount(Count, PerPage) - 1);
}
