// Overtime, the after-hours lab: junk piles and shift rules, seeded anomalies and containment drops, Lab Bench
// junk-to-gear recipes, the pacing budget, and the two taming aids it makes (the Lab Treat every species eats,
// the Calming Mist). All pure (no world).
#include "Misc/AutomationTest.h"
#include "GFLabTypes.h"
#include "GFEconomy.h"
#include "GFRewards.h"
#include "GFItemTypes.h"
#include "GFRecipeTypes.h"
#include "GFDataTables.h"
#include "GFCraftingSubsystem.h"
#include "GFDinoTypes.h"
#include "GFMineTypes.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFLabTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFLabTestUtil
{
	static const UDataTable* Items() { return FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items")); }
	static const UDataTable* Recipes() { return FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes")); }
	static const FDinoDietRow* Diet(FName Species)
	{
		const UDataTable* T = FGFDataTables::Get<FDinoDietRow>(TEXT("DT_DinoDiet"));
		return T ? T->FindRow<FDinoDietRow>(Species, TEXT("Test"), false) : nullptr;
	}
	static void Run(FGFLabRun& R, float Seconds, float Step = 0.1f)
	{
		for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Step)
		{
			FGFLabRules::Tick(R, Step);
		}
	}

	/** One anomaly of Kind, already awake, schedule replaced so the other two can't interfere. */
	static FGFLabRun Solo(FName Kind, int32 Seed)
	{
		FGFLabRun R = FGFLabRules::StartRun(TEXT("Solo"), Seed);
		R.Anomalies.Reset();
		FGFLabAnomaly& A = R.Anomalies.AddDefaulted_GetRef();
		A.Kind = Kind;
		A.StartTime = FGFLabRules::AnomalyEarliest;
		R.Clock = FGFLabRules::AnomalyEarliest;
		FGFLabRules::Tick(R, 0.05f);
		return R;
	}

	static bool SameAnomalies(const TArray<FGFLabAnomaly>& A, const TArray<FGFLabAnomaly>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.Num(); ++i)
		{
			if (A[i].Kind != B[i].Kind || A[i].StartTime != B[i].StartTime)
			{
				return false;
			}
		}
		return true;
	}

	static int32 FirstLive(const FGFLabRun& R)
	{
		for (int32 i = 0; i < R.Anomalies.Num(); ++i)
		{
			if (R.Anomalies[i].bActive && !R.Anomalies[i].bContained)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	// Contain whatever is awake, then put Unbanked back. The junk-table check must not count anomaly drops,
	// and a bite during the seal must not delete pile loot either.
	static void ContainAndDiscard(FGFLabRun& R, int32 Index)
	{
		const TArray<FGFItemStack> Kept = R.Unbanked;
		if (FGFLabRules::BeginContain(R, Index) != EGFLabOrder::Ok)
		{
			return;
		}
		int32 Guard = 0;
		while (R.Containing != INDEX_NONE && Guard++ < 40)
		{
			FGFLabRules::Tick(R, 0.5f);
		}
		R.Unbanked = Kept;
	}

	/** Search every pile, sealing anomalies that wake so a jam or a hidden pile can't skip one. */
	static int32 SearchEverything(FGFLabRun& R)
	{
		int32 Next = 0;
		int32 Guard = 0;
		while (Next < R.Piles.Num() && Guard++ < 500)
		{
			if (R.Searching == INDEX_NONE && R.Containing == INDEX_NONE)
			{
				const int32 Live = FirstLive(R);
				if (Live != INDEX_NONE)
				{
					ContainAndDiscard(R, Live);
					continue;
				}
				if (R.Piles[Next].bSearched)
				{
					++Next;
					continue;
				}
				if (!R.Piles[Next].bHidden)
				{
					FGFLabRules::BeginSearch(R, Next);
				}
			}
			FGFLabRules::Tick(R, 0.5f);
			if (R.Piles.IsValidIndex(Next) && R.Piles[Next].bSearched && R.Searching == INDEX_NONE)
			{
				++Next;
			}
		}
		int32 Missed = 0;
		for (const FGFLabPile& P : R.Piles)
		{
			if (!P.bSearched)
			{
				++Missed;
			}
		}
		return Missed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLabJunkTest, "GFGame.Lab.Junk", GFLabTestFlags)
bool FGFLabJunkTest::RunTest(const FString&)
{
	using namespace GFLabTestUtil;
	const FGFLabData& D = FGFLabData::Get();
	TestEqual(TEXT("Four kinds of junk pile"), D.Kinds.Num(), 4);
	const UDataTable* ItemsT = Items();
	TSet<FName> LabDrops;
	for (const FName K : D.Kinds)
	{
		const FGFLabJunkRow& Row = D.Junk[K];
		TestTrue(*FString::Printf(TEXT("%s takes a few seconds"), *K.ToString()), Row.SearchSeconds >= 2.f && Row.SearchSeconds <= 10.f);
		for (const FGFRewardEntry& E : Row.Loot)
		{
			const FGFItemRow* I = FGFEconomy::FindItem(ItemsT, E.ItemId);
			TestTrue(*FString::Printf(TEXT("%s drops a real tradeable item (%s)"), *K.ToString(), *E.ItemId.ToString()), I && I->Tier > 0);
			LabDrops.Add(E.ItemId);
		}
	}
	// Every Lab exclusive drops here, and nowhere in the mine.
	ItemsT->ForeachRow<FGFItemRow>(TEXT("Test"), [&](const FName& Id, const FGFItemRow& Row)
	{
		if (Row.Exclusive == EGFExclusive::Lab)
		{
			TestTrue(*FString::Printf(TEXT("%s drops in the lab"), *Id.ToString()), LabDrops.Contains(Id));
		}
	});
	for (const TPair<FName, FGFMineRoomRow>& R : FGFMineData::Get().Rooms)
	{
		TestFalse(TEXT("No Lab exclusives in the mine"), FGFRewards::ContainsExclusive(ItemsT, R.Value.Yields, EGFExclusive::Lab));
	}
	const TArray<FGFLabPile> A = FGFLabRules::DealPiles(99), B = FGFLabRules::DealPiles(99);
	TestEqual(TEXT("8 piles"), A.Num(), FGFLabRules::PileCount);
	bool bSame = A.Num() == B.Num();
	for (int32 i = 0; bSame && i < A.Num(); ++i)
	{
		bSame = A[i].Kind == B[i].Kind;
	}
	TestTrue(TEXT("Same seed, same lab"), bSame);
	// Weighted: over many shifts filing cabinets are the most common and specimen tanks the rarest.
	TMap<FName, int32> Seen;
	for (int32 S = 1; S <= 400; ++S)
	{
		for (const FGFLabPile& P : FGFLabRules::DealPiles(S))
		{
			++Seen.FindOrAdd(P.Kind);
		}
	}
	TestTrue(TEXT("Cabinets common, tanks rare"), Seen.FindRef(TEXT("FilingCabinet")) > Seen.FindRef(TEXT("SpecimenTank")) * 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLabShiftTest, "GFGame.Lab.Shift", GFLabTestFlags)
bool FGFLabShiftTest::RunTest(const FString&)
{
	using namespace GFLabTestUtil;
	FGFLabRun R = FGFLabRules::StartRun(TEXT("Owner"), 7);
	TestTrue(TEXT("Bad pile"), FGFLabRules::BeginSearch(R, 8) == EGFLabOrder::BadPile);
	TestTrue(TEXT("Search pile 0"), FGFLabRules::BeginSearch(R, 0) == EGFLabOrder::Ok);
	TestTrue(TEXT("One at a time"), FGFLabRules::BeginSearch(R, 1) == EGFLabOrder::Busy);
	const float Secs = FGFLabData::Get().Row(R.Piles[0].Kind)->SearchSeconds;
	Run(R, Secs - 0.5f);
	TestFalse(TEXT("Still searching"), R.Piles[0].bSearched);
	Run(R, 0.6f);
	TestTrue(TEXT("Searched"), R.Piles[0].bSearched && R.Searching == INDEX_NONE);
	TestTrue(TEXT("Can't search it twice"), FGFLabRules::BeginSearch(R, 0) == EGFLabOrder::Searched);

	// Same pile and roll, before and in overtime: overtime gives at least as much of everything (1.5x).
	int32 Normal = 0, Over = 0;
	for (int32 Seed = 1; Seed <= 200; ++Seed)
	{
		FGFLabRun X = FGFLabRules::StartRun(TEXT("A"), Seed), Y = X;
		Y.Clock = FGFLabRules::ShiftSeconds;
		FGFLabRules::BeginSearch(X, 0);
		FGFLabRules::BeginSearch(Y, 0);
		// 13 s covers the slowest pile even if a printer swarm is jamming it (8 s * 1.5).
		Run(X, 13.f, 0.5f);
		Run(Y, 13.f, 0.5f);
		for (const FGFItemStack& S : X.Unbanked) { Normal += S.Count; }
		for (const FGFItemStack& S : Y.Unbanked) { Over += S.Count; }
	}
	AddInfo(FString::Printf(TEXT("200 searches: %d items in the shift, %d in overtime"), Normal, Over));
	TestTrue(TEXT("Overtime pays more"), Over > Normal * 1.3f);

	FGFLabRun L = FGFLabRules::StartRun(TEXT("B"), 3);
	TestFalse(TEXT("No lockdown during the shift"), FGFLabRules::Tick(L, FGFLabRules::ShiftSeconds));
	TestTrue(TEXT("Overtime"), FGFLabRules::IsOvertime(L.Clock));
	TestTrue(TEXT("Doors seal at lockdown"), FGFLabRules::Tick(L, FGFLabRules::LockdownSeconds - FGFLabRules::ShiftSeconds));

	const TArray<FGFItemStack> Found = { FGFItemStack(TEXT("ScrapWire"), 4), FGFItemStack(TEXT("PolymerGel"), 1), FGFItemStack(TEXT("CircuitInk"), 7) };
	const TArray<FGFItemStack> Out = FGFLabRules::Bank(Found, EGFLabEnd::Extract);
	TestEqual(TEXT("Extract keeps everything"), FGFRewards::CountOf(Out, TEXT("CircuitInk")), 7);
	const TArray<FGFItemStack> Evac = FGFLabRules::Bank(Found, EGFLabEnd::Evacuate);
	TestEqual(TEXT("Evacuate: 4 -> 3"), FGFRewards::CountOf(Evac, TEXT("ScrapWire")), 3);
	TestEqual(TEXT("Evacuate: 1 -> 0"), FGFRewards::CountOf(Evac, TEXT("PolymerGel")), 0);
	TestEqual(TEXT("Evacuate: 7 -> 5"), FGFRewards::CountOf(Evac, TEXT("CircuitInk")), 5);
	TestEqual(TEXT("Lockdown same as evacuating"), FGFRewards::CountOf(FGFLabRules::Bank(Found, EGFLabEnd::Lockdown), TEXT("CircuitInk")), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLabAnomalyTest, "GFGame.Lab.Anomalies", GFLabTestFlags)
bool FGFLabAnomalyTest::RunTest(const FString&)
{
	using namespace GFLabTestUtil;
	const FGFLabData& D = FGFLabData::Get();
	TestEqual(TEXT("Three placeholder anomalies"), D.AnomalyKinds.Num(), 3);
	const UDataTable* ItemsT = Items();
	for (const TCHAR* Kind : { TEXT("RoguePrinterSwarm"), TEXT("ChairEatingFloor"), TEXT("FlickeringServerRoom") })
	{
		const FGFLabAnomalyRow* Row = D.Anomaly(Kind);
		TestNotNull(*FString::Printf(TEXT("%s row"), Kind), Row);
		if (!Row)
		{
			continue;
		}
		bool bShard = false;
		bool bOther = false;
		for (const FGFRewardEntry& E : Row->Loot)
		{
			bShard |= E.ItemId == TEXT("AnomalyShard");
			bOther |= E.ItemId != TEXT("AnomalyShard");
			const FGFItemRow* Item = FGFEconomy::FindItem(ItemsT, E.ItemId);
			TestTrue(*FString::Printf(TEXT("%s drops a real item (%s)"), Kind, *E.ItemId.ToString()), Item && Item->Tier > 0);
		}
		TestTrue(*FString::Printf(TEXT("%s drops Anomaly Shard"), Kind), bShard);
		TestTrue(*FString::Printf(TEXT("%s also drops rare junk"), Kind), bOther);
	}
	const FGFItemRow* Shard = FGFEconomy::FindItem(ItemsT, TEXT("AnomalyShard"));
	TestTrue(TEXT("Anomaly Shard stays lab-exclusive"), Shard && Shard->Exclusive == EGFExclusive::Lab);

	// Schedule: same seed, same list. 1 or 2. Nothing before 30 s, nothing at or after lockdown.
	bool bSchedulesDiffer = false;
	const TArray<FGFLabAnomaly> First = FGFLabRules::ScheduleAnomalies(1);
	float LatestContain = 0.f;
	for (const FName K : D.AnomalyKinds)
	{
		LatestContain = FMath::Max(LatestContain, D.Anomalies[K].ContainSeconds);
	}
	for (int32 Seed = 1; Seed <= 400; ++Seed)
	{
		const TArray<FGFLabAnomaly> A = FGFLabRules::ScheduleAnomalies(Seed);
		const TArray<FGFLabAnomaly> B = FGFLabRules::ScheduleAnomalies(Seed);
		TestTrue(*FString::Printf(TEXT("Seed %d is deterministic"), Seed), SameAnomalies(A, B));
		TestTrue(*FString::Printf(TEXT("Seed %d deals 1 or 2"), Seed), A.Num() >= FGFLabRules::AnomalyMinCount && A.Num() <= FGFLabRules::AnomalyMaxCount);
		float PrevTime = -1.f;
		for (const FGFLabAnomaly& An : A)
		{
			TestTrue(*FString::Printf(TEXT("Seed %d starts at %.0f, in [30, lockdown)"), Seed, An.StartTime),
				An.StartTime >= FGFLabRules::AnomalyEarliest && An.StartTime < FGFLabRules::LockdownSeconds);
			TestTrue(TEXT("Chronological"), An.StartTime > PrevTime);
			TestNotNull(*FString::Printf(TEXT("Kind %s"), *An.Kind.ToString()), D.Anomaly(An.Kind));
			// Waking it and sealing it at once still finishes before the doors.
			TestTrue(TEXT("Containable before lockdown"), An.StartTime + LatestContain < FGFLabRules::LockdownSeconds);
			PrevTime = An.StartTime;
		}
		if (Seed > 1 && !SameAnomalies(A, First))
		{
			bSchedulesDiffer = true;
		}
	}
	TestTrue(TEXT("The schedule actually depends on the seed"), bSchedulesDiffer);

	FGFLabRun Sealed = FGFLabRules::StartRun(TEXT("Seal"), 11);
	TestFalse(TEXT("Still open a second before lockdown"), FGFLabRules::Tick(Sealed, FGFLabRules::LockdownSeconds - 1.f));
	TestTrue(TEXT("Doors seal"), FGFLabRules::Tick(Sealed, 1.f));
	for (const FGFLabAnomaly& An : Sealed.Anomalies)
	{
		TestFalse(TEXT("Nothing stays live through lockdown"), An.bActive);
	}

	// Rogue printer: +50 % search time while it's live, normal again once it's contained.
	{
		const FGFLabAnomalyRow* Swarm = D.Anomaly(TEXT("RoguePrinterSwarm"));
		FGFLabRun R = Solo(TEXT("RoguePrinterSwarm"), 4);
		TestTrue(TEXT("Swarm is live"), R.Anomalies[0].bActive);
		const float Scale = Swarm ? Swarm->SearchTimeScale : 1.5f;
		TestTrue(TEXT("Swarm is +50 %"), FMath::IsNearlyEqual(Scale, 1.5f));
		TestTrue(TEXT("Search under the swarm"), FGFLabRules::BeginSearch(R, 0) == EGFLabOrder::Ok);
		TestTrue(TEXT("Can't contain while searching"), FGFLabRules::BeginContain(R, 0) == EGFLabOrder::Busy);
		const float Search = D.Row(R.Piles[0].Kind)->SearchSeconds;
		Run(R, Search * Scale - 0.4f);
		TestFalse(TEXT("Jam is still on the pile"), R.Piles[0].bSearched);
		Run(R, 0.6f);
		TestTrue(TEXT("Jammed search finishes at 1.5x"), R.Piles[0].bSearched && R.Searching == INDEX_NONE);

		const float Contain = Swarm ? Swarm->ContainSeconds : 4.f;
		TestTrue(TEXT("Contain the swarm"), FGFLabRules::BeginContain(R, 0) == EGFLabOrder::Ok);
		TestTrue(TEXT("One order at a time"), FGFLabRules::BeginSearch(R, 1) == EGFLabOrder::Busy);
		Run(R, Contain + 0.2f);
		TestTrue(TEXT("Swarm contained"), R.Anomalies[0].bContained && !R.Anomalies[0].bActive && R.Containing == INDEX_NONE);
		TestTrue(TEXT("Already contained"), FGFLabRules::BeginContain(R, 0) == EGFLabOrder::Contained);

		const float Search2 = D.Row(R.Piles[1].Kind)->SearchSeconds;
		TestTrue(TEXT("Search after the jam"), FGFLabRules::BeginSearch(R, 1) == EGFLabOrder::Ok);
		// A hair over the normal time. Still short of 1.5x, so a jam that didn't stop fails this.
		Run(R, Search2 + 0.2f);
		TestTrue(TEXT("Jam stopped when the swarm was contained"), R.Piles[1].bSearched);
	}

	// Chair-eating floor: one unbanked item every EatInterval, and it stops when contained.
	{
		const FGFLabAnomalyRow* Floor = D.Anomaly(TEXT("ChairEatingFloor"));
		const float Interval = Floor ? Floor->EatInterval : 15.f;
		TestTrue(TEXT("Floor has an interval"), Interval >= 5.f);
		FGFLabRun R = Solo(TEXT("ChairEatingFloor"), 6);
		R.Unbanked = { FGFItemStack(TEXT("ScrapWire"), 5) };
		Run(R, Interval);
		TestEqual(TEXT("One bite"), FGFRewards::CountOf(R.Unbanked, TEXT("ScrapWire")), 4);
		Run(R, Interval);
		TestEqual(TEXT("Second bite"), FGFRewards::CountOf(R.Unbanked, TEXT("ScrapWire")), 3);
		const float Contain = Floor ? Floor->ContainSeconds : 5.f;
		TestTrue(TEXT("Contain the floor"), FGFLabRules::BeginContain(R, 0) == EGFLabOrder::Ok);
		Run(R, Contain + 0.2f);
		const int32 Left = FGFRewards::CountOf(R.Unbanked, TEXT("ScrapWire"));
		Run(R, Interval * 2.f);
		TestEqual(TEXT("Floor stops eating once contained"), FGFRewards::CountOf(R.Unbanked, TEXT("ScrapWire")), Left);

		FGFLabRun Last = Solo(TEXT("ChairEatingFloor"), 8);
		Last.Unbanked = { FGFItemStack(TEXT("BentPipe"), 1) };
		Run(Last, Interval);
		TestEqual(TEXT("The last item is gone, not a zero stack"), FGFRewards::CountOf(Last.Unbanked, TEXT("BentPipe")), 0);
		TestEqual(TEXT("Empty unbanked"), Last.Unbanked.Num(), 0);
	}

	// Flickering server room: one unsearched pile hides, and comes back when contained.
	{
		const FGFLabAnomalyRow* Room = D.Anomaly(TEXT("FlickeringServerRoom"));
		FGFLabRun R = Solo(TEXT("FlickeringServerRoom"), 9);
		TestTrue(TEXT("A pile is hidden"), R.Piles.IsValidIndex(0) && R.Piles[0].bHidden && R.Anomalies[0].TargetPile == 0);
		TestTrue(TEXT("Can't search a hidden pile"), FGFLabRules::BeginSearch(R, 0) == EGFLabOrder::Hidden);
		const float Contain = Room ? Room->ContainSeconds : 4.f;
		TestTrue(TEXT("Contain the room"), FGFLabRules::BeginContain(R, 0) == EGFLabOrder::Ok);
		Run(R, Contain + 0.2f);
		TestTrue(TEXT("Room contained"), R.Anomalies[0].bContained && !R.Anomalies[0].bActive);
		TestFalse(TEXT("Pile is back"), R.Piles[0].bHidden);
		TestTrue(TEXT("Search works again"), FGFLabRules::BeginSearch(R, 0) == EGFLabOrder::Ok);
	}

	// Bad index, and a scheduled anomaly that hasn't woken yet.
	{
		FGFLabRun R = FGFLabRules::StartRun(TEXT("Early"), 3);
		TestTrue(TEXT("Bad anomaly index"), FGFLabRules::BeginContain(R, INDEX_NONE) == EGFLabOrder::BadAnomaly);
		TestTrue(TEXT("No such anomaly"), FGFLabRules::BeginContain(R, 9) == EGFLabOrder::BadAnomaly);
		R.Anomalies.Reset();
		FGFLabAnomaly& A = R.Anomalies.AddDefaulted_GetRef();
		A.Kind = TEXT("RoguePrinterSwarm");
		A.StartTime = 80.f;
		R.Clock = 0.f;
		TestTrue(TEXT("Not awake yet"), FGFLabRules::BeginContain(R, 0) == EGFLabOrder::BadAnomaly);
	}

	// Containment rolls the row's table. Anomaly Shard shows up at that chance. Overtime pays 1.5x.
	// Same seed, same drop. Evacuate keeps floor(75 %) of whatever came out.
	constexpr int32 RateSeeds = 600;
	int32 NormalItems = 0;
	int32 OvertimeItems = 0;
	for (const FName Kind : D.AnomalyKinds)
	{
		const FGFLabAnomalyRow* Row = D.Anomaly(Kind);
		float Chance = 0.f;
		if (Row)
		{
			for (const FGFRewardEntry& E : Row->Loot)
			{
				if (E.ItemId == TEXT("AnomalyShard"))
				{
					Chance = E.Chance;
				}
			}
		}
		int32 Hits = 0;
		int32 ContainFails = 0;
		const float ContainFor = (Row ? Row->ContainSeconds : 4.f) + 0.25f;
		for (int32 Seed = 1; Seed <= RateSeeds; ++Seed)
		{
			FGFLabRun R = Solo(Kind, Seed);
			if (FGFLabRules::BeginContain(R, 0) != EGFLabOrder::Ok)
			{
				++ContainFails;
				continue;
			}
			Run(R, ContainFor);
			if (FGFRewards::CountOf(R.Unbanked, TEXT("AnomalyShard")) > 0)
			{
				++Hits;
			}
			for (const FGFItemStack& S : R.Unbanked)
			{
				NormalItems += S.Count;
			}
			if (Seed == 1 || Seed == 50 || Seed == 400)
			{
				FGFLabRun Again = Solo(Kind, Seed);
				const bool bContained = FGFLabRules::BeginContain(Again, 0) == EGFLabOrder::Ok;
				Run(Again, ContainFor);
				bool bSameDrop = bContained && Again.Unbanked.Num() == R.Unbanked.Num();
				for (const FGFItemStack& S : R.Unbanked)
				{
					bSameDrop &= FGFRewards::CountOf(Again.Unbanked, S.ItemId) == S.Count;
				}
				for (const FGFItemStack& S : Again.Unbanked)
				{
					bSameDrop &= FGFRewards::CountOf(R.Unbanked, S.ItemId) == S.Count;
				}
				TestTrue(*FString::Printf(TEXT("%s seed %d drops the same thing twice"), *Kind.ToString(), Seed), bSameDrop);
			}

			FGFLabRun Over = Solo(Kind, Seed);
			Over.Clock = FGFLabRules::ShiftSeconds;
			if (FGFLabRules::BeginContain(Over, 0) != EGFLabOrder::Ok)
			{
				++ContainFails;
				continue;
			}
			Run(Over, ContainFor);
			if (Seed == 1)
			{
				TestTrue(*FString::Printf(TEXT("%s finished in overtime"), *Kind.ToString()), FGFLabRules::IsOvertime(Over.Clock));
			}
			for (const FGFItemStack& S : Over.Unbanked)
			{
				OvertimeItems += S.Count;
			}
		}
		TestEqual(*FString::Printf(TEXT("%s containments started"), *Kind.ToString()), ContainFails, 0);
		// One real containment that actually dropped, banked both ways.
		{
			bool bSampled = false;
			for (int32 Seed = 1; Seed <= 40 && !bSampled; ++Seed)
			{
				FGFLabRun R = Solo(Kind, 5000 + Seed);
				if (FGFLabRules::BeginContain(R, 0) != EGFLabOrder::Ok)
				{
					continue;
				}
				Run(R, (Row ? Row->ContainSeconds : 4.f) + 0.25f);
				if (R.Unbanked.Num() == 0)
				{
					continue;
				}
				bSampled = true;
				const TArray<FGFItemStack> Evac = FGFLabRules::Bank(R.Unbanked, EGFLabEnd::Evacuate);
				const TArray<FGFItemStack> Kept = FGFLabRules::Bank(R.Unbanked, EGFLabEnd::Extract);
				for (const FGFItemStack& S : R.Unbanked)
				{
					TestEqual(*FString::Printf(TEXT("%s extract keeps %s"), *Kind.ToString(), *S.ItemId.ToString()), FGFRewards::CountOf(Kept, S.ItemId), S.Count);
					TestEqual(*FString::Printf(TEXT("%s evacuate keeps 75%% of %s"), *Kind.ToString(), *S.ItemId.ToString()),
						FGFRewards::CountOf(Evac, S.ItemId), FMath::FloorToInt(S.Count * FGFLabRules::EvacuateKeep));
				}
			}
			TestTrue(*FString::Printf(TEXT("%s sample dropped something"), *Kind.ToString()), bSampled);
		}
		const float Rate = static_cast<float>(Hits) / static_cast<float>(RateSeeds);
		AddInfo(FString::Printf(TEXT("%s Anomaly Shard rate %.3f (table %.2f, %d/%d)"), *Kind.ToString(), Rate, Chance, Hits, RateSeeds));
		TestTrue(*FString::Printf(TEXT("%s shard rate near the table"), *Kind.ToString()), FMath::Abs(Rate - Chance) <= 0.08f);
	}
	AddInfo(FString::Printf(TEXT("Containment drops: %d items in the shift, %d in overtime"), NormalItems, OvertimeItems));
	TestTrue(TEXT("Overtime pays more on anomaly drops"), OvertimeItems > NormalItems * 1.3f);

	const TArray<FGFItemStack> AnomalyDrop = { FGFItemStack(TEXT("AnomalyShard"), 4), FGFItemStack(TEXT("CircuitInk"), 1) };
	TestEqual(TEXT("Evacuate keeps 75 % of a shard stack: 4 -> 3"), FGFRewards::CountOf(FGFLabRules::Bank(AnomalyDrop, EGFLabEnd::Evacuate), TEXT("AnomalyShard")), 3);
	TestEqual(TEXT("Evacuate keeps 75 % of a single: 1 -> 0"), FGFRewards::CountOf(FGFLabRules::Bank(AnomalyDrop, EGFLabEnd::Evacuate), TEXT("CircuitInk")), 0);
	TestEqual(TEXT("Lockdown banks anomaly drops the same way"), FGFRewards::CountOf(FGFLabRules::Bank(AnomalyDrop, EGFLabEnd::Lockdown), TEXT("AnomalyShard")), 3);

	const float AnomalyEach = FGFLabRules::ExpectedAnomalyValue(ItemsT);
	const float PerMin = FGFLabRules::ExpectedValuePerMinute(ItemsT);
	AddInfo(FString::Printf(TEXT("Anomaly contain ~%.1f value, %.2f a shift, lab %.1f a minute"),
		AnomalyEach, FGFLabRules::ExpectedAnomaliesPerShift() * AnomalyEach, PerMin));
	TestTrue(TEXT("Anomaly drops stay inside the per-minute cap"), PerMin <= 80.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLabCraftTest, "GFGame.Lab.Craft", GFLabTestFlags)
bool FGFLabCraftTest::RunTest(const FString&)
{
	using namespace GFLabTestUtil;
	const UDataTable* ItemsT = Items();
	const UDataTable* RecipesT = Recipes();
	if (!TestNotNull(TEXT("DT_Recipes"), RecipesT) || !TestNotNull(TEXT("DT_Items"), ItemsT))
	{
		return false;
	}
	int32 LabRecipes = 0;
	TSet<FName> Outputs;
	RecipesT->ForeachRow<FGFRecipeRow>(TEXT("Test"), [&](const FName& Id, const FGFRecipeRow& R)
	{
		if (R.Station != FGFLabRules::Station)
		{
			return;
		}
		++LabRecipes;
		Outputs.Add(R.Output.ItemId);
		const FGFItemRow* Out = FGFEconomy::FindItem(ItemsT, R.Output.ItemId);
		TestNotNull(*FString::Printf(TEXT("%s output exists"), *Id.ToString()), Out);
		bool bJunkOrLab = false;
		for (const FGFItemStack& In : R.Inputs)
		{
			const FGFItemRow* I = FGFEconomy::FindItem(ItemsT, In.ItemId);
			TestNotNull(*FString::Printf(TEXT("%s input %s exists"), *Id.ToString(), *In.ItemId.ToString()), I);
			bJunkOrLab |= I && (I->Exclusive == EGFExclusive::Lab || In.ItemId == TEXT("ScrapWire") || In.ItemId == TEXT("BentPipe") || In.ItemId == TEXT("CrackedLens"));
		}
		TestTrue(*FString::Printf(TEXT("%s uses lab junk or lab finds"), *Id.ToString()), bJunkOrLab);
		// A sink, never a mint: what comes out is never worth more than what goes in.
		TestTrue(*FString::Printf(TEXT("%s output value <= inputs"), *Id.ToString()), FGFEconomy::StacksValue(ItemsT, { R.Output }) <= FGFEconomy::StacksValue(ItemsT, R.Inputs));
	});
	TestTrue(TEXT("At least 5 bench recipes"), LabRecipes >= 5);
	for (const TCHAR* Gear : { TEXT("LabTreat"), TEXT("CalmingMist"), TEXT("SafetyGoggles"), TEXT("LabCoat"), TEXT("BurrowLantern") })
	{
		TestTrue(*FString::Printf(TEXT("Bench makes %s"), Gear), Outputs.Contains(FName(Gear)));
	}
	const FGFItemRow* Mist = FGFEconomy::FindItem(ItemsT, TEXT("CalmingMist"));
	TestTrue(TEXT("Calming Mist is a tool: never wagered or rented"), Mist && Mist->Category == EGFItemCategory::Tool && !Mist->bWagerable && !Mist->bRentable);

	const FGFRecipeRow* MistRecipe = RecipesT->FindRow<FGFRecipeRow>(TEXT("CalmingMist"), TEXT("Test"), false);
	FGFInventory Bag;
	Bag.Add(TEXT("BioCulture"), 1);
	Bag.Add(TEXT("GlowDust"), 2);
	Bag.Add(TEXT("Fiber"), 1);
	TestTrue(TEXT("Missing a fiber"), MistRecipe && UGFCraftingSubsystem::TryCraft(*MistRecipe, FGFLabRules::Station, FGameplayTagContainer(), Bag) == EGFCraftResult::MissingInputs);
	Bag.Add(TEXT("Fiber"), 1);
	TestTrue(TEXT("Only at the Lab Bench"), MistRecipe && UGFCraftingSubsystem::TryCraft(*MistRecipe, TEXT("Cookpot"), FGameplayTagContainer(), Bag) == EGFCraftResult::WrongStation);
	TestTrue(TEXT("Brewed"), MistRecipe && UGFCraftingSubsystem::TryCraft(*MistRecipe, FGFLabRules::Station, FGameplayTagContainer(), Bag) == EGFCraftResult::Crafted);
	TestEqual(TEXT("One brew = 3 mists"), Bag.GetCount(TEXT("CalmingMist")), 3);
	TestEqual(TEXT("Inputs used"), Bag.GetCount(TEXT("BioCulture")) + Bag.GetCount(TEXT("GlowDust")) + Bag.GetCount(TEXT("Fiber")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLabEconomyTest, "GFGame.Lab.Economy", GFLabTestFlags)
bool FGFLabEconomyTest::RunTest(const FString&)
{
	using namespace GFLabTestUtil;
	const UDataTable* ItemsT = Items();
	const float Shift = FGFLabRules::ExpectedShiftValue(ItemsT);
	const float PerMin = FGFLabRules::ExpectedValuePerMinute(ItemsT);
	const float AnomalyBudget = FGFLabRules::ExpectedAnomaliesPerShift() * FGFLabRules::ExpectedAnomalyValue(ItemsT);
	AddInfo(FString::Printf(TEXT("Lab shift: ~%.0f pile value, %.1f a minute with anomaly drops (~%.0f) (reference hand gathering 80)"), Shift, PerMin, AnomalyBudget));
	TestTrue(TEXT("Worth a shift"), Shift >= 30.f);
	// Tier-3/4 exclusives make the lab the richest mode per minute, but it stays at or under hand gathering.
	// Anomaly drops are in that per-minute number (pile value + contain value, over pile time + contain time).
	TestTrue(TEXT("Lab <= hand gathering per minute"), PerMin <= 80.f);
	// Searching everything in the shift, then extracting, never loses to waiting for overtime on average.
	TestTrue(TEXT("A full search fits in the shift"), FGFLabRules::PileCount * (8.f + FGFLabRules::WalkSeconds) <= FGFLabRules::ShiftSeconds);
	// Seeded check that the junk table matches the pile estimate (within 15 %). Anomalies are sealed as they
	// wake so a jam or a hidden pile can't skip a search, and their drops are set aside: this number is the
	// piles. The per-minute figure above is the one that includes them.
	double Sum = 0.0;
	constexpr int32 Shifts = 500;
	int32 Missed = 0;
	float MaxClock = 0.f;
	for (int32 S = 1; S <= Shifts; ++S)
	{
		FGFLabRun R = FGFLabRules::StartRun(TEXT("Sim"), S * 7919);
		Missed += SearchEverything(R);
		MaxClock = FMath::Max(MaxClock, R.Clock);
		Sum += FGFEconomy::StacksValue(ItemsT, FGFLabRules::Bank(R.Unbanked, EGFLabEnd::Extract));
	}
	const double Mean = Sum / Shifts;
	AddInfo(FString::Printf(TEXT("Seeded mean shift value %.1f (%d piles missed, slowest clock %.1f s)"), Mean, Missed, MaxClock));
	TestEqual(TEXT("Every pile searched"), Missed, 0);
	TestTrue(TEXT("Seeded mean near the estimate"), FMath::Abs(Mean - Shift) <= Shift * 0.15f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLabTamingAidsTest, "GFGame.Lab.TamingAids", GFLabTestFlags)
bool FGFLabTamingAidsTest::RunTest(const FString&)
{
	using namespace GFLabTestUtil;
	const UDataTable* Diets = FGFDataTables::Get<FDinoDietRow>(TEXT("DT_DinoDiet"));
	if (!TestNotNull(TEXT("DT_DinoDiet"), Diets))
	{
		return false;
	}
	Diets->ForeachRow<FDinoDietRow>(TEXT("Test"), [&](const FName& Id, const FDinoDietRow& Row)
	{
		TestTrue(*FString::Printf(TEXT("%s eats Lab Treats"), *Id.ToString()), Row.Accepts(GFLabItems::LabTreat));
		TestEqual(*FString::Printf(TEXT("%s: 1.5x its usual food"), *Id.ToString()), FGFDinoDiet::BondGain(Row, GFLabItems::LabTreat), FMath::RoundToFloat(Row.MeanAcceptedGain() * 1.5f));
		TestTrue(*FString::Printf(TEXT("%s: still below a favourite"), *Id.ToString()), FGFDinoDiet::BondGain(Row, GFLabItems::LabTreat) < Row.FavouriteGain());
	});

	const FDinoDietRow* Raptor = Diet(TEXT("Raptor"));
	if (!TestNotNull(TEXT("Raptor diet"), Raptor))
	{
		return false;
	}
	// Empty-handed inside the aggro radius: normally a threat.
	const TArray<FGFTamerView> Tamers = { FGFTamerView(TEXT("Owner"), 500.f, NAME_None, false) };
	FGFTameMachine M;
	float Now = 0.f;
	M.Tick(*Raptor, Now, 0.1f, Tamers);
	M.Tick(*Raptor, Now += 0.1f, 0.1f, Tamers);
	TestTrue(TEXT("Threat without food"), M.State == EGFTameState::Threat);
	M.ForceHostile(TEXT("Owner"));
	TestTrue(TEXT("Hostile"), M.State == EGFTameState::Hostile);

	TestTrue(TEXT("Mist works"), M.ApplyCalm(Now));
	TestTrue(TEXT("Hostile -> Alert at once"), M.State == EGFTameState::Alert && M.TargetKey.IsEmpty());
	const float MistEnds = M.CalmUntil;
	bool bStayedCalm = true;
	auto TickTo = [&](float Until)
	{
		while (Now < Until)
		{
			M.Tick(*Raptor, Now += 0.25f, 0.25f, Tamers);
			bStayedCalm &= Now >= MistEnds || (M.State != EGFTameState::Threat && M.State != EGFTameState::Hostile);
		}
	};
	TickTo(Now + 0.5f);
	TestTrue(TEXT("Curious about the player while calm"), M.State == EGFTameState::Curious);
	// Wrong food twice while calm: refused, but it doesn't turn on the player.
	TestTrue(TEXT("Refused"), M.Offer(*Raptor, Now, TEXT("Owner"), TEXT("Stone")) == EGFOfferResult::Refused);
	TickTo(Now + Raptor->OfferCooldown);
	TestTrue(TEXT("Refused again"), M.Offer(*Raptor, Now, TEXT("Owner"), TEXT("Stone")) == EGFOfferResult::Refused);
	TestTrue(TEXT("Calm through refusals"), M.State == EGFTameState::Curious);
	// A Lab Treat feeds it.
	TickTo(Now + Raptor->OfferCooldown);
	TestTrue(TEXT("Lab Treat accepted"), M.Offer(*Raptor, Now, TEXT("Owner"), GFLabItems::LabTreat) == EGFOfferResult::Accepted);
	TestEqual(TEXT("Bond from the treat"), M.Bond, FGFDinoDiet::BondGain(*Raptor, GFLabItems::LabTreat));
	TickTo(MistEnds - 0.1f);
	TestTrue(TEXT("Nobody is a threat for the mist's 10 s"), bStayedCalm && Now - (MistEnds - FGFTameMachine::CalmingMistSeconds) >= 9.5f);
	// The mist wears off: empty-handed in range is a threat again.
	TickTo(MistEnds + 1.f);
	TestTrue(TEXT("After the mist: threat again"), M.State == EGFTameState::Threat);

	FGFTameMachine Tamed;
	Tamed.State = EGFTameState::Tamed;
	TestFalse(TEXT("No mist on a tamed dino"), Tamed.ApplyCalm(0.f));
	return true;
}

#endif
