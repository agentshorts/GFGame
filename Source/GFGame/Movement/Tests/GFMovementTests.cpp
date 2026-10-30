// Stamina, sprint and dash. Pure steps, plus the player CDO's base run speed.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Movement;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFKindleRing.h"
#include "GFMovementPolicy.h"
#include "GFMovementTuning.h"
#include "GFStaminaSim.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFMovementTestsPrivate
{
	constexpr EAutomationTestFlags GFMovementFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	const UGFMovementTuning& GFMovementTuningCDO()
	{
		return *GetDefault<UGFMovementTuning>();
	}

	FGFStaminaState GFMovementFullState()
	{
		FGFStaminaState State;
		State.ResetFull(GFMovementTuningCDO());
		return State;
	}

	FGFStaminaCommand GFMovementCommand(float DeltaSeconds)
	{
		FGFStaminaCommand Command;
		Command.DeltaSeconds = DeltaSeconds;
		Command.bGrounded = true;
		Command.bStaminaEnabled = true;
		Command.CameraForward = FVector::ForwardVector;
		return Command;
	}

	bool GFMovementNear(float A, float B)
	{
		return FMath::IsNearlyEqual(A, B, 0.2f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementRegenDelayTest, "GFGame.Movement.RegenDelay", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementRegenDelayTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Max stamina"), Tuning.MaxStamina, 200.f);
	TestEqual(TEXT("Regen per second"), Tuning.RegenPerSecond, 25.f);
	TestEqual(TEXT("Regen delay"), Tuning.RegenDelay, 0.8f);

	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Sprint = GFMovementCommand(1.f);
	Sprint.bSprintHeld = true;
	State = GFStaminaSim::Step(State, Sprint, Tuning);
	TestTrue(TEXT("One second of sprint spends 15"), GFMovementNear(State.Stamina, 185.f));
	TestTrue(TEXT("Sprint is on"), State.bSprinting);
	TestTrue(TEXT("Spend arms the delay"), GFMovementNear(State.RegenDelayLeft, 0.8f));

	FGFStaminaCommand Wait = GFMovementCommand(0.79f);
	State = GFStaminaSim::Step(State, Wait, Tuning);
	TestTrue(TEXT("No regen inside the delay"), GFMovementNear(State.Stamina, 185.f));
	TestTrue(TEXT("Delay still running"), State.RegenDelayLeft > 0.f);

	Wait.DeltaSeconds = 0.41f;
	State = GFStaminaSim::Step(State, Wait, Tuning);
	TestTrue(TEXT("Regen is 25/s after the delay"), GFMovementNear(State.Stamina, 195.f));
	TestTrue(TEXT("Delay has elapsed"), State.RegenDelayLeft <= 0.f);

	Sprint.DeltaSeconds = 1.f;
	State = GFStaminaSim::Step(State, Sprint, Tuning);
	TestTrue(TEXT("A new spend stops regen"), GFMovementNear(State.Stamina, 180.f));
	Wait.DeltaSeconds = 0.4f;
	State = GFStaminaSim::Step(State, Wait, Tuning);
	TestTrue(TEXT("Fresh delay blocks regen"), GFMovementNear(State.Stamina, 180.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementSprintDrainTest, "GFGame.Movement.SprintDrain", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementSprintDrainTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Sprint cost"), Tuning.SprintCostPerSecond, 15.f);
	TestEqual(TEXT("Sprint multiplier"), Tuning.SprintSpeedMultiplier, 1.4f);
	TestEqual(TEXT("Base run speed"), Tuning.BaseRunSpeed, 500.f);

	const AGFCharacter* Character = GetDefault<AGFCharacter>();
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("Player movement"), Move))
	{
		return false;
	}
	TestTrue(TEXT("Custom movement component"), Move->IsA(UGFCharacterMovementComponent::StaticClass()));
	TestEqual(TEXT("MaxWalkSpeed stays the base run"), Move->MaxWalkSpeed, 500.f);

	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Sprint = GFMovementCommand(2.f);
	Sprint.bSprintHeld = true;
	State = GFStaminaSim::Step(State, Sprint, Tuning);
	TestTrue(TEXT("Two seconds spend 30"), GFMovementNear(State.Stamina, 170.f));
	TestTrue(TEXT("Sprinting"), State.bSprinting);
	TestTrue(TEXT("Sprint speed is 1.4x the live walk speed"), FMath::IsNearlyEqual(GFStaminaSim::HorizontalSpeed(State, Tuning, Tuning.BaseRunSpeed), 700.f, 0.05f));
	TestEqual(TEXT("Walk speed argument is not rewritten"), Tuning.BaseRunSpeed, 500.f);

	int32 Steps = 0;
	while (State.Stamina > 0.f && Steps < 40)
	{
		Sprint.DeltaSeconds = 1.f;
		State = GFStaminaSim::Step(State, Sprint, Tuning);
		++Steps;
	}
	TestTrue(TEXT("Pool hits empty"), State.Stamina <= 0.05f);
	TestTrue(TEXT("Empty sprint latches off"), State.bSprintBlocked);
	TestTrue(TEXT("The emptying step still counted as a sprint"), State.bSprinting);

	State = GFStaminaSim::Step(State, Sprint, Tuning);
	TestFalse(TEXT("Holding Shift on empty does not sprint"), State.bSprinting);
	TestTrue(TEXT("Latch holds while Shift is held"), State.bSprintBlocked);

	FGFStaminaCommand Release = GFMovementCommand(0.05f);
	State = GFStaminaSim::Step(State, Release, Tuning);
	TestFalse(TEXT("Releasing Shift clears the latch"), State.bSprintBlocked);
	Sprint.DeltaSeconds = 0.2f;
	State = GFStaminaSim::Step(State, Sprint, Tuning);
	TestTrue(TEXT("A new hold can sprint once stamina has returned"), State.bSprinting);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementAirDashTest, "GFGame.Movement.AirDashOnce", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementAirDashTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Dash cost"), Tuning.DashCost, 25.f);
	TestEqual(TEXT("Dash speed"), Tuning.DashSpeed, 900.f);
	TestEqual(TEXT("Dash duration"), Tuning.DashDuration, 0.2f);
	TestEqual(TEXT("One air dash"), Tuning.AirDashesPerAirtime, 1);
	TestEqual(TEXT("Gravity pause"), Tuning.DashGravityScale, 0.f);

	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Air = GFMovementCommand(0.05f);
	Air.bGrounded = false;
	Air.bDashPressed = true;
	Air.MoveDirection = FVector(0.0, 1.0, 0.0);
	State = GFStaminaSim::Step(State, Air, Tuning);
	TestTrue(TEXT("Air dash starts"), State.bDashing);
	TestEqual(TEXT("One charge used"), State.AirDashesUsed, 1);
	TestTrue(TEXT("Costs 25"), GFMovementNear(State.Stamina, 175.f));
	TestTrue(TEXT("Burst follows the move input"), State.DashDir.Y > 0.9);
	TestEqual(TEXT("Burst speed"), GFStaminaSim::HorizontalSpeed(State, Tuning, Tuning.BaseRunSpeed), 900.f);
	TestEqual(TEXT("Gravity scale during the burst"), GFStaminaSim::GravityScale(State, Tuning), 0.f);

	FGFStaminaCommand Coast = GFMovementCommand(0.05f);
	Coast.bGrounded = false;
	int32 Guard = 0;
	while (State.bDashing && Guard < 20)
	{
		State = GFStaminaSim::Step(State, Coast, Tuning);
		++Guard;
	}
	TestFalse(TEXT("Burst ends"), State.bDashing);
	TestEqual(TEXT("Charge stays spent"), State.AirDashesUsed, 1);

	FGFStaminaCommand Again = Coast;
	Again.bDashPressed = true;
	const float Before = State.Stamina;
	State = GFStaminaSim::Step(State, Again, Tuning);
	TestFalse(TEXT("No second air dash"), State.bDashing);
	TestEqual(TEXT("Charge unchanged"), State.AirDashesUsed, 1);
	TestTrue(TEXT("Refused dash is free"), GFMovementNear(State.Stamina, Before));

	FGFStaminaState Aimed = GFMovementFullState();
	FGFStaminaCommand NoInput = GFMovementCommand(0.05f);
	NoInput.bGrounded = false;
	NoInput.bDashPressed = true;
	NoInput.CameraForward = FVector(-1.0, 0.0, 0.4);
	Aimed = GFStaminaSim::Step(Aimed, NoInput, Tuning);
	TestTrue(TEXT("No input uses camera yaw"), Aimed.DashDir.X < -0.9);
	TestTrue(TEXT("Dash stays horizontal"), FMath::Abs(Aimed.DashDir.Z) < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementLandingResetTest, "GFGame.Movement.LandingReset", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementLandingResetTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Air = GFMovementCommand(0.05f);
	Air.bGrounded = false;
	Air.bDashPressed = true;
	Air.MoveDirection = FVector(1.0, 0.0, 0.0);
	State = GFStaminaSim::Step(State, Air, Tuning);
	TestEqual(TEXT("Spent in the air"), State.AirDashesUsed, 1);

	FGFStaminaCommand Coast = GFMovementCommand(0.05f);
	Coast.bGrounded = false;
	int32 Guard = 0;
	while (State.bDashing && Guard < 20)
	{
		State = GFStaminaSim::Step(State, Coast, Tuning);
		++Guard;
	}

	FGFStaminaCommand Land = GFMovementCommand(0.05f);
	State = GFStaminaSim::Step(State, Land, Tuning);
	TestEqual(TEXT("Landing clears the air charge"), State.AirDashesUsed, 0);
	TestTrue(TEXT("Now on the ground"), State.bWasGrounded);

	FGFStaminaCommand Hop = GFMovementCommand(0.05f);
	Hop.bGrounded = false;
	Hop.bDashPressed = true;
	Hop.MoveDirection = FVector(1.0, 0.0, 0.0);
	State = GFStaminaSim::Step(State, Hop, Tuning);
	TestTrue(TEXT("Next airtime can dash"), State.bDashing);
	TestEqual(TEXT("One charge in the new airtime"), State.AirDashesUsed, 1);
	TestTrue(TEXT("Second trip costs another 25"), GFMovementNear(State.Stamina, 150.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementGroundCooldownTest, "GFGame.Movement.GroundDashCooldown", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementGroundCooldownTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Ground cooldown"), Tuning.GroundDashCooldown, 0.5f);

	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Dash = GFMovementCommand(0.05f);
	Dash.bDashPressed = true;
	Dash.MoveDirection = FVector(1.0, 0.0, 0.0);
	State = GFStaminaSim::Step(State, Dash, Tuning);
	TestTrue(TEXT("Ground dash starts"), State.bDashing);
	TestTrue(TEXT("Cooldown started"), State.GroundCooldownLeft > 0.4f && State.GroundCooldownLeft < 0.5f);
	TestTrue(TEXT("Costs 25"), GFMovementNear(State.Stamina, 175.f));

	FGFStaminaCommand Coast = GFMovementCommand(0.44f);
	State = GFStaminaSim::Step(State, Coast, Tuning);
	TestFalse(TEXT("Burst over"), State.bDashing);
	TestTrue(TEXT("Still cooling"), State.GroundCooldownLeft > 0.f);

	FGFStaminaCommand Early = GFMovementCommand(0.005f);
	Early.bDashPressed = true;
	const float Pooled = State.Stamina;
	State = GFStaminaSim::Step(State, Early, Tuning);
	TestFalse(TEXT("No dash inside the cooldown"), State.bDashing);
	TestTrue(TEXT("Refused ground dash is free"), GFMovementNear(State.Stamina, Pooled));

	Coast.DeltaSeconds = 0.02f;
	State = GFStaminaSim::Step(State, Coast, Tuning);
	TestTrue(TEXT("Cooldown finished"), State.GroundCooldownLeft <= 0.f);
	Dash.DeltaSeconds = 0.05f;
	State = GFStaminaSim::Step(State, Dash, Tuning);
	TestTrue(TEXT("Dash again after 0.5s"), State.bDashing);
	TestTrue(TEXT("Second dash costs 25"), GFMovementNear(State.Stamina, 150.f));

	FGFStaminaState Separate = GFMovementFullState();
	Separate = GFStaminaSim::Step(Separate, Dash, Tuning);
	FGFStaminaCommand Wait = GFMovementCommand(0.05f);
	int32 Guard = 0;
	while (Separate.bDashing && Guard < 20)
	{
		Separate = GFStaminaSim::Step(Separate, Wait, Tuning);
		++Guard;
	}
	TestTrue(TEXT("Ground cooldown outlasts the burst"), Separate.GroundCooldownLeft > 0.2f);
	FGFStaminaCommand Air = GFMovementCommand(0.05f);
	Air.bGrounded = false;
	Air.bDashPressed = true;
	Air.MoveDirection = FVector(1.0, 0.0, 0.0);
	Separate = GFStaminaSim::Step(Separate, Air, Tuning);
	TestTrue(TEXT("Air dash ignores the ground cooldown"), Separate.bDashing);
	TestEqual(TEXT("Air charge used"), Separate.AirDashesUsed, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementMiniGameTest, "GFGame.Movement.MiniGameOptOut", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementMiniGameTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	TestTrue(TEXT("Beat Hop map"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_Beat")));
	TestTrue(TEXT("PIE Beat Hop map"), GFMovementPolicy::IsMiniGameMapName(TEXT("UEDPIE_0_L_Beat")));
	TestTrue(TEXT("Valley map"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_Valley")));
	TestTrue(TEXT("Arena map"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_Arena")));
	TestTrue(TEXT("Runway map"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_Runway")));
	TestFalse(TEXT("Hub map"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_Hub")));
	TestFalse(TEXT("Net test map"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_NetTest")));
	TestFalse(TEXT("Empty map name"), GFMovementPolicy::IsMiniGameMapName(TEXT("")));

	TestFalse(TEXT("Mini-game defaults off"), GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Beat"), false, false));
	TestTrue(TEXT("Opt-in turns a mini-game on"), GFMovementPolicy::IsStaminaEnabled(TEXT("UEDPIE_0_L_Beat"), true, true));
	TestFalse(TEXT("Opt-in can stay off"), GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Valley"), true, false));
	TestTrue(TEXT("Hub is on without an opt-in"), GFMovementPolicy::IsStaminaEnabled(TEXT("L_Hub"), false, false));

	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Off = GFMovementCommand(2.f);
	Off.bStaminaEnabled = GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Arena"), false, false);
	Off.bSprintHeld = true;
	Off.bDashPressed = true;
	State = GFStaminaSim::Step(State, Off, Tuning);
	TestFalse(TEXT("Feature flag is off"), Off.bStaminaEnabled);
	TestFalse(TEXT("No sprint in a mini-game"), State.bSprinting);
	TestFalse(TEXT("No dash in a mini-game"), State.bDashing);
	TestTrue(TEXT("Pool stays full"), GFMovementNear(State.Stamina, Tuning.MaxStamina));
	TestEqual(TEXT("Speed stays the base run"), GFStaminaSim::HorizontalSpeed(State, Tuning, Tuning.BaseRunSpeed), Tuning.BaseRunSpeed);

	FGFStaminaCommand On = Off;
	On.bStaminaEnabled = GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Arena"), true, true);
	On.bDashPressed = false;
	On.DeltaSeconds = 1.f;
	State = GFStaminaSim::Step(State, On, Tuning);
	TestTrue(TEXT("Opt-in enables the step"), On.bStaminaEnabled);
	TestTrue(TEXT("Opt-in sprint spends stamina"), State.bSprinting && GFMovementNear(State.Stamina, 185.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementKindleRingTest, "GFGame.Movement.KindleRingInfinite", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementKindleRingTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestTrue(TEXT("Hearth radius"), Tuning.KindleRingRadius >= 100.f);

	const AGFKindleRing* Ring = GetDefault<AGFKindleRing>();
	if (!TestNotNull(TEXT("Kindle ring class"), Ring))
	{
		return false;
	}
	TestTrue(TEXT("Ring replicates"), Ring->GetIsReplicated());
	const USphereComponent* Sphere = Ring->FindComponentByClass<USphereComponent>();
	TestNotNull(TEXT("Ring has a sphere"), Sphere);
	TestTrue(TEXT("Default hearth radius"), FMath::IsNearlyEqual(Ring->GetRadius(), 400.f, 0.1f));

	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Sprint = GFMovementCommand(10.f);
	Sprint.bSprintHeld = true;
	Sprint.bInsideKindleRing = true;
	State = GFStaminaSim::Step(State, Sprint, Tuning);
	TestTrue(TEXT("Sprint still works inside the ring"), State.bSprinting);
	TestTrue(TEXT("Stamina stays full"), GFMovementNear(State.Stamina, Tuning.MaxStamina));

	FGFStaminaCommand Air = GFMovementCommand(0.05f);
	Air.bGrounded = false;
	Air.bDashPressed = true;
	Air.bInsideKindleRing = true;
	Air.MoveDirection = FVector(1.0, 0.0, 0.0);
	State = GFStaminaSim::Step(State, Air, Tuning);
	TestTrue(TEXT("Dash still works inside the ring"), State.bDashing);
	TestEqual(TEXT("Air charge is still spent"), State.AirDashesUsed, 1);
	TestTrue(TEXT("Dash did not drain the pool"), GFMovementNear(State.Stamina, Tuning.MaxStamina));

	FGFStaminaCommand Coast = GFMovementCommand(0.05f);
	Coast.bGrounded = false;
	Coast.bInsideKindleRing = true;
	int32 Guard = 0;
	while (State.bDashing && Guard < 20)
	{
		State = GFStaminaSim::Step(State, Coast, Tuning);
		++Guard;
	}
	FGFStaminaCommand Again = Coast;
	Again.bDashPressed = true;
	State = GFStaminaSim::Step(State, Again, Tuning);
	TestFalse(TEXT("Infinite stamina is not an infinite air dash"), State.bDashing);
	TestTrue(TEXT("Pool still full"), GFMovementNear(State.Stamina, Tuning.MaxStamina));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementArcTest, "GFGame.Movement.ArcHidesWhenFull", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementArcTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Hide epsilon"), Tuning.HudHideEpsilon, 0.5f);
	TestFalse(TEXT("Full pool hides the arc"), GFStaminaSim::ShouldShowArc(Tuning.MaxStamina, Tuning.MaxStamina, true, Tuning.HudHideEpsilon));
	TestTrue(TEXT("A spent point shows the arc"), GFStaminaSim::ShouldShowArc(Tuning.MaxStamina - 1.f, Tuning.MaxStamina, true, Tuning.HudHideEpsilon));
	TestFalse(TEXT("Mini-games hide the arc"), GFStaminaSim::ShouldShowArc(0.f, Tuning.MaxStamina, false, Tuning.HudHideEpsilon));
	TestFalse(TEXT("Disabled and full stays hidden"), GFStaminaSim::ShouldShowArc(Tuning.MaxStamina, Tuning.MaxStamina, false, Tuning.HudHideEpsilon));
	return true;
}

#endif
