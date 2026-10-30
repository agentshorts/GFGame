#include "GFLabTypes.h"
#include "GFDataTables.h"
#include "GFEconomy.h"

const FName FGFLabRules::Station(TEXT("LabBench"));

const FGFLabData& FGFLabData::Get()
{
	static FGFLabData Data;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		if (const UDataTable* T = FGFDataTables::Get<FGFLabJunkRow>(TEXT("DT_LabJunk")))
		{
			T->ForeachRow<FGFLabJunkRow>(TEXT("Lab"), [&](const FName& Name, const FGFLabJunkRow& Row)
			{
				Data.Junk.Add(Name, Row);
				Data.Kinds.Add(Name);
			});
		}
	}
	return Data;
}

TArray<FGFLabPile> FGFLabRules::DealPiles(int32 Seed)
{
	const FGFLabData& D = FGFLabData::Get();
	TArray<FGFLabPile> Out;
	int32 Total = 0;
	for (const FName K : D.Kinds)
	{
		Total += FMath::Max(0, D.Junk[K].Weight);
	}
	if (Total <= 0)
	{
		return Out;
	}
	FRandomStream Rng(Seed);
	for (int32 i = 0; i < PileCount; ++i)
	{
		int32 Pick = Rng.RandRange(0, Total - 1);
		for (const FName K : D.Kinds)
		{
			Pick -= FMath::Max(0, D.Junk[K].Weight);
			if (Pick < 0)
			{
				Out.AddDefaulted_GetRef().Kind = K;
				break;
			}
		}
	}
	return Out;
}

FGFLabRun FGFLabRules::StartRun(const FString& Key, int32 Seed)
{
	FGFLabRun Run;
	Run.Key = Key;
	Run.Seed = Seed;
	Run.Piles = DealPiles(Seed);
	return Run;
}

EGFLabOrder FGFLabRules::BeginSearch(FGFLabRun& Run, int32 Index)
{
	if (!Run.Piles.IsValidIndex(Index))
	{
		return EGFLabOrder::BadPile;
	}
	if (Run.Piles[Index].bSearched)
	{
		return EGFLabOrder::Searched;
	}
	if (Run.Searching != INDEX_NONE)
	{
		return EGFLabOrder::Busy;
	}
	const FGFLabJunkRow* Row = FGFLabData::Get().Row(Run.Piles[Index].Kind);
	Run.Searching = Index;
	Run.SearchLeft = Row ? Row->SearchSeconds : 0.f;
	return EGFLabOrder::Ok;
}

bool FGFLabRules::Tick(FGFLabRun& Run, float Dt)
{
	Run.Clock += Dt;
	if (Run.Searching != INDEX_NONE)
	{
		Run.SearchLeft -= Dt;
		if (Run.SearchLeft <= 0.f)
		{
			FGFLabPile& P = Run.Piles[Run.Searching];
			P.bSearched = true;
			if (const FGFLabJunkRow* Row = FGFLabData::Get().Row(P.Kind))
			{
				// One stream per roll so results don't depend on tick size.
				FRandomStream Rng(HashCombine(GetTypeHash(Run.Seed), GetTypeHash(++Run.Rolls)));
				FGFRewards::Roll(Row->Loot, Rng, IsOvertime(Run.Clock) ? OvertimeMultiplier : 1.f, Run.Unbanked);
			}
			Run.Searching = INDEX_NONE;
			Run.SearchLeft = 0.f;
		}
	}
	return Run.Clock >= LockdownSeconds;
}

TArray<FGFItemStack> FGFLabRules::Bank(const TArray<FGFItemStack>& Unbanked, EGFLabEnd How)
{
	if (How == EGFLabEnd::Extract)
	{
		return Unbanked;
	}
	TArray<FGFItemStack> Out;
	for (const FGFItemStack& S : Unbanked)
	{
		const int32 Keep = FMath::FloorToInt(S.Count * EvacuateKeep);
		if (Keep > 0)
		{
			Out.Emplace(S.ItemId, Keep);
		}
	}
	return Out;
}

float FGFLabRules::ExpectedSearchValue(FName Kind, const UDataTable* Items, float Multiplier)
{
	const FGFLabJunkRow* Row = FGFLabData::Get().Row(Kind);
	float V = 0.f;
	if (Row)
	{
		for (const FGFRewardEntry& E : Row->Loot)
		{
			V += E.Chance * FMath::Max(1.f, 0.5f * (E.Min + E.Max) * Multiplier) * FGFEconomy::UnitValue(Items, E.ItemId);
		}
	}
	return V;
}

float FGFLabRules::ExpectedShiftValue(const UDataTable* Items)
{
	const FGFLabData& D = FGFLabData::Get();
	float WeightSum = 0.f, V = 0.f;
	for (const FName K : D.Kinds)
	{
		WeightSum += D.Junk[K].Weight;
		V += D.Junk[K].Weight * ExpectedSearchValue(K, Items);
	}
	return WeightSum > 0.f ? PileCount * V / WeightSum : 0.f;
}

float FGFLabRules::ExpectedValuePerMinute(const UDataTable* Items)
{
	const FGFLabData& D = FGFLabData::Get();
	float WeightSum = 0.f, Seconds = 0.f;
	for (const FName K : D.Kinds)
	{
		WeightSum += D.Junk[K].Weight;
		Seconds += D.Junk[K].Weight * D.Junk[K].SearchSeconds;
	}
	const float ShiftTime = WeightSum > 0.f ? PileCount * (Seconds / WeightSum + WalkSeconds) : 0.f;
	return ShiftTime > 0.f ? ExpectedShiftValue(Items) * 60.f / ShiftTime : 0.f;
}
