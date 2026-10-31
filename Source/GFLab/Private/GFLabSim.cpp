#include "GFLabTypes.h"
#include "GFDataTables.h"
#include "GFEconomy.h"

const FName FGFLabRules::Station(TEXT("LabBench"));

namespace
{
	// Separate from the pile dealer's stream (that one is seeded with the raw shift seed).
	static constexpr int32 AnomalyStreamSalt = 0x414E4F4D; // 'ANOM'

	static bool IsLive(const FGFLabAnomaly& A, float Clock)
	{
		return !A.bContained
			&& A.StartTime >= FGFLabRules::AnomalyEarliest
			&& A.StartTime < FGFLabRules::LockdownSeconds
			&& Clock >= A.StartTime
			&& Clock < FGFLabRules::LockdownSeconds;
	}

	static float JamScale(const FGFLabRun& Run, float Time)
	{
		if (Time >= FGFLabRules::LockdownSeconds)
		{
			return 1.f;
		}
		float Scale = 1.f;
		const FGFLabData& D = FGFLabData::Get();
		for (const FGFLabAnomaly& A : Run.Anomalies)
		{
			if (A.bContained || A.StartTime > Time || A.StartTime >= FGFLabRules::LockdownSeconds)
			{
				continue;
			}
			const FGFLabAnomalyRow* Row = D.Anomaly(A.Kind);
			if (Row && Row->Effect == EGFLabAnomalyEffect::JamSearch)
			{
				Scale = FMath::Max(Scale, FMath::Max(1.f, Row->SearchTimeScale));
			}
		}
		return Scale;
	}

