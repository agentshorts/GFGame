#include "GFWager.h"
#include "GFEconomy.h"
#include "GFRecipeTypes.h"
#include "Engine/DataTable.h"
#include "Math/RandomStream.h"

const TCHAR* FGFWagerRules::ToString(EGFWagerCheck R)
{
	switch (R)
	{
	case EGFWagerCheck::Ok: return TEXT("Ok");
	case EGFWagerCheck::WagersOff: return TEXT("WagersOff");
	case EGFWagerCheck::Empty: return TEXT("Empty");
	case EGFWagerCheck::TooManyStacks: return TEXT("TooManyStacks");
	case EGFWagerCheck::InvalidStacks: return TEXT("InvalidStacks");
	case EGFWagerCheck::NotWagerable: return TEXT("NotWagerable");
	case EGFWagerCheck::Insufficient: return TEXT("Insufficient");
	case EGFWagerCheck::OverCap: return TEXT("OverCap");
	case EGFWagerCheck::PinnedProtected: return TEXT("PinnedProtected");
	}
	return TEXT("?");
}

int64 FGFWagerRules::CapValue(const UDataTable* Items, const FGFInventory& Inv)
{
	return static_cast<int64>(FMath::FloorToDouble(FGFEconomy::InventoryValue(Items, Inv) * CapFraction));
}

int32 FGFWagerRules::ProtectedCount(const FGFRecipeRow* Pinned, FName ItemId)
{
	int32 N = 0;
	if (Pinned)
	{
		for (const FGFItemStack& S : Pinned->Inputs)
		{
			N += S.ItemId == ItemId ? S.Count : 0;
		}
	}
	return N;
}

EGFWagerCheck FGFWagerRules::CheckStake(const UDataTable* Items, const FGFInventory& Inv, const TArray<FGFItemStack>& Stake, const FGFRecipeRow* Pinned, bool bWagersOff)
{
	if (bWagersOff)
	{
		return EGFWagerCheck::WagersOff;
	}
	if (Stake.Num() == 0)
	{
		return EGFWagerCheck::Empty;
	}
	TArray<FGFItemStack> Norm;
	if (!FGFEconomy::NormalizeStacks(Stake, Norm) || Norm.Num() != Stake.Num())
	{
		return EGFWagerCheck::InvalidStacks;
	}
	if (Norm.Num() > MaxStakeStacks)
	{
		return EGFWagerCheck::TooManyStacks;
	}
	for (const FGFItemStack& S : Norm)
	{
		if (!FGFEconomy::IsWagerable(Items, S.ItemId))
		{
			return EGFWagerCheck::NotWagerable; // dinos, furniture, tools and key items never
		}
		if (!Inv.Has(S.ItemId, S.Count))
		{
			return EGFWagerCheck::Insufficient;
		}
	}
	if (FGFEconomy::StacksValue(Items, Norm) > CapValue(Items, Inv))
	{
		return EGFWagerCheck::OverCap;
	}
	for (const FGFItemStack& S : Norm)
	{
		const int32 Keep = ProtectedCount(Pinned, S.ItemId);
		if (Keep > 0 && Inv.GetCount(S.ItemId) - S.Count < Keep)
		{
			return EGFWagerCheck::PinnedProtected; // can't stake what the player's pinned recipe still needs
		}
	}
	return EGFWagerCheck::Ok;
}

TArray<FGFItemStack> FGFWagerRules::Suggest(const UDataTable* Items, const FGFInventory& Inv, const FGFRecipeRow* Pinned)
{
	const int64 Target = FMath::Max<int64>(1, static_cast<int64>(FGFEconomy::InventoryValue(Items, Inv) * SuggestFraction));
	const int64 Cap = CapValue(Items, Inv);
	struct FCand { FName Id; int32 Spare; int32 Value; };
	TArray<FCand> Cands;
	for (const FGFItemStack& S : Inv.Items)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, S.ItemId);
		const int32 Spare = S.Count - ProtectedCount(Pinned, S.ItemId);
		if (Row && FGFEconomy::IsWagerable(Items, S.ItemId) && Row->Value > 0 && Spare > 0)
		{
			Cands.Add({ S.ItemId, Spare, Row->Value });
		}
	}
	// Most plentiful (by value) first, so the suggestion uses what the player has lots of.
	Cands.Sort([](const FCand& A, const FCand& B) { return int64(A.Spare) * A.Value != int64(B.Spare) * B.Value ? int64(A.Spare) * A.Value > int64(B.Spare) * B.Value : A.Id.LexicalLess(B.Id); });
	TArray<FGFItemStack> Out;
	int64 Total = 0;
	for (const FCand& C : Cands)
	{
		if (Out.Num() >= MaxStakeStacks || Total >= Target)
		{
			break;
		}
		const int32 Want = FMath::Clamp(static_cast<int32>((Target - Total) / C.Value), 0, C.Spare);
		const int32 Fit = static_cast<int32>(FMath::Min<int64>(Want, (Cap - Total) / C.Value));
		if (Fit > 0)
		{
			Out.Emplace(C.Id, Fit);
			Total += int64(Fit) * C.Value;
		}
	}
	return Out;
}

