// Dino Arena (PLAN.md M6): ability data, combat math, the seeded auto-battle, Slow Time, commands,
// possession, wager safeguards, the AI's counter-bet and the free reward roll. All pure (no world).
#include "Misc/AutomationTest.h"
#include "GFArenaTypes.h"
#include "GFWager.h"
#include "GFEconomy.h"
#include "GFRewards.h"
#include "GFRecipeTypes.h"
#include "GFItemTypes.h"
#include "GFDataTables.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFArenaTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFArenaTestUtil
{
	static FGFArenaCommand Cmd(const FString& Who, int32 Unit, int32 Slot, int32 Target)
	{
		FGFArenaCommand C;
		C.IssuerKey = Who;
		C.UnitId = Unit;
		C.Slot = Slot;
		C.TargetId = Target;
		return C;
	}

	static FGFArenaEntrant Dino(const FString& Owner, FName Species, int32 Level, int32 Spot)
	{
		FGFArenaEntrant E;
		E.OwnerKey = Owner;
		E.CompanionId = FGuid::NewGuid();
		E.Species = Species;
		E.Level = Level;
		E.Spot = Spot;
		return E;
	}

	static void ZeroCooldowns(FGFArenaSim& Sim)
	{
		for (FGFArenaUnit& U : Sim.Units)
		{
			for (float& C : U.Cooldowns)
			{
				C = 0.f;
			}
			U.AttackTimer = 10.f; // no basic attacks muddying the numbers
		}
	}

	static void SetStatus(FGFArenaUnit& U, EGFArenaStatus S, float Seconds)
	{
		U.Status[static_cast<int32>(S)] = Seconds;
	}

	static const UDataTable* Items()
	{
		return FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	}

	static FGFInventory MakeInv(std::initializer_list<FGFItemStack> Stacks)
	{
		FGFInventory Inv;
		for (const FGFItemStack& S : Stacks)
		{
			Inv.Add(S.ItemId, S.Count);
		}
		return Inv;
	}
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaAbilityDataTest, "GFGame.Arena.AbilityData", GFArenaTestFlags)
bool FGFArenaAbilityDataTest::RunTest(const FString&)
{
	const FGFArenaData& D = FGFArenaData::Get();
	TestEqual(TEXT("Five arena species"), D.Species.Num(), 5);
	for (const FName S : D.SpeciesIds())
	{
		for (int32 i = 0; i < 3; ++i)
		{
			const FGFDinoAbilityRow* Row = D.Ability(S, i);
			if (TestNotNull(*FString::Printf(TEXT("%s ability %d"), *S.ToString(), i + 1), Row))
			{
				TestTrue(TEXT("Cooldown positive"), Row->Cooldown > 0.f);
				TestEqual(TEXT("Third ability unlocks at 3"), Row->UnlockLevel, i == 2 ? 3 : 1);
			}
		}
		TestEqual(TEXT("Two abilities at level 1"), D.AbilityCountFor(S, 1), 2);
		TestEqual(TEXT("Three at level 3"), D.AbilityCountFor(S, 3), 3);
	}
	for (int32 Stars = 0; Stars <= 3; ++Stars)
	{
		const FGFArenaRewardRow* R = D.RewardFor(Stars);
		if (TestNotNull(*FString::Printf(TEXT("Reward row %d"), Stars), R))
		{
			TestFalse(TEXT("Rewards are materials only"), FGFRewards::ContainsExclusive(GFArenaTestUtil::Items(), R->Rolls, EGFExclusive::HallowNight) || FGFRewards::ContainsExclusive(GFArenaTestUtil::Items(), R->Rolls, EGFExclusive::Lab));
		}
	}
	const FGFWagerBandRow* Friendly = D.BandRow(0);
	const FGFWagerBandRow* Huge = D.BandRow(4);
	if (TestNotNull(TEXT("No wager = Medium"), Friendly) && TestNotNull(TEXT("Huge band"), Huge))
	{
		TestEqual(TEXT("Friendly fights are even"), Friendly->LevelDelta, 0);
		TestTrue(TEXT("Huge stakes meet the Champion"), Huge->AI == EGFArenaAI::Champion);
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaMathTest, "GFGame.Arena.Math", GFArenaTestFlags)
bool FGFArenaMathTest::RunTest(const FString&)
{
	using namespace GFArenaTestUtil;
	TestEqual(TEXT("Plain"), FGFArenaMath::Damage(10.f, 1.5f, false, false, false), 15.f, 0.001f);
	TestEqual(TEXT("Well fed"), FGFArenaMath::Damage(10.f, 1.5f, true, false, false), 17.25f, 0.001f);
	TestEqual(TEXT("Shred"), FGFArenaMath::Damage(10.f, 1.5f, false, true, false), 18.f, 0.001f);
	TestEqual(TEXT("Guard"), FGFArenaMath::Damage(10.f, 1.5f, false, false, true), 10.5f, 0.001f);
	TestEqual(TEXT("Combo"), FGFArenaMath::Damage(10.f, 1.5f, false, false, false, 2.f), 30.f, 0.001f);
	TestEqual(TEXT("Level 3 scale"), FGFArenaMath::LevelScale(3), 1.2f, 0.001f);

	const FGFArenaData& D = FGFArenaData::Get();
	// Tidal Snap does double damage to a Wet target (River Spray -> Tidal Snap combo).
	for (const bool bWet : { true, false })
	{
		FGFArenaSim Sim;
		Sim.Init(7, EGFArenaAI::Normal, EGFArenaAI::Normal);
		const int32 A = Sim.AddUnit(D, 0, Dino(TEXT("Her"), TEXT("Sailback"), 3, 1));
		const int32 B = Sim.AddUnit(D, 1, Dino(FString(), TEXT("Rex"), 1, 1));
		ZeroCooldowns(Sim);
		Sim.Find(B)->Pos = Sim.Find(A)->Pos + FVector2D(150.f, 0.f);
		if (bWet)
		{
			SetStatus(*Sim.Find(B), EGFArenaStatus::Wet, 5.f);
		}
		const float Before = Sim.Find(B)->HP;
		Sim.ApplyCommands(D, { Cmd(TEXT("Her"), A, 2, B) });
		const float Dealt = Before - Sim.Find(B)->HP;
		const float Expected = Sim.Find(A)->Attack * 1.6f * (bWet ? 2.f : 1.f);
		TestEqual(bWet ? TEXT("Tidal Snap x2 vs Wet") : TEXT("Tidal Snap plain"), Dealt, Expected, 0.01f);
		TestEqual(TEXT("Combo counted"), Sim.Combos, bWet ? 1 : 0);
	}
	// Sail Flare taunts nearby enemies and guards the Sailback.
	{
		FGFArenaSim Sim;
		Sim.Init(8, EGFArenaAI::Normal, EGFArenaAI::Normal);
		const int32 A = Sim.AddUnit(D, 0, Dino(TEXT("Her"), TEXT("Sailback"), 1, 1));
		const int32 B = Sim.AddUnit(D, 1, Dino(FString(), TEXT("Raptor"), 1, 1));
		ZeroCooldowns(Sim);
		Sim.Find(B)->Pos = Sim.Find(A)->Pos + FVector2D(300.f, 0.f);
		Sim.ApplyCommands(D, { Cmd(TEXT("Her"), A, 0, INDEX_NONE) });
		TestTrue(TEXT("Raptor taunted"), Sim.Find(B)->Has(EGFArenaStatus::Taunt));
		TestEqual(TEXT("Taunted by the Sailback"), Sim.Find(B)->TauntBy, A);
		TestTrue(TEXT("Sailback guarded"), Sim.Find(A)->Has(EGFArenaStatus::Guard));
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaAutoSimTest, "GFGame.Arena.AutoSim", GFArenaTestFlags)
bool FGFArenaAutoSimTest::RunTest(const FString&)
{
	using namespace GFArenaTestUtil;
	const FGFArenaData& D = FGFArenaData::Get();
	const TArray<FName> Species = D.SpeciesIds();
	const EGFArenaAI Profiles[] = { EGFArenaAI::Easy, EGFArenaAI::Normal, EGFArenaAI::Champion };
	auto Build = [&](int32 Seed, FGFArenaSim& Sim)
	{
		FRandomStream R(Seed * 31 + 5);
		Sim.Init(Seed, EGFArenaAI::Normal, Profiles[R.RandRange(0, 2)]);
		const int32 Level = R.RandRange(1, 6);
		for (int32 i = 0; i < 3; ++i)
		{
			Sim.AddUnit(D, 0, Dino(TEXT("Her"), Species[R.RandHelper(Species.Num())], Level, i * 2));
		}
		const int32 Opp = R.RandRange(3, 4);
		for (int32 i = 0; i < Opp; ++i)
		{
			Sim.AddUnit(D, 1, Dino(FString(), Species[R.RandHelper(Species.Num())], Level, i));
		}
	};
	int32 Wins[2] = { 0, 0 }, Draws = 0, Abilities = 0;
	float WorstIdle = 0.f, Longest = 0.f;
	const int32 Fights = 500;
	for (int32 Seed = 1; Seed <= Fights; ++Seed)
	{
		FGFArenaSim A, B;
		Build(Seed, A);
		Build(Seed, B);
		const EGFArenaOutcome OA = A.RunToEnd(D);
		const EGFArenaOutcome OB = B.RunToEnd(D);
		if (OA != OB || A.Time != B.Time)
		{
			AddError(FString::Printf(TEXT("Seed %d is not deterministic"), Seed));
		}
		Wins[0] += OA == EGFArenaOutcome::Team0Won ? 1 : 0;
		Wins[1] += OA == EGFArenaOutcome::Team1Won ? 1 : 0;
		Draws += OA == EGFArenaOutcome::Draw ? 1 : 0;
		Longest = FMath::Max(Longest, A.Time);
		for (const FGFArenaUnit& U : A.Units)
		{
			WorstIdle = FMath::Max(WorstIdle, U.MaxIdle);
			Abilities += U.AbilitiesUsed;
		}
	}
	AddInfo(FString::Printf(TEXT("Arena: %d/%d/%d (her/opp/draw), longest %.1fs, worst idle %.2fs, %d abilities"), Wins[0], Wins[1], Draws, Longest, WorstIdle, Abilities));
	TestTrue(TEXT("Every fight ends within 180 s"), Longest <= FGFArenaSim::MaxSeconds + 0.1f);
	TestTrue(TEXT("No dino stands stuck 3 s"), WorstIdle < 3.f);
	TestTrue(TEXT("Her side wins some"), Wins[0] > 0);
	TestTrue(TEXT("Opponents win some"), Wins[1] > 0);
	TestTrue(TEXT("Timeouts under 5 %"), Draws < Fights / 20);
	TestTrue(TEXT("Abilities get used"), Abilities > Fights);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaSlowTimeTest, "GFGame.Arena.SlowTime", GFArenaTestFlags)
bool FGFArenaSlowTimeTest::RunTest(const FString&)
{
	FGFSlowTime S;
	TestTrue(TEXT("Ready at start"), S.TryActivate(100.0));
	TestEqual(TEXT("Dilation 0.2"), S.CurrentDilation(), 0.2f, 0.0001f);
	TestEqual(TEXT("3 s left"), S.Remaining(103.0), 3.0, 0.0001);
	TestFalse(TEXT("One charge"), S.TryActivate(101.0));
	TestFalse(TEXT("Still running at 105"), S.Update(105.0));
	TestTrue(TEXT("Runs out at 106"), S.Update(106.0));
	TestEqual(TEXT("Back to 1.0"), S.CurrentDilation(), 1.f, 0.0001f);
	TestEqual(TEXT("30 s cooldown from the end"), S.CooldownLeft(107.0), 29.0, 0.0001);
	TestFalse(TEXT("Not ready at 130"), S.TryActivate(130.0));
	TestTrue(TEXT("Ready at 136"), S.TryActivate(136.0));
	TestTrue(TEXT("Second press ends it"), S.End(137.0));
	TestFalse(TEXT("Ending twice does nothing"), S.End(138.0));
	TestEqual(TEXT("Cooldown restarts from the early end"), S.CooldownLeft(147.0), 20.0, 0.0001);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaCommandsTest, "GFGame.Arena.Commands", GFArenaTestFlags)
bool FGFArenaCommandsTest::RunTest(const FString&)
{
	using namespace GFArenaTestUtil;
	const FGFArenaData& D = FGFArenaData::Get();
	FGFArenaSim Sim;
	Sim.Init(11, EGFArenaAI::Normal, EGFArenaAI::Normal);
	const int32 Rex = Sim.AddUnit(D, 0, Dino(TEXT("Her"), TEXT("Rex"), 3, 1));
	const int32 Nip = Sim.AddUnit(D, 0, Dino(TEXT("Her"), TEXT("Nipper"), 1, 3));
	const int32 Foe = Sim.AddUnit(D, 1, Dino(FString(), TEXT("Sailback"), 1, 1));
	const int32 Far = Sim.AddUnit(D, 1, Dino(FString(), TEXT("Hornback"), 1, 5));

	auto Check = [&](const FGFArenaCommand& C) { return Sim.CheckCommand(D, C).ToString(); };
	TestEqual(TEXT("Only the owner commands"), Check(Cmd(TEXT("Friend"), Rex, 0, Foe)), FString(TEXT("NotOwner")));
	TestEqual(TEXT("Opponents can't be commanded"), Check(Cmd(TEXT("Her"), Foe, 0, Rex)), FString(TEXT("NotOwner")));
	TestEqual(TEXT("Ability 3 locked at level 1"), Check(Cmd(TEXT("Her"), Nip, 2, INDEX_NONE)), FString(TEXT("Locked")));
	TestEqual(TEXT("Cooling down"), Check(Cmd(TEXT("Her"), Rex, 0, INDEX_NONE)), FString(TEXT("Cooldown")));
	ZeroCooldowns(Sim);
	TestEqual(TEXT("Own dino isn't a target"), Check(Cmd(TEXT("Her"), Nip, 0, Rex)), FString(TEXT("BadTarget")));
	TestEqual(TEXT("Ok"), Check(Cmd(TEXT("Her"), Rex, 0, INDEX_NONE)), FString(TEXT("Ok")));

	// Roar (area stun) goes off at once in range; the Nipper's Swarm Nip on the far Hornback waits and walks there.
	Sim.Find(Foe)->Pos = Sim.Find(Rex)->Pos + FVector2D(200.f, 0.f);
	Sim.Find(Far)->Pos = FVector2D(1000.f, 600.f);
	Sim.ApplyCommands(D, { Cmd(TEXT("Her"), Rex, 0, INDEX_NONE), Cmd(TEXT("Her"), Nip, 0, Far) });
	TestTrue(TEXT("Roar stunned the Sailback"), Sim.Find(Foe)->Has(EGFArenaStatus::Stun));
	TestEqual(TEXT("Roar on cooldown"), Sim.Find(Rex)->Cooldowns[0], 16.f, 0.001f);
	TestEqual(TEXT("Swarm Nip pending"), Sim.Find(Nip)->CommandSlot, 0);
	const float Dist0 = FVector2D::Distance(Sim.Find(Nip)->Pos, Sim.Find(Far)->Pos);
	Sim.Step(D, FGFArenaSim::StepSeconds);
	TestTrue(TEXT("Nipper heads for its target"), FVector2D::Distance(Sim.Find(Nip)->Pos, Sim.Find(Far)->Pos) < Dist0);
	int32 Steps = 0;
	while (Sim.Find(Nip)->CommandSlot != INDEX_NONE && Steps++ < 400 && !Sim.IsOver())
	{
		Sim.Step(D, FGFArenaSim::StepSeconds);
	}
	TestEqual(TEXT("Order resolved"), Sim.Find(Nip)->CommandSlot, int32(INDEX_NONE));
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaPossessionTest, "GFGame.Arena.Possession", GFArenaTestFlags)
bool FGFArenaPossessionTest::RunTest(const FString&)
{
	using namespace GFArenaTestUtil;
	const FGFArenaData& D = FGFArenaData::Get();
	FGFArenaSim Sim;
	Sim.Init(12, EGFArenaAI::Normal, EGFArenaAI::Normal);
	const int32 A = Sim.AddUnit(D, 0, Dino(TEXT("Her"), TEXT("Rex"), 1, 1));
	const int32 B = Sim.AddUnit(D, 0, Dino(TEXT("Her"), TEXT("Raptor"), 1, 4));
	Sim.AddUnit(D, 1, Dino(FString(), TEXT("Hornback"), 1, 1));

	TestEqual(TEXT("Not hers"), Sim.Possess(TEXT("Friend"), A).ToString(), FString(TEXT("NotOwner")));
	TestEqual(TEXT("Possess"), Sim.Possess(TEXT("Her"), A).ToString(), FString(TEXT("Ok")));
	TestEqual(TEXT("Possessing A"), Sim.PossessedBy(TEXT("Her")), A);
	const float X0 = Sim.Find(A)->Pos.X;
	TestEqual(TEXT("Input"), Sim.SetInput(TEXT("Her"), FVector2D(1.f, 0.f), false, INDEX_NONE).ToString(), FString(TEXT("Ok")));
	for (int32 i = 0; i < 10; ++i)
	{
		Sim.Step(D, FGFArenaSim::StepSeconds);
	}
	TestTrue(TEXT("She steers it"), Sim.Find(A)->Pos.X > X0 + 50.f);
	TestEqual(TEXT("Cycle to B"), Sim.Cycle(TEXT("Her")), B);
	TestTrue(TEXT("A back on AI"), Sim.Find(A)->PossessedBy.IsEmpty() && Sim.Find(A)->MoveInput.IsNearlyZero());
	TestEqual(TEXT("Cycle back to A"), Sim.Cycle(TEXT("Her")), A);

	// Her dino faints under her control: she hops to the next one.
	Sim.Find(A)->HP = 0.5f;
	SetStatus(*Sim.Find(A), EGFArenaStatus::Burn, 5.f);
	Sim.Step(D, FGFArenaSim::StepSeconds);
	TestTrue(TEXT("A fainted"), Sim.Find(A)->bFainted);
	TestEqual(TEXT("Auto-cycled to B"), Sim.PossessedBy(TEXT("Her")), B);
	TestEqual(TEXT("Can't possess a fainted dino"), Sim.Possess(TEXT("Her"), A).ToString(), FString(TEXT("Fainted")));
	Sim.Release(TEXT("Her"));
	TestEqual(TEXT("Back on the balcony"), Sim.PossessedBy(TEXT("Her")), int32(INDEX_NONE));
	TestEqual(TEXT("No input when not possessing"), Sim.SetInput(TEXT("Her"), FVector2D(1.f, 0.f), true, 0).ToString(), FString(TEXT("NotPossessing")));
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaWagerSafeguardsTest, "GFGame.Arena.Wager.Safeguards", GFArenaTestFlags)
bool FGFArenaWagerSafeguardsTest::RunTest(const FString&)
{
	using namespace GFArenaTestUtil;
	const UDataTable* T = Items();
	if (!TestNotNull(TEXT("Items"), T))
	{
		return false;
	}
	const FGFInventory Inv = MakeInv({ FGFItemStack(TEXT("Amber"), 10), FGFItemStack(TEXT("Wood"), 20), FGFItemStack(TEXT("Stone"), 5), FGFItemStack(TEXT("Clay"), 5), FGFItemStack(TEXT("Chair"), 1) });
	TestEqual(TEXT("Cap is 25 % of 110"), FGFWagerRules::CapValue(T, Inv), int64(27));
	auto Check = [&](std::initializer_list<FGFItemStack> S, const FGFRecipeRow* Pinned = nullptr, bool bOff = false)
	{
		return FGFWagerRules::CheckStake(T, Inv, TArray<FGFItemStack>(S), Pinned, bOff);
	};
	TestTrue(TEXT("Ok"), Check({ FGFItemStack(TEXT("Amber"), 3) }) == EGFWagerCheck::Ok);
	TestTrue(TEXT("Wagers can be switched off"), Check({ FGFItemStack(TEXT("Amber"), 3) }, nullptr, true) == EGFWagerCheck::WagersOff);
	TestTrue(TEXT("Empty"), Check({}) == EGFWagerCheck::Empty);
	TestTrue(TEXT("Duplicates refused"), Check({ FGFItemStack(TEXT("Wood"), 2), FGFItemStack(TEXT("Wood"), 3) }) == EGFWagerCheck::InvalidStacks);
	TestTrue(TEXT("At most 3 stacks"), Check({ FGFItemStack(TEXT("Wood"), 1), FGFItemStack(TEXT("Stone"), 1), FGFItemStack(TEXT("Clay"), 1), FGFItemStack(TEXT("Amber"), 1) }) == EGFWagerCheck::TooManyStacks);
	TestTrue(TEXT("Furniture never"), Check({ FGFItemStack(TEXT("Chair"), 1) }) == EGFWagerCheck::NotWagerable);
	TestTrue(TEXT("Dinos never"), Check({ FGFItemStack(TEXT("Raptor"), 1) }) == EGFWagerCheck::NotWagerable);
	TestTrue(TEXT("Only what she has"), Check({ FGFItemStack(TEXT("Wood"), 25) }) == EGFWagerCheck::Insufficient);
	TestTrue(TEXT("25 % cap"), Check({ FGFItemStack(TEXT("Amber"), 4) }) == EGFWagerCheck::OverCap);
	FGFRecipeRow Pinned;
	Pinned.Inputs.Add(FGFItemStack(TEXT("Amber"), 9));
	Pinned.Output = FGFItemStack(TEXT("Glitter"), 1);
	TestEqual(TEXT("Pinned needs 9 amber"), FGFWagerRules::ProtectedCount(&Pinned, TEXT("Amber")), 9);
	TestTrue(TEXT("Pinned recipe materials are protected"), Check({ FGFItemStack(TEXT("Amber"), 2) }, &Pinned) == EGFWagerCheck::PinnedProtected);
	TestTrue(TEXT("The spare one is fine"), Check({ FGFItemStack(TEXT("Amber"), 1) }, &Pinned) == EGFWagerCheck::Ok);

	const TArray<FGFItemStack> Suggest = FGFWagerRules::Suggest(T, Inv, &Pinned);
	const int64 SV = FGFEconomy::StacksValue(T, Suggest);
	AddInfo(FString::Printf(TEXT("Suggested stake value %lld"), SV));
	TestTrue(TEXT("Suggestion is something"), Suggest.Num() > 0 && SV > 0);
	TestTrue(TEXT("Suggestion passes the rules"), FGFWagerRules::CheckStake(T, Inv, Suggest, &Pinned, false) == EGFWagerCheck::Ok);
	TestTrue(TEXT("Suggestion near 10 %"), SV <= 16);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaWagerMatchTest, "GFGame.Arena.Wager.Match", GFArenaTestFlags)
bool FGFArenaWagerMatchTest::RunTest(const FString&)
{
	using namespace GFArenaTestUtil;
	const UDataTable* T = Items();
	if (!TestNotNull(TEXT("Items"), T))
	{
		return false;
	}
	TArray<FName> Plain, Exclusive;
	T->ForeachRow<FGFItemRow>(TEXT("WagerMatch"), [&](const FName& Key, const FGFItemRow& Row)
	{
		if (!FGFEconomy::IsWagerable(T, Key))
		{
			return;
		}
		const bool bEx = FGFRewards::ContainsExclusive(T, TArray<FGFItemStack>{ FGFItemStack(Key, 1) }, EGFExclusive::HallowNight) ||
			FGFRewards::ContainsExclusive(T, TArray<FGFItemStack>{ FGFItemStack(Key, 1) }, EGFExclusive::Lab);
		(bEx ? Exclusive : Plain).Add(Key);
	});
	TestTrue(TEXT("Items to wager"), Plain.Num() > 10);
	FRandomStream R(99);
	int32 PlainTried = 0, PlainEmpty = 0, Bad = 0;
	for (int32 i = 0; i < 3000; ++i)
	{
		const bool bUseExclusive = Exclusive.Num() > 0 && R.RandRange(0, 9) == 0;
		TArray<FGFItemStack> Stake;
		const int32 N = R.RandRange(1, 3);
		for (int32 s = 0; s < N; ++s)
		{
			const FName Id = (bUseExclusive && s == 0) ? Exclusive[R.RandHelper(Exclusive.Num())] : Plain[R.RandHelper(Plain.Num())];
			if (!Stake.ContainsByPredicate([&](const FGFItemStack& X) { return X.ItemId == Id; }))
			{
				Stake.Emplace(Id, R.RandRange(1, 12));
			}
		}
		TArray<FName> Needs;
		Needs.Add(Plain[R.RandHelper(Plain.Num())]);
		const TArray<FGFItemStack> C1 = FGFWagerRules::MatchCounter(T, Stake, Needs, i);
		const TArray<FGFItemStack> C2 = FGFWagerRules::MatchCounter(T, Stake, Needs, i);
		bool bSame = C1.Num() == C2.Num();
		for (int32 k = 0; bSame && k < C1.Num(); ++k)
		{
			bSame = C1[k].ItemId == C2[k].ItemId && C1[k].Count == C2[k].Count;
		}
		if (!bSame)
		{
			AddError(TEXT("Counter-bet not deterministic"));
		}
		if (!bUseExclusive)
		{
			++PlainTried;
			PlainEmpty += C1.Num() == 0 ? 1 : 0;
		}
		if (C1.Num() == 0)
		{
			continue;
		}
		FString Why;
		if (!FGFWagerRules::CheckCounter(T, Stake, C1, Needs, &Why))
		{
			if (++Bad <= 5)
			{
				AddError(FString::Printf(TEXT("Unfair counter: %s"), *Why));
			}
		}
		if (FGFRewards::ContainsExclusive(T, C1, EGFExclusive::HallowNight) || FGFRewards::ContainsExclusive(T, C1, EGFExclusive::Lab))
		{
			AddError(TEXT("Counter-bet holds an exclusive item"));
		}
	}
	AddInfo(FString::Printf(TEXT("No counter for %d of %d plain stakes"), PlainEmpty, PlainTried));
	TestEqual(TEXT("Every counter is fair"), Bad, 0);
	TestTrue(TEXT("Fewer than 3 % of plain stakes get no counter"), PlainEmpty * 100 < PlainTried * 3);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaRewardsTest, "GFGame.Arena.Rewards", GFArenaTestFlags)
bool FGFArenaRewardsTest::RunTest(const FString&)
{
	using namespace GFArenaTestUtil;
	const FGFArenaData& D = FGFArenaData::Get();
	const UDataTable* T = Items();
	double Mean[4] = { 0, 0, 0, 0 };
	const int32 Rolls = 10000;
	for (int32 Stars = 0; Stars <= 3; ++Stars)
	{
		const FGFArenaRewardRow* Row = D.RewardFor(Stars);
		if (!TestNotNull(TEXT("Row"), Row))
		{
			return false;
		}
		FRandomStream R(1000 + Stars);
		int32 Empty = 0;
		for (int32 i = 0; i < Rolls; ++i)
		{
			TArray<FGFItemStack> Out;
			FGFRewards::Roll(Row->Rolls, R, 1.f, Out);
			Empty += Out.Num() == 0 ? 1 : 0;
			Mean[Stars] += double(FGFEconomy::StacksValue(T, Out));
		}
		Mean[Stars] /= Rolls;
		TestEqual(TEXT("Every match gets something"), Empty, 0);
	}
	AddInfo(FString::Printf(TEXT("Reward means: %.1f %.1f %.1f %.1f"), Mean[0], Mean[1], Mean[2], Mean[3]));
	TestTrue(TEXT("More stars, better rewards"), Mean[0] < Mean[1] && Mean[1] < Mean[2] && Mean[2] < Mean[3]);
	TestTrue(TEXT("3 stars stays modest"), Mean[3] < 60.0);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEconomyForfeitTest, "GFGame.Economy.Forfeit", GFArenaTestFlags)
bool FGFEconomyForfeitTest::RunTest(const FString&)
{
	FGFMapLedgerAccounts A;
	A.Accounts.Add(TEXT("Her")).Add(TEXT("Amber"), 10);
	FGFTransferLedger L;
	const FGuid Wager = FGuid::NewGuid();
	L.OpenEscrow(Wager, TEXT("Wager"));
	TestTrue(TEXT("Stake in"), L.Deposit(A, FGuid::NewGuid(), Wager, TEXT("Her"), { FGFItemStack(TEXT("Amber"), 3) }) == EGFTxResult::Applied);
	const FGuid Tx = FGuid::NewGuid();
	TestTrue(TEXT("Lost wager forfeits"), L.Forfeit(Tx, Wager, TEXT("Lost")) == EGFTxResult::Applied);
	TestTrue(TEXT("Replay is a no-op"), L.Forfeit(Tx, Wager, TEXT("Lost")) == EGFTxResult::AlreadyApplied);
	TestEqual(TEXT("She keeps the rest"), A.Accounts[TEXT("Her")].GetCount(TEXT("Amber")), 7);
	TestEqual(TEXT("Nothing held"), L.HeldInEscrow(TEXT("Amber")), int64(0));
	TestEqual(TEXT("Counted as burned"), L.GetBurned(TEXT("Amber")), int64(3));
	TestTrue(TEXT("Can't refund after"), L.Refund(A, FGuid::NewGuid(), Wager) == EGFTxResult::EscrowClosed);
	return true;
}

#endif
