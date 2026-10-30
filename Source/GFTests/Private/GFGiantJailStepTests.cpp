// Big Grumble's jail-edge wait with the capped move: safe land stands between him and the jail, so he has to slide
// around it. Every step stays within one step length and clear of the land, he never gets into the box, he still
// reaches the edge, waits out the dwell, and then takes the free player.
#include "GFGiantTypes.h"
#include "GFSafeZone.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFGiantJailStepTest
{
	static FGFGiantPlayerView Who(const TCHAR* Key, const FVector& At, double Play, int64 FirstSeen)
	{
		FGFGiantPlayerView P;
		P.Key = Key;
		P.Name = FString(Key) + TEXT("Name");
		P.Location = At;
		P.PlaySeconds = Play;
		P.FirstSeenTicks = FirstSeen;
		P.RunSpeed = 600.f;
		P.GroupId = FName(TEXT("Wild"));
		return P;
	}

	static FGFGiantLines Script()
	{
		FGFGiantLines L;
		L.ByPhase.Add(EGFGiantLinePhase::Spotted, { { TEXT("S"), TEXT("Hi") } });
		L.ByPhase.Add(EGFGiantLinePhase::Chase, { { TEXT("C"), TEXT("Run") } });
		L.ByPhase.Add(EGFGiantLinePhase::Circling, { { TEXT("R"), TEXT("Wait") } });
		L.ByPhase.Add(EGFGiantLinePhase::Leaving, { { TEXT("L"), TEXT("Bye") } });
		return L;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantJailStepTest, "GFGame.Giant.Handoff.JailCappedSteps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantJailStepTest::RunTest(const FString&)
{
	using namespace GFGiantJailStepTest;
	FGFGiantTuning T;
	T.ShoveRange = 0.f;
	T.bEatIdleTarget = false;
	T.EventSeconds = 120.f;
	T.JailDwellSeconds = 2.f;

	// Jail at the origin. A land disc sits off its +X side, a little off the axis so he can slide around it.
	const FBox Box(FVector(-300.f, -300.f, -100.f), FVector(300.f, 300.f, 500.f));
	FGFSafeRegion R;
	R.Id = FName(TEXT("Land"));
	R.Center = FVector(2400.f, 400.f, 0.f);
	R.Members = 1;
	R.BaseRadius = 1500.f;
	R.RadiusPerExtraMember = 0.f;
	R.MaxRadius = 1500.f;
	const FGFSafeCircles Land({ R });

	// The jailed player is the newest; the free player is far off on the other side.
	const TArray<FGFGiantPlayerView> P = {
		Who(TEXT("J"), FVector(0.f, 0.f, 100.f), 1.0, 9),
		Who(TEXT("F"), FVector(-9000.f, 0.f, 0.f), 500.0, 1) };

	FGFGiantSim Sim;
	Sim.NoEntry = { Box };
	FGFGiantStepOut Out;
	Sim.Begin(T, Script(), Land, P, false, 11, Out);
	TestEqual(TEXT("Goes for the jailed player"), Sim.TargetKey, FString(TEXT("J")));
	// Start him past the land, so the straight line to the jail edge crosses it.
	Sim.Location = FVector(5600.f, 0.f, 0.f);
	TestTrue(TEXT("Starts clear of the land"), FGFSafeZone::IsClear(Land, Sim.Location, T.GiantRadius));

	int32 LongSteps = 0, OnLand = 0, InBox = 0;
	float Longest = 0.f, Closest = TNumericLimits<float>::Max(), SwitchedAt = -1.f;
	for (int32 Step = 0; Step < 600 && Sim.IsActive(); ++Step)
	{
		const FVector Before = Sim.Location;
		FGFGiantStepOut O;
		Sim.Step(0.1f, P, O);
		const float Moved = FVector::Dist2D(Before, Sim.Location);
		Longest = FMath::Max(Longest, Moved);
		LongSteps += Moved > Sim.LastSpeed * 0.1f + 1.f ? 1 : 0;
		OnLand += FGFSafeZone::IsClear(Land, Sim.Location, T.GiantRadius - 1.f) ? 0 : 1;
		const float Gap = FMath::Max(FMath::Max(FMath::Abs(Sim.Location.X) - 300.f, 0.f), FMath::Max(FMath::Abs(Sim.Location.Y) - 300.f, 0.f));
		InBox += Gap < T.GiantRadius - 1.f ? 1 : 0;
		if (SwitchedAt < 0.f)
		{
			Closest = FMath::Min(Closest, Gap);
			if (Sim.TargetKey == TEXT("F"))
			{
				SwitchedAt = Sim.Elapsed;
				break;
			}
		}
	}
	TestEqual(TEXT("Every step within one step length"), LongSteps, 0);
	TestEqual(TEXT("Never on the land"), OnLand, 0);
	TestEqual(TEXT("Never inside the jail (body included)"), InBox, 0);
	TestTrue(TEXT("Came up to the jail edge"), Closest < T.GiantRadius + 80.f);
	TestTrue(TEXT("Waited out the dwell, then switched"), SwitchedAt >= T.JailDwellSeconds - 0.01f && SwitchedAt < 40.f);
	TestEqual(TEXT("Then hunts the free player"), Sim.TargetKey, FString(TEXT("F")));
	AddInfo(FString::Printf(TEXT("Jail behind land: longest move %.1f, closest %.1f cm, switched at %.2f s"), Longest, Closest, SwitchedAt));
	return true;
}

#endif
