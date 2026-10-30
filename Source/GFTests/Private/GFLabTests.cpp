// Overtime, the after-hours lab: junk piles and shift rules, Lab Bench junk-to-gear recipes, the pacing budget,
// and the two taming aids it makes (the Lab Treat every species eats, the Calming Mist). All pure (no world).
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
	FGFLabRun R = FGFLabRules::StartRun(TEXT("Her"), 7);
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
		Run(X, 10.f, 0.5f);
		Run(Y, 10.f, 0.5f);
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
	AddInfo(FString::Printf(TEXT("Lab shift: ~%.0f value, %.1f a minute (reference hand gathering 80)"), Shift, PerMin));
	TestTrue(TEXT("Worth a shift"), Shift >= 30.f);
	// Tier-3/4 exclusives make the lab the richest mode per minute, but it stays at or under hand gathering.
	TestTrue(TEXT("Lab <= hand gathering per minute"), PerMin <= 80.f);
	// Searching everything in the shift, then extracting, never loses to waiting for overtime on average.
	TestTrue(TEXT("A full search fits in the shift"), FGFLabRules::PileCount * (8.f + FGFLabRules::WalkSeconds) <= FGFLabRules::ShiftSeconds);
	// Seeded check that the table matches the estimate (within 15 %).
	double Sum = 0.0;
	constexpr int32 Shifts = 500;
	for (int32 S = 1; S <= Shifts; ++S)
	{
		FGFLabRun R = FGFLabRules::StartRun(TEXT("Sim"), S * 7919);
		for (int32 i = 0; i < R.Piles.Num(); ++i)
		{
			FGFLabRules::BeginSearch(R, i);
			Run(R, 8.5f, 0.5f);
		}
		Sum += FGFEconomy::StacksValue(ItemsT, FGFLabRules::Bank(R.Unbanked, EGFLabEnd::Extract));
	}
	const double Mean = Sum / Shifts;
	AddInfo(FString::Printf(TEXT("Seeded mean shift value %.1f"), Mean));
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
	const TArray<FGFTamerView> Her = { FGFTamerView(TEXT("Her"), 500.f, NAME_None, false) };
	FGFTameMachine M;
	float Now = 0.f;
	M.Tick(*Raptor, Now, 0.1f, Her);
	M.Tick(*Raptor, Now += 0.1f, 0.1f, Her);
	TestTrue(TEXT("Threat without food"), M.State == EGFTameState::Threat);
	M.ForceHostile(TEXT("Her"));
	TestTrue(TEXT("Hostile"), M.State == EGFTameState::Hostile);

	TestTrue(TEXT("Mist works"), M.ApplyCalm(Now));
	TestTrue(TEXT("Hostile -> Alert at once"), M.State == EGFTameState::Alert && M.TargetKey.IsEmpty());
	const float MistEnds = M.CalmUntil;
	bool bStayedCalm = true;
	auto TickTo = [&](float Until)
	{
		while (Now < Until)
		{
			M.Tick(*Raptor, Now += 0.25f, 0.25f, Her);
			bStayedCalm &= Now >= MistEnds || (M.State != EGFTameState::Threat && M.State != EGFTameState::Hostile);
		}
	};
	TickTo(Now + 0.5f);
	TestTrue(TEXT("Curious about her while calm"), M.State == EGFTameState::Curious);
	// Wrong food twice while calm: refused, but it doesn't turn on her.
	TestTrue(TEXT("Refused"), M.Offer(*Raptor, Now, TEXT("Her"), TEXT("Stone")) == EGFOfferResult::Refused);
	TickTo(Now + Raptor->OfferCooldown);
	TestTrue(TEXT("Refused again"), M.Offer(*Raptor, Now, TEXT("Her"), TEXT("Stone")) == EGFOfferResult::Refused);
	TestTrue(TEXT("Calm through refusals"), M.State == EGFTameState::Curious);
	// A Lab Treat feeds it.
	TickTo(Now + Raptor->OfferCooldown);
	TestTrue(TEXT("Lab Treat accepted"), M.Offer(*Raptor, Now, TEXT("Her"), GFLabItems::LabTreat) == EGFOfferResult::Accepted);
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