TArray<FName> FGFWagerRules::Needs(const FGFInventory& Inv, const FGFRecipeRow* Pinned)
{
	TArray<FName> Out;
	if (Pinned)
	{
		for (const FGFItemStack& S : Pinned->Inputs)
		{
			if (!Inv.Has(S.ItemId, S.Count))
			{
				Out.AddUnique(S.ItemId);
			}
		}
		for (const FGFItemStack& S : Pinned->Inputs)
		{
			Out.AddUnique(S.ItemId);
		}
	}
	return Out;
}

namespace GFWagerImpl
{
	struct FStakeInfo
	{
		int64 Value = 0;
		int32 MinTier = 99;
		int32 MaxTier = -1;
		TSet<EGFMaterialCategory> Categories;
	};

	static FStakeInfo Describe(const UDataTable* Items, const TArray<FGFItemStack>& Stake)
	{
		FStakeInfo I;
		I.Value = FGFEconomy::StacksValue(Items, Stake);
		for (const FGFItemStack& S : Stake)
		{
			if (const FGFItemRow* Row = FGFEconomy::FindItem(Items, S.ItemId))
			{
				I.MinTier = FMath::Min(I.MinTier, Row->Tier);
				I.MaxTier = FMath::Max(I.MaxTier, Row->Tier);
				I.Categories.Add(Row->MaterialCategory);
			}
		}
		return I;
	}

	static void ValueWindow(int64 V, int64& Lo, int64& Hi)
	{
		Lo = static_cast<int64>(FMath::CeilToDouble(V * (1.0 - FGFWagerRules::ValueTolerance)));
		Hi = static_cast<int64>(FMath::FloorToDouble(V * (1.0 + FGFWagerRules::ValueTolerance)));
		Lo = FMath::Max<int64>(Lo, 1);
	}
}

bool FGFWagerRules::CheckCounter(const UDataTable* Items, const TArray<FGFItemStack>& Stake, const TArray<FGFItemStack>& Counter, const TArray<FName>& InNeeds, FString* Why)
{
	auto Fail = [Why](const FString& S) { if (Why) { *Why = S; } return false; };
	const GFWagerImpl::FStakeInfo Info = GFWagerImpl::Describe(Items, Stake);
	if (Counter.Num() == 0 || Counter.Num() > MaxCounterStacks)
	{
		return Fail(TEXT("stack count"));
	}
	TArray<FGFItemStack> Norm;
	if (!FGFEconomy::NormalizeStacks(Counter, Norm) || Norm.Num() != Counter.Num())
	{
		return Fail(TEXT("not normalised"));
	}
	const int64 V = FGFEconomy::StacksValue(Items, Counter);
	int64 Lo, Hi;
	GFWagerImpl::ValueWindow(Info.Value, Lo, Hi);
	if (V < Lo || V > Hi)
	{
		return Fail(FString::Printf(TEXT("value %lld outside %lld..%lld"), V, Lo, Hi));
	}
	if (FGFEconomy::ValueBand(V) != FGFEconomy::ValueBand(Info.Value))
	{
		return Fail(TEXT("band"));
	}
	int64 SameCat = 0;
	for (const FGFItemStack& S : Counter)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, S.ItemId);
		if (!Row || !Row->bWagerable || Row->Value <= 0)
		{
			return Fail(FString::Printf(TEXT("%s not wagerable"), *S.ItemId.ToString()));
		}
		if (Row->Exclusive != EGFExclusive::None)
		{
			return Fail(FString::Printf(TEXT("%s is exclusive"), *S.ItemId.ToString()));
		}
		if (Row->Tier < Info.MinTier - 1 || Row->Tier > Info.MaxTier + 1)
		{
			return Fail(FString::Printf(TEXT("%s tier %d outside %d..%d"), *S.ItemId.ToString(), Row->Tier, Info.MinTier - 1, Info.MaxTier + 1));
		}
		const bool bSame = Info.Categories.Contains(Row->MaterialCategory);
		if (bSame)
		{
			SameCat += int64(S.Count) * Row->Value;
		}
		else if (!InNeeds.Contains(S.ItemId))
		{
			return Fail(FString::Printf(TEXT("%s neither same category nor needed"), *S.ItemId.ToString()));
		}
	}
	if (SameCat * 2 < V)
	{
		return Fail(TEXT("under 50% from the player's categories"));
	}
	return true;
}

