#include "GFRewards.h"
#include "GFEconomy.h"

void FGFRewards::AddTo(TArray<FGFItemStack>& Stacks, FName ItemId, int32 Count)
{
	if (ItemId.IsNone() || Count <= 0)
	{
		return;
	}
	for (FGFItemStack& S : Stacks)
	{
		if (S.ItemId == ItemId)
		{
			S.Count += Count;
			return;
		}
	}
	Stacks.Emplace(ItemId, Count);
}

void FGFRewards::AddAll(TArray<FGFItemStack>& Stacks, const TArray<FGFItemStack>& More)
{
	for (const FGFItemStack& S : More)
	{
		AddTo(Stacks, S.ItemId, S.Count);
	}
}

int32 FGFRewards::CountOf(const TArray<FGFItemStack>& Stacks, FName ItemId)
{
	int32 N = 0;
	for (const FGFItemStack& S : Stacks)
	{
		N += S.ItemId == ItemId ? S.Count : 0;
	}
	return N;
}

void FGFRewards::Roll(const TArray<FGFRewardEntry>& Entries, FRandomStream& Rng, float Multiplier, TArray<FGFItemStack>& Out)
{
	for (const FGFRewardEntry& E : Entries)
	{
		// Always draw both numbers so one entry's outcome never shifts the next entry's roll.
		const float R = Rng.GetFraction();
		const int32 Amount = Rng.RandRange(FMath::Min(E.Min, E.Max), FMath::Max(E.Min, E.Max));
		if (R < E.Chance && Amount > 0)
		{
			AddTo(Out, E.ItemId, FMath::Max(1, FMath::RoundToInt(Amount * Multiplier)));
		}
	}
}

bool FGFRewards::ContainsExclusive(const UDataTable* Items, const TArray<FGFItemStack>& Stacks, EGFExclusive Which)
{
	for (const FGFItemStack& S : Stacks)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, S.ItemId);
		if (Row && Row->Exclusive == Which)
		{
			return true;
		}
	}
	return false;
}

bool FGFRewards::ContainsExclusive(const UDataTable* Items, const TArray<FGFRewardEntry>& Entries, EGFExclusive Which)
{
	for (const FGFRewardEntry& E : Entries)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, E.ItemId);
		if (Row && Row->Exclusive == Which)
		{
			return true;
		}
	}
	return false;
}
