// Crystal theft rules. No world: sight, the warn / flee / hire flow, one hire, and heat.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Theft;Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "GFDinoTypes.h"
#include "GFTerritoryTypes.h"
#include "GFTheftRules.h"
#include "GFTownsfolkRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFTheftTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static FGFHireCandidate Cand(float Distance, bool bWild, bool bInRange)
	{
		FGFHireCandidate Out;
		Out.Distance = Distance;
		Out.bWild = bWild;
		Out.bInRange = bInRange;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTheftWitnessSightTest, "GFGame.Theft.Witness.Sight", GFTheftTests::Flags)
bool FGFTheftWitnessSightTest::RunTest(const FString&)
{
	using namespace GFTheftTests;
	TestTrue(TEXT("1500 cm with sight"), FGFTheftRules::Sees(EGFTownsfolkRole::Villager, 1500.f, true));
	TestTrue(TEXT("On the crystal"), FGFTheftRules::Sees(EGFTownsfolkRole::Villager, 0.f, true));
	TestFalse(TEXT("Just past 1500 cm"), FGFTheftRules::Sees(EGFTownsfolkRole::Villager, 1500.01f, true));
	TestFalse(TEXT("Close but blocked"), FGFTheftRules::Sees(EGFTownsfolkRole::Villager, 100.f, false));
	TestFalse(TEXT("An elder does not witness"), FGFTheftRules::Sees(EGFTownsfolkRole::Elder, 10.f, true));
	TestFalse(TEXT("A negative distance does not see"), FGFTheftRules::Sees(EGFTownsfolkRole::Villager, -1.f, true));
	TestEqual(TEXT("Warn shout"), FString(FGFTownsfolkRules::WarnShout()), FString(TEXT("Hey! Leave that crystal alone!")));

	FGFTerritoryFaction Rime;
	Rime.Id = TEXT("Rime");
	Rime.Center = FVector::ZeroVector;
	Rime.RadiusByMembers = { 0.f, 1600.f };
	FGFTerritoryFaction Verdant;
	Verdant.Id = TEXT("Verdant");
	Verdant.Center = FVector(-4400.f, 0.f, 0.f);
	Verdant.RadiusByMembers = { 0.f, 1400.f };
	TArray<FGFTerritoryFaction> Factions;
	Factions.Add(Rime);
	Factions.Add(Verdant);
	const FGFTerritoryCurve Curve;
	TestEqual(TEXT("Faction land keeps its id"), FGFTheftRules::ResolveTown(Factions, Curve, TEXT("Verdant"), FVector::ZeroVector), FName(TEXT("Verdant")));
	TestEqual(TEXT("An empty town still names its anchor"), FGFTheftRules::ResolveTown(Factions, Curve, NAME_None, FVector(100.f, 0.f, 40.f)), FName(TEXT("Rime")));
	TestTrue(TEXT("Far from every camp stays unnamed"), FGFTheftRules::ResolveTown(Factions, Curve, NAME_None, FVector(0.f, 8000.f, 0.f)).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTheftFlowWarnFleeHireTest, "GFGame.Theft.Flow.WarnFleeHire", GFTheftTests::Flags)
bool FGFTheftFlowWarnFleeHireTest::RunTest(const FString&)
{
	FGFTownsfolkMind Mind;
	Mind.TownCenter = FVector::ZeroVector;
	Mind.TownRadius = 5000.f;
	Mind.WalkSpeed = 140.f;
	Mind.StopDistance = 100.f;
	Mind.Home = FVector::ZeroVector;
	Mind.IdleMin = 4.f;
	Mind.IdleMax = 4.f;
	FVector Location(0.f, 0.f, 0.f);
	FRandomStream Rng(1);
	const FVector Thief(400.f, 0.f, 0.f);
	const FVector Elder(40.f, 0.f, 0.f);

	TestTrue(TEXT("Idle can warn"), Mind.BeginWarn(Thief, Elder));
	TestTrue(TEXT("Warning"), Mind.State == EGFTownsfolkBehavior::Warn);
	TestTrue(TEXT("Faces the thief"), Mind.FacePoint.Equals(Thief, 0.1f));
	TestFalse(TEXT("A warning does not stop to talk"), Mind.BeginTalk(4.f));
	TestTrue(TEXT("Still warning"), Mind.State == EGFTownsfolkBehavior::Warn);
	TestEqual(TEXT("The shout is still running"), static_cast<int32>(Mind.Advance(2.9f, Rng, Location)), static_cast<int32>(EGFTownsfolkPulse::None));
	TestTrue(TEXT("Warn lasts the full 3 s"), Mind.State == EGFTownsfolkBehavior::Warn);
	TestEqual(TEXT("Then flee"), static_cast<int32>(Mind.Advance(0.2f, Rng, Location)), static_cast<int32>(EGFTownsfolkPulse::StartedFlee));
	TestTrue(TEXT("Fleeing"), Mind.State == EGFTownsfolkBehavior::Flee);
	TestFalse(TEXT("Flee does not start a second warn"), Mind.BeginWarn(Thief, Elder));

	TestEqual(TEXT("Reaching the elder asks for the hire"), static_cast<int32>(Mind.Advance(0.1f, Rng, Location)), static_cast<int32>(EGFTownsfolkPulse::AskHire));
	TestTrue(TEXT("Back to idle after the ask"), Mind.State == EGFTownsfolkBehavior::Idle);
	TestTrue(TEXT("Did not leave the town disc"), FVector::Dist2D(Location, Mind.TownCenter) <= Mind.TownRadius + 0.05f);

	FGFTownsfolkMind Far;
	Far.TownCenter = FVector::ZeroVector;
	Far.TownRadius = 8000.f;
	Far.WalkSpeed = 100.f;
	Far.StopDistance = 50.f;
	Far.IdleMin = 1.f;
	Far.IdleMax = 1.f;
	FVector FarLocation(0.f, 0.f, 0.f);
	FRandomStream FarRng(2);
	TestTrue(TEXT("Far warn"), Far.BeginWarn(Thief, FVector(5000.f, 0.f, 0.f)));
	TestEqual(TEXT("Far flee starts"), static_cast<int32>(Far.Advance(FGFTownsfolkMind::WarnSeconds, FarRng, FarLocation)), static_cast<int32>(EGFTownsfolkPulse::StartedFlee));
	TestEqual(TEXT("A 10 s flee with no arrival still asks"), static_cast<int32>(Far.Advance(FGFTownsfolkMind::FleeSeconds, FarRng, FarLocation)), static_cast<int32>(EGFTownsfolkPulse::AskHire));
	TestTrue(TEXT("Timeout returns to idle"), Far.State == EGFTownsfolkBehavior::Idle);

	TArray<FGFHireCandidate> Pool;
	Pool.Add(GFTheftTests::Cand(800.f, true, true));
	Pool.Add(GFTheftTests::Cand(200.f, true, true));
	Pool.Add(GFTheftTests::Cand(400.f, true, true));
	Pool.Add(GFTheftTests::Cand(50.f, false, true));
	const FGFHirePlan Plan = FGFTheftRules::PlanHire(Pool, 2);
	TestEqual(TEXT("Three wild dinos are hired"), Plan.Existing.Num(), 3);
	TestEqual(TEXT("Nearest first"), Plan.Existing[0], 1);
	TestEqual(TEXT("Then the next"), Plan.Existing[1], 2);
	TestEqual(TEXT("Then the far one"), Plan.Existing[2], 0);
	TestEqual(TEXT("No spawn when three are in range"), Plan.SpawnCount, 0);

	FGFTheftHeat Heat;
	TestTrue(TEXT("The witness spots the player"), FGFTheftRules::NoteWitness(Heat, TEXT("Rime")));
	TestTrue(TEXT("Spotted"), Heat.State == EGFTheftHeat::Spotted);
	TestTrue(TEXT("The hire starts the hunt"), FGFTheftRules::NoteHire(Heat));
	TestTrue(TEXT("Hunted"), Heat.State == EGFTheftHeat::Hunted);
	TestEqual(TEXT("Hunt clock"), Heat.HuntLeft, FGFTheftRules::HuntSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTheftHireOnePerTheftTest, "GFGame.Theft.Hire.OnePerTheft", GFTheftTests::Flags)
bool FGFTheftHireOnePerTheftTest::RunTest(const FString&)
{
	TestFalse(TEXT("Nobody saw it"), FGFTheftRules::ShouldHire(0, false));
	TestTrue(TEXT("The first witness asks"), FGFTheftRules::ShouldHire(2, false));
	TestFalse(TEXT("The second witness does not hire again"), FGFTheftRules::ShouldHire(2, true));
	TestFalse(TEXT("A later ask stays closed"), FGFTheftRules::ShouldHire(1, true));

	int32 Hires = 0;
	bool bHired = false;
	auto Ask = [&Hires, &bHired]()
	{
		if (FGFTheftRules::ShouldHire(2, bHired))
		{
			bHired = true;
			++Hires;
		}
	};
	Ask();
	Ask();
	TestEqual(TEXT("Two villagers, one hire"), Hires, 1);

	const FGFHirePlan None = FGFTheftRules::PlanHire({}, 2);
	TestEqual(TEXT("No dinos in range spawns two"), None.SpawnCount, 2);
	TestEqual(TEXT("Nothing existing"), None.Existing.Num(), 0);

	TArray<FGFHireCandidate> One;
	One.Add(GFTheftTests::Cand(100.f, true, true));
	One.Add(GFTheftTests::Cand(10.f, true, false));
	const FGFHirePlan OnePlan = FGFTheftRules::PlanHire(One, 2);
	TestEqual(TEXT("One in range is taken"), OnePlan.Existing.Num(), 1);
	TestEqual(TEXT("The out of range dino is not taken"), OnePlan.Existing[0], 0);
	TestEqual(TEXT("One more is spawned to reach two"), OnePlan.SpawnCount, 1);

	TArray<FGFHireCandidate> Two;
	Two.Add(GFTheftTests::Cand(300.f, true, true));
	Two.Add(GFTheftTests::Cand(100.f, true, true));
	const FGFHirePlan TwoPlan = FGFTheftRules::PlanHire(Two, 2);
	TestEqual(TEXT("Two in range need no spawn"), TwoPlan.SpawnCount, 0);
	TestEqual(TEXT("Both are hired"), TwoPlan.Existing.Num(), 2);

	const FGFHirePlan NoSpecies = FGFTheftRules::PlanHire({}, 0);
	TestEqual(TEXT("No species means no spawn"), NoSpecies.SpawnCount, 0);

	FGFTheftHeat Heat;
	TestFalse(TEXT("An unseen break does not spot"), FGFTheftRules::NoteWitness(Heat, NAME_None));
	TestTrue(TEXT("Heat stays clear"), Heat.State == EGFTheftHeat::Clear);
	TestFalse(TEXT("Clear heat does not hire"), FGFTheftRules::NoteHire(Heat));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTheftHeatClearsTest, "GFGame.Theft.Heat.Clears", GFTheftTests::Flags)
bool FGFTheftHeatClearsTest::RunTest(const FString&)
{
	auto Hunt = []()
	{
		FGFTheftHeat Heat;
		FGFTheftRules::NoteWitness(Heat, TEXT("Rime"));
		FGFTheftRules::NoteHire(Heat);
		return Heat;
	};

	FGFTheftHeat ByTime = Hunt();
	TestFalse(TEXT("119 s inside is still hunted"), FGFTheftRules::AdvanceHeat(ByTime, 119.f, true, 2));
	TestTrue(TEXT("Still hunted"), ByTime.State == EGFTheftHeat::Hunted);
	TestTrue(TEXT("The clock clears it"), FGFTheftRules::AdvanceHeat(ByTime, 2.f, true, 2));
	TestTrue(TEXT("Clear after the clock"), ByTime.State == EGFTheftHeat::Clear);
	TestFalse(TEXT("A second tick does not clear again"), FGFTheftRules::AdvanceHeat(ByTime, 5.f, true, 2));

	FGFTheftHeat ByLeave = Hunt();
	TestFalse(TEXT("59 s outside is still hunted"), FGFTheftRules::AdvanceHeat(ByLeave, 59.f, false, 2));
	TestEqual(TEXT("Away so far"), ByLeave.Away, 59.f);
	TestTrue(TEXT("60 s outside clears"), FGFTheftRules::AdvanceHeat(ByLeave, 1.f, false, 2));
	TestTrue(TEXT("Clear after leaving"), ByLeave.State == EGFTheftHeat::Clear);

	FGFTheftHeat Reset = Hunt();
	TestFalse(TEXT("50 s outside"), FGFTheftRules::AdvanceHeat(Reset, 50.f, false, 2));
	TestFalse(TEXT("Stepping back in"), FGFTheftRules::AdvanceHeat(Reset, 1.f, true, 2));
	TestEqual(TEXT("Away resets"), Reset.Away, 0.f);
	TestFalse(TEXT("Another 50 s outside is not enough"), FGFTheftRules::AdvanceHeat(Reset, 50.f, false, 2));
	TestTrue(TEXT("Still hunted after the reset"), Reset.State == EGFTheftHeat::Hunted);

	FGFTheftHeat ByKill = Hunt();
	TestTrue(TEXT("No hired dino left clears at once"), FGFTheftRules::AdvanceHeat(ByKill, 0.1f, true, 0));
	TestTrue(TEXT("Clear after the hire dies"), ByKill.State == EGFTheftHeat::Clear);

	FGFTameMachine Dino;
	Dino.ForceHostile(TEXT("Thief"), true, 1.f);
	TestEqual(TEXT("The hired dino is hostile"), static_cast<int32>(Dino.State), static_cast<int32>(EGFTameState::Hostile));
	Dino.ReleaseHire();
	TestEqual(TEXT("Heat clear stands the dino down"), static_cast<int32>(Dino.State), static_cast<int32>(EGFTameState::Wild));
	TestTrue(TEXT("The thief is no longer the target"), Dino.TargetKey.IsEmpty());
	return true;
}

#endif