TArray<FGFItemStack> FGFWagerRules::MatchCounter(const UDataTable* Items, const TArray<FGFItemStack>& Stake, const TArray<FName>& InNeeds, int32 Seed)
{
	TArray<FGFItemStack> Empty;
	const GFWagerImpl::FStakeInfo Info = GFWagerImpl::Describe(Items, Stake);
	if (!Items || Info.Value <= 0)
	{
		return Empty;
	}
	int64 Lo, Hi;
	GFWagerImpl::ValueWindow(Info.Value, Lo, Hi);

	// Candidate pools, sorted by name so the seed alone decides.
	TArray<FName> Same, Need;
	Items->ForeachRow<FGFItemRow>(TEXT("Wager"), [&](const FName& Id, const FGFItemRow& Row)
	{
		if (!Row.bWagerable || Row.Value <= 0 || Row.Exclusive != EGFExclusive::None || Row.Tier < Info.MinTier - 1 || Row.Tier > Info.MaxTier + 1)
		{
			return;
		}
		if (Info.Categories.Contains(Row.MaterialCategory))
		{
			Same.Add(Id);
		}
		else if (InNeeds.Contains(Id))
		{
			Need.Add(Id);
		}
	});
	Same.Sort(FNameLexicalLess());
	Need.Sort(FNameLexicalLess());
	if (Same.Num() == 0)
	{
		return Empty;
	}

	FRandomStream Rng(Seed);
	for (int32 Attempt = 0; Attempt < 400; ++Attempt)
	{
		const int32 NumSame = Rng.RandRange(1, FMath::Min(2, Same.Num()));
		const int32 NumNeed = Need.Num() > 0 ? Rng.RandRange(0, FMath::Min(2, Need.Num())) : 0;
		TArray<FName> Picked;
		while (Picked.Num() < NumSame)
		{
			Picked.AddUnique(Same[Rng.RandHelper(Same.Num())]);
		}
		TArray<FName> PickedNeed;
		while (PickedNeed.Num() < NumNeed)
		{
			PickedNeed.AddUnique(Need[Rng.RandHelper(Need.Num())]);
		}
		const double NeedShare = NumNeed > 0 ? Rng.FRandRange(0.15f, 0.45f) : 0.0;
		const double Target = static_cast<double>(Info.Value);
		TArray<FGFItemStack> Counter;
		auto Fill = [&](const TArray<FName>& Ids, double Budget)
		{
			for (int32 i = 0; i < Ids.Num(); ++i)
			{
				const int32 Unit = FGFEconomy::UnitValue(Items, Ids[i]);
				const double Share = Budget / Ids.Num();
				Counter.Emplace(Ids[i], FMath::Max(1, FMath::RoundToInt(Share / Unit)));
			}
		};
		Fill(Picked, Target * (1.0 - NeedShare));
		Fill(PickedNeed, Target * NeedShare);
		// Nudge the cheapest same-category stack toward the stake's value.
		int32 Cheapest = 0;
		for (int32 i = 1; i < Picked.Num(); ++i)
		{
			if (FGFEconomy::UnitValue(Items, Counter[i].ItemId) < FGFEconomy::UnitValue(Items, Counter[Cheapest].ItemId))
			{
				Cheapest = i;
			}
		}
		const int32 CheapUnit = FGFEconomy::UnitValue(Items, Counter[Cheapest].ItemId);
		const int64 Now = FGFEconomy::StacksValue(Items, Counter);
		const int64 Delta = Info.Value - Now;
		Counter[Cheapest].Count = FMath::Max(1, Counter[Cheapest].Count + static_cast<int32>(FMath::RoundToDouble(double(Delta) / CheapUnit)));
		if (CheckCounter(Items, Stake, Counter, InNeeds))
		{
			return Counter;
		}
	}
	// Fallback: mirror the player's stake (same items are always same-category), minus anything exclusive.
	TArray<FGFItemStack> Mirror;
	for (const FGFItemStack& S : Stake)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, S.ItemId);
		if (Row && Row->Exclusive == EGFExclusive::None)
		{
			Mirror.Add(S);
		}
	}
	if (Mirror.Num() > 0 && CheckCounter(Items, Stake, Mirror, InNeeds))
	{
		return Mirror;
	}
	return Empty;
}
