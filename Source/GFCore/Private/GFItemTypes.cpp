#include "GFItemTypes.h"

int32 FGFInventory::GetCount(FName ItemId) const
{
	for (const FGFItemStack& S : Items)
	{
		if (S.ItemId == ItemId)
		{
			return S.Count;
		}
	}
	return 0;
}

bool FGFInventory::HasAll(const TArray<FGFItemStack>& Stacks) const
{
	// Sum duplicates first so a recipe listing the same item twice is checked correctly.
	TMap<FName, int32> Needed;
	for (const FGFItemStack& S : Stacks)
	{
		Needed.FindOrAdd(S.ItemId) += S.Count;
	}
	for (const TPair<FName, int32>& P : Needed)
	{
		if (!Has(P.Key, P.Value))
		{
			return false;
		}
	}
	return true;
}

void FGFInventory::Add(FName ItemId, int32 Count)
{
	if (ItemId.IsNone() || Count <= 0)
	{
		return;
	}
	for (FGFItemStack& S : Items)
	{
		if (S.ItemId == ItemId)
		{
			S.Count += Count;
			return;
		}
	}
	Items.Emplace(ItemId, Count);
}

bool FGFInventory::Remove(FName ItemId, int32 Count)
{
	if (Count <= 0)
	{
		return true;
	}
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (Items[i].ItemId == ItemId)
		{
			if (Items[i].Count < Count)
			{
				return false;
			}
			Items[i].Count -= Count;
			if (Items[i].Count == 0)
			{
				Items.RemoveAt(i);
			}
			return true;
		}
	}
	return false;
}

bool FGFInventory::RemoveAll(const TArray<FGFItemStack>& Stacks)
{
	if (!HasAll(Stacks))
	{
		return false;
	}
	for (const FGFItemStack& S : Stacks)
	{
		verify(Remove(S.ItemId, S.Count));
	}
	return true;
}

uint32 FGFInventory::GetContentHash() const
{
	TArray<FGFItemStack> Sorted = Items;
	Sorted.Sort([](const FGFItemStack& A, const FGFItemStack& B) { return A.ItemId.LexicalLess(B.ItemId); });
	uint32 Hash = 0;
	for (const FGFItemStack& S : Sorted)
	{
		Hash = HashCombine(Hash, HashCombine(GetTypeHash(S.ItemId.ToString()), GetTypeHash(S.Count)));
	}
	return Hash;
}
