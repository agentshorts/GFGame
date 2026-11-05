// Live landing target (issue 342): the predicted landing follows the flight as it changes, steering included.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Movement.LandingTarget;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFStaminaSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFLandingTargetTestsPrivate
{
	static constexpr EAutomationTestFlags LandingTargetFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A flat floor at FloorZ, the way the world trace answers over open ground. */
	static FGFLandingPrediction LandingOnFloor(const FGFFlightProbe& Probe, double FloorZ = 0.0)
	{
		return GFTraversalSim::PredictFlightLanding(Probe, [FloorZ](const FVector& From, const FVector& To, FVector& OutHit)
		{
			if (From.Z < FloorZ || To.Z >= FloorZ)
			{
				return false;
			}
			const double Alpha = (From.Z - FloorZ) / (From.Z - To.Z);
			OutHit = FMath::Lerp(From, To, Alpha);
			return true;
		});
	}

	static FGFFlightProbe LandingProbe(const FVector& Origin, const FVector& Velocity)
	{
		FGFFlightProbe Probe;
		Probe.Origin = Origin;
		Probe.Velocity = Velocity;
		Probe.GravityZ = -980.f;
		Probe.MaxLateralSpeed = 600.f;
		Probe.Lookahead = 3.f;
		Probe.SampleSeconds = 0.05f;
		return Probe;
	}

	/** Flies the real flight forward Seconds with the same steering, as the movement would between HUD frames. */
	static void LandingFly(FGFFlightProbe& Probe, float Seconds)
	{
		const int32 Steps = 20;
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			GFTraversalSim::StepFlight(Probe.Origin, Probe.Velocity, Probe.LateralAccel, Probe.GravityZ, Probe.MaxLateralSpeed, Seconds / Steps);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLandingTargetBallisticTest, "GFGame.Movement.LandingTarget.Ballistic", GFLandingTargetTestsPrivate::LandingTargetFlags)
bool FGFLandingTargetBallisticTest::RunTest(const FString&)
{
	using namespace GFLandingTargetTestsPrivate;
	const FVector Origin(0.0, 0.0, 300.0);
	const FVector Velocity(200.0, 0.0, 350.0);
	FVector Position = Origin;
	FVector Moving = Velocity;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		GFTraversalSim::StepFlight(Position, Moving, FVector::ZeroVector, -980.f, 600.f, 0.1f);
	}
	TestTrue(TEXT("No steering: the step is the plain arc"), FVector::Dist(Position, GFTraversalSim::BallisticPoint(Origin, Velocity, -980.f, 0.7f)) < 0.01);

	const FGFLandingPrediction Plane = GFTraversalSim::PredictLandingOnPlane(Origin, Velocity, -980.f, 0.f, 3.f);
	const FGFLandingPrediction Flight = LandingOnFloor(LandingProbe(Origin, Velocity));
	TestTrue(TEXT("Both land"), Plane.bHit && Flight.bHit);
	TestTrue(*FString::Printf(TEXT("Same spot as the v1 arc (%.2f cm apart)"), FVector::Dist(Plane.Point, Flight.Point)), FVector::Dist(Plane.Point, Flight.Point) < 3.0);
	TestTrue(TEXT("Same time"), FMath::IsNearlyEqual(Plane.Time, Flight.Time, 0.02f));

	FGFFlightProbe Short = LandingProbe(Origin, FVector(0.0, 0.0, 800.0));
	Short.Lookahead = 0.3f;
	TestFalse(TEXT("A short lookahead can miss"), LandingOnFloor(Short).bHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLandingTargetLiveTest, "GFGame.Movement.LandingTarget.Live", GFLandingTargetTestsPrivate::LandingTargetFlags)
bool FGFLandingTargetLiveTest::RunTest(const FString&)
{
	using namespace GFLandingTargetTestsPrivate;
	FGFFlightProbe Probe = LandingProbe(FVector(0.0, 0.0, 200.0), FVector(300.0, 0.0, 500.0));
	const FGFLandingPrediction Takeoff = LandingOnFloor(Probe);
	if (!TestTrue(TEXT("Lands from takeoff"), Takeoff.bHit))
	{
		return false;
	}

	// Nothing changes in the air: asking again mid-flight gives the same spot (the target does not drift on its own).
	FGFFlightProbe Mid = Probe;
	LandingFly(Mid, 0.4f);
	const FGFLandingPrediction Again = LandingOnFloor(Mid);
	TestTrue(*FString::Printf(TEXT("Mid-flight re-predict is stable (%.2f cm)"), FVector::Dist(Again.Point, Takeoff.Point)), Again.bHit && FVector::Dist(Again.Point, Takeoff.Point) < 3.0);
	TestTrue(TEXT("Less time left"), Again.Time < Takeoff.Time - 0.3f);

	// Steering sideways mid-flight: the target moves with it, frame by frame.
	FGFFlightProbe Steer = Mid;
	Steer.LateralAccel = FVector(0.0, 1200.0, 0.0);
	const FGFLandingPrediction Steered = LandingOnFloor(Steer);
	TestTrue(*FString::Printf(TEXT("Steering right moves the target right (Y %.1f)"), Steered.Point.Y), Steered.bHit && Steered.Point.Y > 50.0);
	LandingFly(Steer, 0.2f);
	const FGFLandingPrediction Later = LandingOnFloor(Steer);
	TestTrue(TEXT("Still steering: the next frame agrees with where it was heading"), Later.bHit && FVector::Dist(Later.Point, Steered.Point) < 5.0);
	Steer.LateralAccel = FVector::ZeroVector;
	const FGFLandingPrediction LetGo = LandingOnFloor(Steer);
	TestTrue(TEXT("Letting go mid-air moves it back in (less far right than held)"), LetGo.bHit && LetGo.Point.Y < Later.Point.Y - 1.0);

	// A velocity change in the air (a dash or a boost) re-aims it at once.
	FGFFlightProbe Dash = Mid;
	Dash.Velocity.X += 600.0;
	const FGFLandingPrediction Dashed = LandingOnFloor(Dash);
	TestTrue(TEXT("A mid-air speed change moves the target ahead"), Dashed.bHit && Dashed.Point.X > Again.Point.X + 100.0);

	// Air steering cannot push past the run cap (or the speed already carried).
	FGFFlightProbe Capped = Mid;
	Capped.LateralAccel = FVector(5000.0, 0.0, 0.0);
	Capped.MaxLateralSpeed = 300.f;
	const FGFLandingPrediction CappedHit = LandingOnFloor(Capped);
	TestTrue(TEXT("Pushing forward at the cap adds nothing"), CappedHit.bHit && FVector::Dist(CappedHit.Point, Again.Point) < 3.0);

	// A higher floor (a tile on a step) is met sooner.
	const FGFLandingPrediction Step = LandingOnFloor(Mid, 100.0);
	TestTrue(TEXT("A raised floor lands sooner"), Step.bHit && Step.Time < Again.Time);

	// Softer gravity (a stamina float) carries further.
	FGFFlightProbe Floaty = Mid;
	Floaty.GravityZ = -490.f;
	TestTrue(TEXT("Lower gravity lands further"), LandingOnFloor(Floaty).Point.X > Again.Point.X + 50.0);
	return true;
}

#endif