	static int32 PickHiddenPile(const FGFLabRun& Run)
	{
		for (int32 i = 0; i < Run.Piles.Num(); ++i)
		{
			if (!Run.Piles[i].bSearched && !Run.Piles[i].bHidden && i != Run.Searching)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	static void OnActivate(FGFLabRun& Run, FGFLabAnomaly& A)
	{
		const FGFLabAnomalyRow* Row = FGFLabData::Get().Anomaly(A.Kind);
		if (!Row || Row->Effect != EGFLabAnomalyEffect::HidePile)
		{
			return;
		}
		if (A.TargetPile == INDEX_NONE)
		{
			A.TargetPile = PickHiddenPile(Run);
		}
		if (Run.Piles.IsValidIndex(A.TargetPile) && !Run.Piles[A.TargetPile].bSearched)
		{
			Run.Piles[A.TargetPile].bHidden = true;
		}
	}

	static void OnDeactivate(FGFLabRun& Run, FGFLabAnomaly& A)
	{
		if (Run.Piles.IsValidIndex(A.TargetPile))
		{
			Run.Piles[A.TargetPile].bHidden = false;
		}
	}

	static void RefreshAnomalies(FGFLabRun& Run)
	{
		for (FGFLabAnomaly& A : Run.Anomalies)
		{
			const bool bLive = IsLive(A, Run.Clock);
			if (bLive && !A.bActive)
			{
				A.bActive = true;
				OnActivate(Run, A);
			}
			else if (!bLive && A.bActive)
			{
				A.bActive = false;
				OnDeactivate(Run, A);
			}
		}
	}

	static void EatOne(FGFLabRun& Run)
	{
		for (int32 i = 0; i < Run.Unbanked.Num(); ++i)
		{
			if (Run.Unbanked[i].Count <= 0)
			{
				continue;
			}
			Run.Unbanked[i].Count -= 1;
			if (Run.Unbanked[i].Count <= 0)
			{
				Run.Unbanked.RemoveAt(i);
			}
			return;
		}
	}

	static void ApplyEats(FGFLabRun& Run, float Prev, float Clock)
	{
		const FGFLabData& D = FGFLabData::Get();
		for (int32 Index = 0; Index < Run.Anomalies.Num(); ++Index)
		{
			const FGFLabAnomaly& A = Run.Anomalies[Index];
			if (A.bContained || A.StartTime >= FGFLabRules::LockdownSeconds)
			{
				continue;
			}
			const FGFLabAnomalyRow* Row = D.Anomaly(A.Kind);
			if (!Row || Row->Effect != EGFLabAnomalyEffect::EatUnbanked || Row->EatInterval <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			// A containment that finishes this tick stops the bites at that instant (the seal wins a tie).
			float Cut = FGFLabRules::LockdownSeconds;
			if (Run.Containing == Index && Run.ContainLeft <= (Clock - Prev))
			{
				Cut = FMath::Min(Cut, Prev + FMath::Max(0.f, Run.ContainLeft));
			}
			const float Step = Row->EatInterval;
			for (int32 K = 1; K < 10000; ++K)
			{
				const float T = A.StartTime + K * Step;
				if (T >= Cut || T > Clock)
				{
					break;
				}
				if (T > Prev)
				{
					EatOne(Run);
				}
			}
		}
	}

	static float WorkDrained(const FGFLabRun& Run, float Prev, float Clock)
	{
		TArray<float, TInlineAllocator<8>> Cuts;
		Cuts.Add(Prev);
		Cuts.Add(Clock);
		for (const FGFLabAnomaly& A : Run.Anomalies)
		{
			if (!A.bContained && A.StartTime > Prev && A.StartTime < Clock && A.StartTime < FGFLabRules::LockdownSeconds)
			{
				Cuts.Add(A.StartTime);
			}
		}
		if (FGFLabRules::LockdownSeconds > Prev && FGFLabRules::LockdownSeconds < Clock)
		{
			Cuts.Add(FGFLabRules::LockdownSeconds);
		}
		Cuts.Sort();
		float Work = 0.f;
		for (int32 i = 0; i + 1 < Cuts.Num(); ++i)
		{
			const float A = Cuts[i];
			const float B = Cuts[i + 1];
			if (B - A < KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Work += (B - A) / JamScale(Run, 0.5f * (A + B));
		}
		return Work;
	}

	static void FinishContain(FGFLabRun& Run)
	{
		const int32 Index = Run.Containing;
		Run.Containing = INDEX_NONE;
		Run.ContainLeft = 0.f;
		if (!Run.Anomalies.IsValidIndex(Index))
		{
			return;
		}
		FGFLabAnomaly& A = Run.Anomalies[Index];
		if (A.bContained)
		{
			return;
		}
		A.bContained = true;
		A.bActive = false;
		if (Run.Piles.IsValidIndex(A.TargetPile))
		{
			Run.Piles[A.TargetPile].bHidden = false;
		}
		if (const FGFLabAnomalyRow* Row = FGFLabData::Get().Anomaly(A.Kind))
		{
			// One stream per roll, shared with searches, so the order of actions is the only thing that matters.
			FRandomStream Rng(HashCombine(GetTypeHash(Run.Seed), GetTypeHash(++Run.Rolls)));
			FGFRewards::Roll(Row->Loot, Rng, FGFLabRules::IsOvertime(Run.Clock) ? FGFLabRules::OvertimeMultiplier : 1.f, Run.Unbanked);
		}
	}

	static float LootValue(const TArray<FGFRewardEntry>& Loot, const UDataTable* Items, float Multiplier)
	{
		float V = 0.f;
		for (const FGFRewardEntry& E : Loot)
		{
			V += E.Chance * FMath::Max(1.f, 0.5f * (E.Min + E.Max) * Multiplier) * FGFEconomy::UnitValue(Items, E.ItemId);
		}
		return V;
	}

	static FName PickAnomalyKind(FRandomStream& Rng, const FGFLabData& D)
	{
		int32 Total = 0;
		for (const FName K : D.AnomalyKinds)
		{
			Total += FMath::Max(0, D.Anomalies[K].Weight);
		}
		if (Total <= 0)
		{
			return NAME_None;
		}
		int32 Pick = Rng.RandRange(0, Total - 1);
		for (const FName K : D.AnomalyKinds)
		{
			Pick -= FMath::Max(0, D.Anomalies[K].Weight);
			if (Pick < 0)
			{
				return K;
			}
		}
		return D.AnomalyKinds.Last();
	}
}

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
		if (const UDataTable* T = FGFDataTables::Get<FGFLabAnomalyRow>(TEXT("DT_LabAnomalies")))
		{
			T->ForeachRow<FGFLabAnomalyRow>(TEXT("Lab"), [&](const FName& Name, const FGFLabAnomalyRow& Row)
			{
				Data.Anomalies.Add(Name, Row);
				Data.AnomalyKinds.Add(Name);
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

TArray<FGFLabAnomaly> FGFLabRules::ScheduleAnomalies(int32 Seed)
{
	TArray<FGFLabAnomaly> Out;
	const FGFLabData& D = FGFLabData::Get();
	if (D.AnomalyKinds.Num() == 0)
	{
		return Out;
	}
	// Leave enough clock after the last wake that containing it immediately finishes before the doors seal.
	float WindowEnd = LockdownSeconds;
	for (const FName K : D.AnomalyKinds)
	{
		WindowEnd = FMath::Min(WindowEnd, LockdownSeconds - FMath::Max(0.f, D.Anomalies[K].ContainSeconds));
	}
	if (WindowEnd <= AnomalyEarliest + 1.f)
	{
		WindowEnd = LockdownSeconds;
	}
	TArray<int32> Slots;
	for (int32 T = FMath::CeilToInt(AnomalyEarliest); T < FMath::FloorToInt(WindowEnd); ++T)
	{
		Slots.Add(T);
	}
	if (Slots.Num() == 0)
	{
		return Out;
	}
	FRandomStream Rng(Seed ^ AnomalyStreamSalt);
	const int32 Count = FMath::Min(Rng.RandRange(AnomalyMinCount, AnomalyMaxCount), Slots.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 J = Rng.RandRange(i, Slots.Num() - 1);
		Swap(Slots[i], Slots[J]);
		FGFLabAnomaly& A = Out.AddDefaulted_GetRef();
		A.StartTime = static_cast<float>(Slots[i]);
		A.Kind = PickAnomalyKind(Rng, D);
	}
	Out.Sort([](const FGFLabAnomaly& A, const FGFLabAnomaly& B) { return A.StartTime < B.StartTime; });
	return Out;
}

FGFLabRun FGFLabRules::StartRun(const FString& Key, int32 Seed)
{
	FGFLabRun Run;
	Run.Key = Key;
	Run.Seed = Seed;
	Run.Piles = DealPiles(Seed);
	Run.Anomalies = ScheduleAnomalies(Seed);
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
	if (Run.Piles[Index].bHidden)
	{
		return EGFLabOrder::Hidden;
	}
	if (Run.Searching != INDEX_NONE || Run.Containing != INDEX_NONE)
	{
		return EGFLabOrder::Busy;
	}
	const FGFLabJunkRow* Row = FGFLabData::Get().Row(Run.Piles[Index].Kind);
	Run.Searching = Index;
	Run.SearchLeft = Row ? Row->SearchSeconds : 0.f;
	return EGFLabOrder::Ok;
}

EGFLabOrder FGFLabRules::BeginContain(FGFLabRun& Run, int32 Index)
{
	if (!Run.Anomalies.IsValidIndex(Index))
	{
		return EGFLabOrder::BadAnomaly;
	}
	if (Run.Anomalies[Index].bContained)
	{
		return EGFLabOrder::Contained;
	}
	if (Run.Searching != INDEX_NONE || Run.Containing != INDEX_NONE)
	{
		return EGFLabOrder::Busy;
	}
	if (!Run.Anomalies[Index].bActive)
	{
		return EGFLabOrder::BadAnomaly;
	}
	const FGFLabAnomalyRow* Row = FGFLabData::Get().Anomaly(Run.Anomalies[Index].Kind);
	Run.Containing = Index;
	Run.ContainLeft = Row ? Row->ContainSeconds : 0.f;
	return EGFLabOrder::Ok;
}

bool FGFLabRules::Tick(FGFLabRun& Run, float Dt)
{
	if (Dt < 0.f)
	{
		Dt = 0.f;
	}
	const float Prev = Run.Clock;
	Run.Clock += Dt;
	RefreshAnomalies(Run);
	ApplyEats(Run, Prev, Run.Clock);
	if (Run.Searching != INDEX_NONE)
	{
		Run.SearchLeft -= WorkDrained(Run, Prev, Run.Clock);
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
	if (Run.Containing != INDEX_NONE)
	{
		Run.ContainLeft -= Dt;
		if (Run.ContainLeft <= 0.f)
		{
			FinishContain(Run);
		}
	}
	RefreshAnomalies(Run);
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
	return Row ? LootValue(Row->Loot, Items, Multiplier) : 0.f;
}

float FGFLabRules::ExpectedAnomalyValue(const UDataTable* Items, float Multiplier)
{
	const FGFLabData& D = FGFLabData::Get();
	float WeightSum = 0.f;
	float V = 0.f;
	for (const FName K : D.AnomalyKinds)
	{
		const FGFLabAnomalyRow& Row = D.Anomalies[K];
		const float W = static_cast<float>(FMath::Max(0, Row.Weight));
		WeightSum += W;
		V += W * LootValue(Row.Loot, Items, Multiplier);
	}
	return WeightSum > 0.f ? V / WeightSum : 0.f;
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
	float WeightSum = 0.f, Seconds = 0.f, AnomalyWeight = 0.f, ContainSeconds = 0.f;
	for (const FName K : D.Kinds)
	{
		WeightSum += D.Junk[K].Weight;
		Seconds += D.Junk[K].Weight * D.Junk[K].SearchSeconds;
	}
	for (const FName K : D.AnomalyKinds)
	{
		const float W = static_cast<float>(FMath::Max(0, D.Anomalies[K].Weight));
		AnomalyWeight += W;
		ContainSeconds += W * D.Anomalies[K].ContainSeconds;
	}
	const float PileTime = WeightSum > 0.f ? PileCount * (Seconds / WeightSum + WalkSeconds) : 0.f;
	const float ContainTime = AnomalyWeight > 0.f ? ExpectedAnomaliesPerShift() * (ContainSeconds / AnomalyWeight) : 0.f;
	const float ShiftTime = PileTime + ContainTime;
	const float Value = ExpectedShiftValue(Items) + ExpectedAnomaliesPerShift() * ExpectedAnomalyValue(Items);
	return ShiftTime > 0.f ? Value * 60.f / ShiftTime : 0.f;
}
