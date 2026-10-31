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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementDashGripTest, "GFGame.Movement.DashGrip", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementDashGripTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Grip loss"), Tuning.DashGripSpeedLoss, 0.6f);
	TestEqual(TEXT("Grip time"), Tuning.DashGripMaxSeconds, 2.f);
	TestEqual(TEXT("Grip drain"), Tuning.DashGripDrainPerSecond, 10.f);

	const float Keep = Tuning.DashSpeed * (1.f - Tuning.DashGripSpeedLoss);
	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Dash = GFMovementCommand(0.05f);
	Dash.bGrounded = false;
	Dash.bDashPressed = true;
	Dash.MoveDirection = FVector(1.0, 0.0, 0.0);
	State = GFStaminaSim::Step(State, Dash, Tuning);
	TestTrue(TEXT("Dash is up before the hit"), State.bDashing);

	FGFStaminaCommand Glance = GFMovementCommand(0.05f);
	Glance.bGrounded = false;
	Glance.Contact.bDashImpact = true;
	Glance.Contact.ImpactNormal = FVector(-1.0, 0.0, 0.0);
	Glance.Contact.SpeedBefore = Tuning.DashSpeed;
	Glance.Contact.SpeedAfter = Keep;
	FGFStaminaState Glanced = GFStaminaSim::Step(State, Glance, Tuning);
	TestFalse(TEXT("Losing exactly 60% does not cling"), Glanced.bGripping);
	TestTrue(TEXT("The dash continues"), Glanced.bDashing);

	FGFStaminaCommand Floor = Glance;
	Floor.Contact.SpeedAfter = 0.f;
	Floor.Contact.ImpactNormal = FVector(0.0, 0.0, 1.0);
	FGFStaminaState Floored = GFStaminaSim::Step(State, Floor, Tuning);
	TestFalse(TEXT("A floor is not a cling"), Floored.bGripping);

	FGFStaminaCommand Hit = Glance;
	Hit.Contact.SpeedAfter = Keep - 1.f;
	State = GFStaminaSim::Step(State, Hit, Tuning);
	TestTrue(TEXT("Losing more than 60% clings"), State.bGripping);
	TestFalse(TEXT("The dash ends on the wall"), State.bDashing);
	TestTrue(TEXT("Cling drains 10/s"), GFMovementNear(State.Stamina, 174.5f));
	TestTrue(TEXT("Cling budget remains"), State.GripTimeLeft > 1.9f && State.GripTimeLeft < 2.f);

	FGFStaminaCommand Hold = GFMovementCommand(1.95f);
	Hold.bGrounded = false;
	State = GFStaminaSim::Step(State, Hold, Tuning);
	TestFalse(TEXT("Cling ends at 2s"), State.bGripping);
	TestTrue(TEXT("Two seconds of cling cost 20"), GFMovementNear(State.Stamina, 155.f));

	FGFStaminaState Kindle = GFMovementFullState();
	Kindle = GFStaminaSim::Step(Kindle, Dash, Tuning);
	FGFStaminaCommand KindleHit = Hit;
	KindleHit.bInsideKindleRing = true;
	Kindle = GFStaminaSim::Step(Kindle, KindleHit, Tuning);
	TestTrue(TEXT("Hearth still clings"), Kindle.bGripping);
	TestTrue(TEXT("Hearth cling is free"), GFMovementNear(Kindle.Stamina, Tuning.MaxStamina));

	FGFStaminaState Empty = GFMovementFullState();
	Empty.bGripping = true;
	Empty.GripTimeLeft = 2.f;
	Empty.Stamina = 1.f;
	Empty.GripNormal = FVector(1.0, 0.0, 0.0);
	FGFStaminaCommand Drain = GFMovementCommand(0.2f);
	Drain.bGrounded = false;
	Empty = GFStaminaSim::Step(Empty, Drain, Tuning);
	TestFalse(TEXT("An empty pool drops the cling"), Empty.bGripping);
	TestTrue(TEXT("The cling spent the last point"), GFMovementNear(Empty.Stamina, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementWallKickTest, "GFGame.Movement.WallKickRefundsAirDash", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementWallKickTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Dash = GFMovementCommand(0.05f);
	Dash.bGrounded = false;
	Dash.bDashPressed = true;
	State = GFStaminaSim::Step(State, Dash, Tuning);
	TestEqual(TEXT("Air dash spent"), State.AirDashesUsed, 1);

	FGFStaminaCommand Hit = GFMovementCommand(0.05f);
	Hit.bGrounded = false;
	Hit.Contact.bDashImpact = true;
	Hit.Contact.ImpactNormal = FVector(1.0, 0.0, 0.0);
	Hit.Contact.SpeedBefore = Tuning.DashSpeed;
	Hit.Contact.SpeedAfter = 100.f;
	State = GFStaminaSim::Step(State, Hit, Tuning);
	TestTrue(TEXT("Gripping the wall"), State.bGripping);

	FGFStaminaCommand Kick = GFMovementCommand(0.05f);
	Kick.bGrounded = false;
	Kick.bJumpPressed = true;
	State = GFStaminaSim::Step(State, Kick, Tuning);
	TestFalse(TEXT("Jump leaves the wall"), State.bGripping);
	TestEqual(TEXT("Air dash is refunded"), State.AirDashesUsed, 0);
	TestTrue(TEXT("Kick pushes off the wall"), State.bOverrideVelocity && FVector::DotProduct(State.OverrideVelocity, FVector(1.0, 0.0, 0.0)) > 0.0);
	TestTrue(TEXT("Kick has lift"), State.OverrideVelocity.Z > 0.0);

	FGFStaminaCommand Again = GFMovementCommand(0.05f);
	Again.bGrounded = false;
	Again.bDashPressed = true;
	State = GFStaminaSim::Step(State, Again, Tuning);
	TestTrue(TEXT("The refunded dash chains"), State.bDashing);
	TestEqual(TEXT("One charge in the new hop"), State.AirDashesUsed, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementWallRunTest, "GFGame.Movement.WallRun", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementWallRunTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Wall run speed gate"), Tuning.WallRunMinSpeed, 400.f);
	TestEqual(TEXT("Wall run window"), Tuning.WallRunMaxSeconds, 1.5f);
	TestEqual(TEXT("Wall run drain"), Tuning.WallRunDrainPerSecond, 15.f);

	auto Wall = [](float Speed, const FVector& Move, bool bGround)
	{
		FGFStaminaCommand Command = GFMovementTestsPrivate::GFMovementCommand(0.2f);
		Command.bGrounded = bGround;
		Command.HorizontalSpeed = Speed;
		Command.MoveDirection = Move;
		Command.Contact.bWallContact = true;
		Command.Contact.WallNormal = FVector(1.0, 0.0, 0.0);
		return Command;
	};

	const UGFMovementTuning& Tune = Tuning;
	FGFStaminaState Slow = GFStaminaSim::Step(GFMovementFullState(), Wall(400.f, FVector(0.0, 1.0, 0.0), false), Tune);
	TestFalse(TEXT("400 is not above 400"), Slow.bWallRunning);

	FGFStaminaState Into = GFStaminaSim::Step(GFMovementFullState(), Wall(500.f, FVector(-1.0, 0.0, 0.0), false), Tune);
	TestFalse(TEXT("Pushing into the wall does not run"), Into.bWallRunning);

	FGFStaminaState Ground = GFStaminaSim::Step(GFMovementFullState(), Wall(500.f, FVector(0.0, 1.0, 0.0), true), Tune);
	TestFalse(TEXT("Wall run is airborne"), Ground.bWallRunning);

	FGFStaminaCommand Run = Wall(500.f, FVector(0.0, 1.0, 0.0), false);
	FGFStaminaState State = GFStaminaSim::Step(GFMovementFullState(), Run, Tune);
	TestTrue(TEXT("Along the wall at speed runs"), State.bWallRunning);
	TestTrue(TEXT("Wall run drains 15/s"), GFMovementNear(State.Stamina, 197.f));
	TestTrue(TEXT("Run sticks to the wall"), State.bOverrideVelocity && State.OverrideVelocity.Y > 0.0 && State.OverrideVelocity.X < 0.0);

	Run.DeltaSeconds = 1.5f;
	State = GFStaminaSim::Step(GFMovementFullState(), Run, Tune);
	TestFalse(TEXT("The 1.5s budget ends the run"), State.bWallRunning);
	TestTrue(TEXT("Full window costs 22.5"), GFMovementNear(State.Stamina, 177.5f));
	TestTrue(TEXT("Still on the wall blocks a restart"), State.bWallRunBlocked);

	FGFStaminaCommand Stay = Wall(500.f, FVector(0.0, 1.0, 0.0), false);
	Stay.DeltaSeconds = 0.05f;
	State = GFStaminaSim::Step(State, Stay, Tune);
	TestFalse(TEXT("No second run until the wall is left"), State.bWallRunning);

	FGFStaminaCommand Leave = GFMovementCommand(0.05f);
	Leave.bGrounded = false;
	State = GFStaminaSim::Step(State, Leave, Tune);
	State = GFStaminaSim::Step(State, Stay, Tune);
	TestTrue(TEXT("A new wall visit gets a new budget"), State.bWallRunning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementSwivelTest, "GFGame.Movement.SwivelLaunch", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementSwivelTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Swivel tag"), GFTraversalSim::SwivelTag(), FName(TEXT("Swivel")));
	TestTrue(TEXT("A post is a swivel"), GFTraversalSim::IsSwivelTarget(false, FVector(40.0, 40.0, 300.0), Tuning));
	TestFalse(TEXT("A cube is not a swivel"), GFTraversalSim::IsSwivelTarget(false, FVector(400.0, 400.0, 400.0), Tuning));
	TestTrue(TEXT("The tag wins"), GFTraversalSim::IsSwivelTarget(true, FVector(400.0, 400.0, 40.0), Tuning));

	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Grab = GFMovementCommand(0.05f);
	Grab.bGrounded = false;
	Grab.HorizontalSpeed = 500.f;
	Grab.PlayerLocation = FVector(100.0, 0.0, 120.0);
	Grab.MoveDirection = FVector(0.0, 1.0, 0.0);
	Grab.Contact.bSwivelContact = true;
	Grab.Contact.SwivelOrigin = FVector(0.0, 0.0, 120.0);
	State = GFStaminaSim::Step(State, Grab, Tuning);
	TestTrue(TEXT("Orbit starts"), State.bSwiveling);
	TestTrue(TEXT("Angle advances"), State.SwivelAngle > 0.15f && State.SwivelAngle < 0.35f);
	TestTrue(TEXT("Orbit speed holds"), State.bOverrideVelocity && FMath::Abs(State.OverrideVelocity.Size() - 500.0) < 1.0);

	FGFStaminaCommand Launch = Grab;
	Launch.bJumpPressed = true;
	Launch.PlayerLocation = State.TravelLocation;
	State = GFStaminaSim::Step(State, Launch, Tuning);
	TestFalse(TEXT("Jump leaves the pole"), State.bSwiveling);
	const double Horizontal = FVector(State.OverrideVelocity.X, State.OverrideVelocity.Y, 0.0).Size();
	TestTrue(TEXT("Launch is faster than the orbit"), Horizontal > 700.0 && Horizontal < 800.0);
	TestTrue(TEXT("Launch has lift"), State.OverrideVelocity.Z > 200.0);

	FGFStaminaState Dashed = GFMovementFullState();
	FGFStaminaCommand Dash = GFMovementCommand(0.05f);
	Dash.bGrounded = false;
	Dash.bDashPressed = true;
	Dashed = GFStaminaSim::Step(Dashed, Dash, Tuning);
	FGFStaminaCommand Pole = Grab;
	Pole.bDashPressed = false;
	Dashed = GFStaminaSim::Step(Dashed, Pole, Tuning);
	TestTrue(TEXT("A dash into a pole orbits instead of clinging"), Dashed.bSwiveling);
	TestFalse(TEXT("The dash does not also cling"), Dashed.bGripping);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementGrappleSwingTest, "GFGame.Movement.GrappleSwing", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementGrappleSwingTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Grapple range"), Tuning.GrappleRange, 1500.f);
	TestEqual(TEXT("Grapple cooldown"), Tuning.GrappleCooldown, 2.f);
	TestEqual(TEXT("Grapple cost"), Tuning.GrappleCost, 20.f);

	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Miss = GFMovementCommand(0.05f);
	Miss.bGrounded = false;
	Miss.bGrappleSwingHeld = true;
	Miss.PlayerLocation = FVector::ZeroVector;
	Miss.Contact.bGrappleHit = true;
	Miss.Contact.GrapplePoint = FVector(1600.0, 0.0, 0.0);
	State = GFStaminaSim::Step(State, Miss, Tuning);
	TestFalse(TEXT("Past 15m does not attach"), State.bSwinging);
	TestTrue(TEXT("A miss is free"), GFMovementNear(State.Stamina, Tuning.MaxStamina));

	FGFStaminaCommand Fire = Miss;
	Fire.Contact.GrapplePoint = FVector(1000.0, 0.0, 100.0);
	Fire.PlayerLocation = FVector(0.0, 0.0, 100.0);
	Fire.Velocity = FVector(0.0, 500.0, 0.0);
	State = GFStaminaSim::Step(GFMovementFullState(), Fire, Tuning);
	TestTrue(TEXT("Swing attaches"), State.bSwinging);
	TestTrue(TEXT("Swing costs 20"), GFMovementNear(State.Stamina, 180.f));
	TestTrue(TEXT("Cooldown starts at 2s"), GFMovementNear(State.GrappleCooldownLeft, 2.f));
	TestTrue(TEXT("Rope reaches the anchor"), GFMovementNear(State.RopeLength, 1000.f));
	const double RopeNow = FVector::Dist(State.TravelLocation, State.GrappleAnchor);
	TestTrue(TEXT("Pendulum stays on the rope"), FMath::Abs(RopeNow - 1000.0) < 2.0);

	const float Spent = State.Stamina;
	FGFStaminaCommand Again = Fire;
	Again.bGrappleSwingHeld = true;
	State = GFStaminaSim::Step(State, Again, Tuning);
	TestTrue(TEXT("Cooldown blocks a second attach"), GFMovementNear(State.Stamina, Spent));

	FGFStaminaCommand Release = GFMovementCommand(0.05f);
	Release.bGrounded = false;
	Release.PlayerLocation = State.TravelLocation;
	const double Before = State.SwingVelocity.Size();
	State = GFStaminaSim::Step(State, Release, Tuning);
	TestFalse(TEXT("Letting go ends the swing"), State.bSwinging);
	TestTrue(TEXT("Release adds momentum"), State.bOverrideVelocity && State.OverrideVelocity.Size() > Before * 1.2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementGrappleYankTest, "GFGame.Movement.GrappleYank", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementGrappleYankTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Yank = GFMovementCommand(0.5f);
	Yank.bGrounded = false;
	Yank.bGrappleYankPressed = true;
	Yank.PlayerLocation = FVector(0.0, 0.0, 100.0);
	Yank.Contact.bGrappleHit = true;
	Yank.Contact.GrapplePoint = FVector(1000.0, 0.0, 100.0);
	State = GFStaminaSim::Step(State, Yank, Tuning);
	TestTrue(TEXT("Yank attaches"), State.bYanking);
	TestTrue(TEXT("Yank costs 20"), GFMovementNear(State.Stamina, 180.f));
	TestTrue(TEXT("Yank moves toward the anchor"), State.TravelLocation.X > 600.0 && State.TravelLocation.X < 800.0);

	Yank.bGrappleYankPressed = false;
	Yank.DeltaSeconds = 0.5f;
	Yank.PlayerLocation = State.TravelLocation;
	State = GFStaminaSim::Step(State, Yank, Tuning);
	TestFalse(TEXT("Yank arrives and lets go"), State.bYanking);
	TestTrue(TEXT("Yank stops on the anchor"), FVector::Dist(State.TravelLocation, FVector(1000.0, 0.0, 100.0)) < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementLandingMarkerTest, "GFGame.Movement.LandingMarker", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementLandingMarkerTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	FGFStaminaState Air = GFMovementFullState();
	TestFalse(TEXT("Grounded hides the marker"), GFTraversalSim::ShouldShowLandingMarker(Air, true, true));
	TestTrue(TEXT("Airborne shows the marker"), GFTraversalSim::ShouldShowLandingMarker(Air, false, true));
	TestFalse(TEXT("Disabled hides the marker"), GFTraversalSim::ShouldShowLandingMarker(Air, false, false));
	Air.bGripping = true;
	TestFalse(TEXT("A cling hides the marker"), GFTraversalSim::ShouldShowLandingMarker(Air, false, true));
	Air.bGripping = false;
	Air.bWallRunning = true;
	TestFalse(TEXT("A wall run hides the marker"), GFTraversalSim::ShouldShowLandingMarker(Air, false, true));
	Air.bWallRunning = false;
	Air.bSwiveling = true;
	TestFalse(TEXT("A swivel hides the marker"), GFTraversalSim::ShouldShowLandingMarker(Air, false, true));
	Air.bSwiveling = false;
	Air.bSwinging = true;
	TestTrue(TEXT("A swing shows the marker"), GFTraversalSim::ShouldShowLandingMarker(Air, true, true));

	const FGFLandingPrediction Hit = GFTraversalSim::PredictLandingOnPlane(FVector(0.0, 0.0, 300.0), FVector(200.0, 0.0, 0.0), -980.f, 0.f, Tuning.LandingLookahead);
	TestTrue(TEXT("The arc hits the floor"), Hit.bHit);
	TestTrue(TEXT("The hit is on the floor"), FMath::Abs(Hit.Point.Z) < 1.0);
	TestTrue(TEXT("Horizontal speed carries the landing"), Hit.Point.X > 140.0 && Hit.Point.X < 180.0);
	const FVector Sample = GFTraversalSim::BallisticPoint(FVector(0.0, 0.0, 300.0), FVector(200.0, 0.0, 0.0), -980.f, Hit.Time);
	TestTrue(TEXT("The sample matches the root"), FVector::Dist(Sample, Hit.Point) < 0.1);

	const FGFLandingPrediction Miss = GFTraversalSim::PredictLandingOnPlane(FVector(0.0, 0.0, 300.0), FVector(0.0, 0.0, 100.0), -980.f, 0.f, 0.05f);
	TestFalse(TEXT("A short lookahead can miss"), Miss.bHit);

	FGFStaminaState Frozen = GFMovementFullState();
	Frozen.bGripping = true;
	Frozen.GrappleCooldownLeft = 1.2f;
	FGFStaminaCommand Off = GFMovementCommand(2.f);
	Off.bStaminaEnabled = false;
	Off.bSprintHeld = true;
	Off.Contact.bWallContact = true;
	Frozen = GFStaminaSim::Step(Frozen, Off, Tuning);
	TestFalse(TEXT("Mini-games drop a cling"), Frozen.bGripping);
	TestFalse(TEXT("Mini-games do not wall run"), Frozen.bWallRunning);
	TestTrue(TEXT("Grapple cooldown stays frozen"), GFMovementNear(Frozen.GrappleCooldownLeft, 1.2f));
	TestTrue(TEXT("The pool is full while disabled"), GFMovementNear(Frozen.Stamina, Tuning.MaxStamina));
	return true;
}

#endif
