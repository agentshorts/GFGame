// Stamina, sprint, dash and traversal (wall climb #355 included). Pure steps, plus the player CDO's base run speed.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Movement;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFKindleRing.h"
#include "GFMovementPolicy.h"
#include "GFMovementTuning.h"
#include "GFPathRules.h"
#include "GFStaminaSim.h"
#include "GFVillainRules.h"
#include "GFMiniGameGateSubsystem.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UObjectIterator.h"

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
	TestEqual(TEXT("Scavenger does not run faster"), FGFPathRules::MoveSpeedMultiplier(EGFPath::Scavenger), 1.f);
	TestEqual(TEXT("Villain Mode does not run faster"), FGFVillainRules::MoveSpeedMultiplier(), 1.f);
	TestEqual(TEXT("Base run times either multiplier stays 500"), Tuning.BaseRunSpeed * FGFPathRules::MoveSpeedMultiplier(EGFPath::Scavenger) * FGFVillainRules::MoveSpeedMultiplier(), 500.f);

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
	// Warm the gate-name cache before this function starts its own class iterator.
	TestFalse(TEXT("Hub map"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_Hub")));
	TestFalse(TEXT("Net test map"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_NetTest")));
	TestFalse(TEXT("Old arena package"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_Arena")));
	TestFalse(TEXT("Empty map name"), GFMovementPolicy::IsMiniGameMapName(TEXT("")));

	// Maps the gate classes actually load. L_Arena is not one of them.
	const TCHAR* GateMaps[] = {
		TEXT("/Game/Maps/L_Valley"),
		TEXT("/Game/Maps/L_Runway"),
		TEXT("/Game/Maps/L_Moba"),
		TEXT("/Game/Maps/L_Beat"),
		TEXT("/Game/Maps/L_Crucible"),
		TEXT("/Game/Maps/L_Depths"),
		TEXT("/Game/Maps/L_GrumbleDig"),
		TEXT("/Game/Maps/L_Mine"),
	};
	TArray<FString> LoadedShort;
	// Expected count is loaded gate classes, not GateMaps length, so a new gate needs no magic number update.
	int32 GateClassCount = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsNative()
			|| !It->IsChildOf(UGFMiniGameGateSubsystem::StaticClass())
			|| It->HasAnyClassFlags(CLASS_Abstract)
			|| It->GetName().StartsWith(TEXT("SKEL_"))
			|| It->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}
		const UGFMiniGameGateSubsystem* Gate = Cast<UGFMiniGameGateSubsystem>(It->GetDefaultObject());
		if (!Gate || Gate->GetMapName().IsNone())
		{
			continue;
		}
		++GateClassCount;
		const FString Full = Gate->GetMapName().ToString();
		LoadedShort.AddUnique(GFMovementPolicy::ShortMapName(Full));
		TestTrue(*FString::Printf(TEXT("Loaded gate %s"), *Full), GFMovementPolicy::IsMiniGameMapName(Full));
	}
	TestEqual(TEXT("Every gate class has a map"), LoadedShort.Num(), GateClassCount);
	for (const TCHAR* GateMap : GateMaps)
	{
		const FString Short = GFMovementPolicy::ShortMapName(GateMap);
		TestTrue(*FString::Printf(TEXT("%s is a gate map"), *Short), LoadedShort.Contains(Short));
		TestTrue(*FString::Printf(TEXT("%s package"), GateMap), GFMovementPolicy::IsMiniGameMapName(GateMap));
		TestTrue(*FString::Printf(TEXT("PIE %s"), *Short), GFMovementPolicy::IsMiniGameMapName(FString::Printf(TEXT("UEDPIE_0_%s"), *Short)));
		TestTrue(*FString::Printf(TEXT("PIE package %s"), *Short), GFMovementPolicy::IsMiniGameMapName(FString::Printf(TEXT("/Game/Maps/UEDPIE_0_%s"), *Short)));
		TestFalse(*FString::Printf(TEXT("%s defaults off"), *Short), GFMovementPolicy::IsStaminaEnabled(GateMap, false, false));
	}

	TestFalse(TEXT("Mini-game defaults off"), GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Beat"), false, false));
	TestTrue(TEXT("Opt-in turns a mini-game on"), GFMovementPolicy::IsStaminaEnabled(TEXT("UEDPIE_0_L_Beat"), true, true));
	TestFalse(TEXT("Opt-in can stay off"), GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Valley"), true, false));
	TestTrue(TEXT("Hub is on without an opt-in"), GFMovementPolicy::IsStaminaEnabled(TEXT("L_Hub"), false, false));

	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Off = GFMovementCommand(2.f);
	Off.bStaminaEnabled = GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Crucible"), false, false);
	Off.bSprintHeld = true;
	Off.bDashPressed = true;
	State = GFStaminaSim::Step(State, Off, Tuning);
	TestFalse(TEXT("Feature flag is off"), Off.bStaminaEnabled);
	TestFalse(TEXT("No sprint in a mini-game"), State.bSprinting);
	TestFalse(TEXT("No dash in a mini-game"), State.bDashing);
	TestTrue(TEXT("Pool stays full"), GFMovementNear(State.Stamina, Tuning.MaxStamina));
	TestEqual(TEXT("Speed stays the base run"), GFStaminaSim::HorizontalSpeed(State, Tuning, Tuning.BaseRunSpeed), Tuning.BaseRunSpeed);

	FGFStaminaCommand On = Off;
	On.bStaminaEnabled = GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Crucible"), true, true);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementTripleJumpTest, "GFGame.Movement.TripleJump", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementTripleJumpTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Three jumps"), Tuning.JumpsPerAirtime, 3);
	TestEqual(TEXT("Jump 2 height"), Tuning.Jump2HeightScale, 0.85f);
	TestEqual(TEXT("Jump 3 height"), Tuning.Jump3HeightScale, 1.10f);
	TestEqual(TEXT("Extra jump cost"), Tuning.ExtraJumpCost, 15.f);
	TestEqual(TEXT("Jump speed"), Tuning.JumpZVelocity, 520.f);
	TestEqual(TEXT("Jump 3 boost"), Tuning.Jump3ForwardBoost, 250.f);
	TestEqual(TEXT("Flip length"), Tuning.JumpFlipDuration, 0.45f);
	TestEqual(TEXT("Flip degrees"), Tuning.JumpFlipDegrees, 360.f);

	const AGFCharacter* Character = GetDefault<AGFCharacter>();
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("Player movement"), Move))
	{
		return false;
	}
	TestEqual(TEXT("Base hop matches the asset"), Move->JumpZVelocity, Tuning.JumpZVelocity);

	const float Jump2Z = Tuning.JumpZVelocity * FMath::Sqrt(Tuning.Jump2HeightScale);
	const float Jump3Z = Tuning.JumpZVelocity * FMath::Sqrt(Tuning.Jump3HeightScale);

	FGFStaminaState State = GFMovementFullState();
	FGFStaminaCommand Hop = GFMovementCommand(0.05f);
	Hop.bJumpPressed = true;
	Hop.MoveDirection = FVector(0.0, 1.0, 0.0);
	State = GFStaminaSim::Step(State, Hop, Tuning);
	TestTrue(TEXT("Ground hop is jump 1"), State.bJumpAccepted && State.JumpsUsed == 1);
	TestTrue(TEXT("Jump 1 is free"), GFMovementNear(State.Stamina, Tuning.MaxStamina));
	TestTrue(TEXT("Jump 1 is full height"), GFMovementNear(State.JumpZSpeed, Tuning.JumpZVelocity));
	TestTrue(TEXT("Jump 1 has no boost"), State.JumpForward.IsNearlyZero());
	TestTrue(TEXT("Jump 1 does not flip"), State.FlipTimeLeft <= 0.f);
	TestEqual(TEXT("Jump 1 leaves the air dash"), State.AirDashesUsed, 0);

	FGFStaminaCommand Air = Hop;
	Air.bGrounded = false;
	State = GFStaminaSim::Step(State, Air, Tuning);
	TestTrue(TEXT("Second press is jump 2"), State.bJumpAccepted && State.JumpsUsed == 2);
	TestTrue(TEXT("Jump 2 costs 15"), GFMovementNear(State.Stamina, 185.f));
	TestTrue(TEXT("Jump 2 is 85% height"), GFMovementNear(State.JumpZSpeed, Jump2Z));
	TestTrue(TEXT("Jump 2 does not flip"), State.FlipTimeLeft <= 0.f);
	TestTrue(TEXT("Spend arms the delay"), GFMovementNear(State.RegenDelayLeft, Tuning.RegenDelay));

	Air.MoveDirection = FVector(0.0, 1.0, 0.0);
	State = GFStaminaSim::Step(State, Air, Tuning);
	TestTrue(TEXT("Third press is the flip"), State.bJumpAccepted && State.JumpsUsed == 3);
	TestTrue(TEXT("Jump 3 costs another 15"), GFMovementNear(State.Stamina, 170.f));
	TestTrue(TEXT("Jump 3 is 110% height"), GFMovementNear(State.JumpZSpeed, Jump3Z));
	TestTrue(TEXT("Jump 3 boosts forward"), State.JumpForward.Y > 200.0 && FMath::Abs(State.JumpForward.X) < 1.0);
	TestTrue(TEXT("Flip timer starts"), GFMovementNear(State.FlipTimeLeft, Tuning.JumpFlipDuration));
	TestEqual(TEXT("The flip did not spend the air dash"), State.AirDashesUsed, 0);

	const float Spent = State.Stamina;
	State = GFStaminaSim::Step(State, Air, Tuning);
	TestFalse(TEXT("A fourth press is refused"), State.bJumpAccepted);
	TestEqual(TEXT("The count stays at 3"), State.JumpsUsed, 3);
	TestTrue(TEXT("A refused jump is free"), GFMovementNear(State.Stamina, Spent));

	FGFStaminaCommand Dash = Air;
	Dash.bJumpPressed = false;
	Dash.bDashPressed = true;
	const int32 JumpsBeforeDash = 2;
	FGFStaminaState Dashed = GFMovementFullState();
	Dashed.JumpsUsed = JumpsBeforeDash;
	Dashed.bWasGrounded = false;
	Dashed.Stamina = 185.f;
	Dashed.RegenDelayLeft = Tuning.RegenDelay;
	Dashed = GFStaminaSim::Step(Dashed, Dash, Tuning);
	TestEqual(TEXT("Air dash does not spend a jump"), Dashed.JumpsUsed, JumpsBeforeDash);
	TestEqual(TEXT("Air dash still has its own charge"), Dashed.AirDashesUsed, 1);
	Dash.bDashPressed = false;
	Dash.bJumpPressed = true;
	Dashed = GFStaminaSim::Step(Dashed, Dash, Tuning);
	TestTrue(TEXT("Jump 3 still follows an air dash"), Dashed.bJumpAccepted && Dashed.JumpsUsed == 3);

	FGFStaminaCommand Land = GFMovementCommand(0.05f);
	Land.bGrounded = true;
	FGFStaminaState Landed = State;
	Landed.bWasGrounded = false;
	Landed.AirDashesUsed = 1;
	Landed = GFStaminaSim::Step(Landed, Land, Tuning);
	TestEqual(TEXT("Landing resets the jump count"), Landed.JumpsUsed, 0);
	TestEqual(TEXT("Landing still resets the air dash"), Landed.AirDashesUsed, 0);
	TestTrue(TEXT("Landing ends the flip"), Landed.FlipTimeLeft <= 0.f);

	FGFStaminaState Broke = GFMovementFullState();
	Broke.Stamina = 10.f;
	Broke.RegenDelayLeft = 1.f;
	FGFStaminaCommand BrokeHop = GFMovementCommand(0.05f);
	BrokeHop.bJumpPressed = true;
	Broke = GFStaminaSim::Step(Broke, BrokeHop, Tuning);
	TestTrue(TEXT("Jump 1 works on an empty-ish pool"), Broke.bJumpAccepted && Broke.JumpsUsed == 1);
	TestTrue(TEXT("Jump 1 still costs nothing"), GFMovementNear(Broke.Stamina, 10.f));
	BrokeHop.bGrounded = false;
	Broke = GFStaminaSim::Step(Broke, BrokeHop, Tuning);
	TestFalse(TEXT("Jump 2 waits for 15 stamina"), Broke.bJumpAccepted);
	TestEqual(TEXT("The refused extra jump is not counted"), Broke.JumpsUsed, 1);
	TestTrue(TEXT("Stamina stays put"), GFMovementNear(Broke.Stamina, 10.f));

	FGFStaminaState Hearth = GFMovementFullState();
	Hearth.bWasGrounded = false;
	FGFStaminaCommand HearthHop = GFMovementCommand(0.05f);
	HearthHop.bGrounded = false;
	HearthHop.bJumpPressed = true;
	HearthHop.bInsideKindleRing = true;
	Hearth = GFStaminaSim::Step(Hearth, HearthHop, Tuning);
	Hearth = GFStaminaSim::Step(Hearth, HearthHop, Tuning);
	Hearth = GFStaminaSim::Step(Hearth, HearthHop, Tuning);
	TestTrue(TEXT("Hearth still allows the flip"), Hearth.bJumpAccepted && Hearth.JumpsUsed == 3);
	TestTrue(TEXT("Hearth extra jumps are free"), GFMovementNear(Hearth.Stamina, Tuning.MaxStamina));

	FGFStaminaState Kick = GFMovementFullState();
	Kick.JumpsUsed = 2;
	Kick.Stamina = 170.f;
	Kick.RegenDelayLeft = 1.f;
	Kick.bWasGrounded = false;
	Kick.bGripping = true;
	Kick.GripTimeLeft = 1.f;
	Kick.GripNormal = FVector(1.0, 0.0, 0.0);
	Kick.FlipTimeLeft = 0.4f;
	FGFStaminaCommand KickCmd = GFMovementCommand(0.05f);
	KickCmd.bGrounded = false;
	KickCmd.bJumpPressed = true;
	Kick = GFStaminaSim::Step(Kick, KickCmd, Tuning);
	TestTrue(TEXT("Wall-kick consumes the press"), Kick.bTraversalJump && !Kick.bJumpAccepted);
	TestEqual(TEXT("Wall-kick resets the jump count"), Kick.JumpsUsed, 0);
	TestEqual(TEXT("Wall-kick still refunds the air dash"), Kick.AirDashesUsed, 0);
	TestTrue(TEXT("Wall-kick drops the flip"), Kick.FlipTimeLeft <= 0.f);
	TestTrue(TEXT("Wall-kick is not an extra jump charge"), GFMovementNear(Kick.Stamina, 170.f));
	KickCmd.bJumpPressed = true;
	Kick = GFStaminaSim::Step(Kick, KickCmd, Tuning);
	TestTrue(TEXT("The next hop after a kick is jump 1"), Kick.bJumpAccepted && Kick.JumpsUsed == 1);
	TestTrue(TEXT("That hop is full height"), GFMovementNear(Kick.JumpZSpeed, Tuning.JumpZVelocity));

	FGFStaminaState Run = GFMovementFullState();
	FGFStaminaCommand Along = GFMovementCommand(0.05f);
	Along.bGrounded = false;
	Along.HorizontalSpeed = 500.f;
	Along.MoveDirection = FVector(0.0, 1.0, 0.0);
	Along.Contact.bWallContact = true;
	Along.Contact.WallNormal = FVector(1.0, 0.0, 0.0);
	Run = GFStaminaSim::Step(Run, Along, Tuning);
	TestTrue(TEXT("Setup is a wall run"), Run.bWallRunning);
	Along.bJumpPressed = true;
	Run = GFStaminaSim::Step(Run, Along, Tuning);
	TestTrue(TEXT("A hop leaves the wall run"), Run.bJumpAccepted && !Run.bWallRunning);
	TestTrue(TEXT("That visit stays closed"), Run.bWallRunBlocked);
	Along.bJumpPressed = false;
	Run = GFStaminaSim::Step(Run, Along, Tuning);
	TestFalse(TEXT("Still touching the wall does not restart the run"), Run.bWallRunning);

	FGFStaminaState Rope = GFMovementFullState();
	Rope.bSwinging = true;
	Rope.bWasGrounded = false;
	Rope.GrappleAnchor = FVector(0.0, 0.0, 500.0);
	Rope.TravelLocation = FVector::ZeroVector;
	Rope.RopeLength = 500.f;
	Rope.SwingVelocity = FVector(100.0, 0.0, 0.0);
	FGFStaminaCommand Swing = GFMovementCommand(0.05f);
	Swing.bGrounded = false;
	Swing.bJumpPressed = true;
	Swing.bGrappleSwingHeld = true;
	Rope = GFStaminaSim::Step(Rope, Swing, Tuning);
	TestFalse(TEXT("A swing keeps the jump press"), Rope.bJumpAccepted);
	TestEqual(TEXT("A swing does not count a hop"), Rope.JumpsUsed, 0);
	TestTrue(TEXT("Still swinging"), Rope.bSwinging);

	FGFStaminaState Off = GFMovementFullState();
	Off.JumpsUsed = 2;
	Off.FlipTimeLeft = 0.3f;
	FGFStaminaCommand Disabled = GFMovementCommand(0.05f);
	Disabled.bStaminaEnabled = false;
	Disabled.bJumpPressed = true;
	Off = GFStaminaSim::Step(Off, Disabled, Tuning);
	TestEqual(TEXT("Mini-games clear the jump count"), Off.JumpsUsed, 0);
	TestFalse(TEXT("Mini-games do not accept the hop"), Off.bJumpAccepted);
	TestTrue(TEXT("Mini-games end the flip"), Off.FlipTimeLeft <= 0.f);
	return true;
}

// #355 wall climb. Wall is at +X with its face normal pointing -X, so "into" is +X and "along" is +/-Y.
namespace GFWallClimbTestsPrivate
{
	const FVector GFClimbNormal(-1.0, 0.0, 0.0);
	const FVector GFClimbInto(1.0, 0.0, 0.0);
	const FVector GFClimbAlong(0.0, 1.0, 0.0);

	FGFStaminaCommand GFClimbCommand(float DeltaSeconds, const FVector& Move, bool bGrounded)
	{
		FGFStaminaCommand Command = GFMovementTestsPrivate::GFMovementCommand(DeltaSeconds);
		Command.bGrounded = bGrounded;
		Command.MoveDirection = Move;
		Command.PlayerLocation = FVector(0.0, 0.0, 88.0);
		Command.Contact.bWallAhead = true;
		Command.Contact.WallAheadNormal = GFClimbNormal;
		return Command;
	}

	/** Grounded into the wall for one short step, so the state is climbing. */
	FGFStaminaState GFClimbingState(const UGFMovementTuning& Tuning)
	{
		return GFStaminaSim::Step(GFMovementTestsPrivate::GFMovementFullState(), GFClimbCommand(0.05f, GFClimbInto, true), Tuning);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementWallClimbEnterTest, "GFGame.Movement.WallClimb.Enter", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementWallClimbEnterTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	using namespace GFWallClimbTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Climb speed"), Tuning.WallClimbSpeed, 350.f);
	TestEqual(TEXT("Climb drain"), Tuning.WallClimbDrainPerSecond, 12.f);
	TestEqual(TEXT("Climb window"), Tuning.WallClimbMaxSeconds, 1.5f);
	TestTrue(TEXT("Hand-off speed clears the wall run gate"), Tuning.WallClimbToRunSpeed > Tuning.WallRunMinSpeed);

	FGFStaminaState State = GFStaminaSim::Step(GFMovementFullState(), GFClimbCommand(0.1f, GFClimbInto, true), Tuning);
	TestTrue(TEXT("Grounded into the wall climbs"), State.bWallClimbing);
	TestFalse(TEXT("A climb is not a wall run"), State.bWallRunning);
	TestTrue(TEXT("Climb goes straight up at 350"), State.bOverrideVelocity && GFMovementNear(static_cast<float>(State.OverrideVelocity.Z), 350.f));
	TestTrue(TEXT("Climb sticks into the wall"), State.OverrideVelocity.X > 0.0 && FMath::IsNearlyZero(State.OverrideVelocity.Y));
	TestTrue(TEXT("Climb drains 12/s"), GFMovementNear(State.Stamina, 198.8f));
	TestTrue(TEXT("No gravity while climbing"), FMath::IsNearlyZero(GFStaminaSim::GravityScale(State, Tuning)));
	TestFalse(TEXT("No landing marker while climbing"), GFTraversalSim::ShouldShowLandingMarker(State, false, true));
	TestTrue(TEXT("Climb normal is the wall"), State.WallClimbNormal.Equals(GFClimbNormal, 0.01));

	FVector Location(0.0, 0.0, 88.0);
	for (int32 Index = 0; Index < 10; ++Index)
	{
		FGFStaminaCommand Up = GFClimbCommand(0.05f, GFClimbInto, false);
		Up.PlayerLocation = Location;
		State = GFStaminaSim::Step(State, Up, Tuning);
		Location += State.OverrideVelocity * 0.05;
	}
	TestTrue(TEXT("Still climbing after half a second"), State.bWallClimbing);
	TestTrue(TEXT("Half a second climbs 175 cm"), FMath::IsNearlyEqual(Location.Z - 88.0, 175.0, 1.0));

	FGFStaminaState Air = GFStaminaSim::Step(GFMovementFullState(), GFClimbCommand(0.05f, GFClimbInto, false), Tuning);
	TestFalse(TEXT("A climb does not start in mid-air"), Air.bWallClimbing);

	FGFStaminaState Coyote = GFStaminaSim::Step(GFMovementFullState(), GFMovementCommand(0.05f), Tuning);
	TestTrue(TEXT("Standing arms the coyote window"), GFMovementNear(Coyote.WallClimbCoyoteLeft, Tuning.WallClimbCoyoteSeconds));
	FGFStaminaState JustLeft = GFStaminaSim::Step(Coyote, GFClimbCommand(0.05f, GFClimbInto, false), Tuning);
	TestTrue(TEXT("Just after leaving the floor still climbs"), JustLeft.bWallClimbing);
	FGFStaminaCommand Fall = GFMovementCommand(0.2f);
	Fall.bGrounded = false;
	FGFStaminaState Late = GFStaminaSim::Step(Coyote, Fall, Tuning);
	Late = GFStaminaSim::Step(Late, GFClimbCommand(0.05f, GFClimbInto, false), Tuning);
	TestFalse(TEXT("Past the coyote window does not climb"), Late.bWallClimbing);

	FGFStaminaCommand Hop = GFClimbCommand(0.05f, GFClimbInto, true);
	Hop.bJumpPressed = true;
	FGFStaminaState Hopped = GFStaminaSim::Step(GFMovementFullState(), Hop, Tuning);
	TestFalse(TEXT("A grounded jump into the wall is a hop, not a climb"), Hopped.bWallClimbing);
	TestTrue(TEXT("The hop is accepted"), Hopped.bJumpAccepted);

	FGFStaminaState Along = GFStaminaSim::Step(GFMovementFullState(), GFClimbCommand(0.05f, GFClimbAlong, true), Tuning);
	TestFalse(TEXT("Along the wall does not climb"), Along.bWallClimbing);
	FGFStaminaState Idle = GFStaminaSim::Step(GFMovementFullState(), GFClimbCommand(0.05f, FVector::ZeroVector, true), Tuning);
	TestFalse(TEXT("No input does not climb"), Idle.bWallClimbing);
	FGFStaminaState Diagonal = GFStaminaSim::Step(GFMovementFullState(), GFClimbCommand(0.05f, FVector(1.0, 0.8, 0.0), true), Tuning);
	TestTrue(TEXT("Mostly into the wall still climbs"), Diagonal.bWallClimbing);

	FGFStaminaCommand Slope = GFClimbCommand(0.05f, GFClimbInto, true);
	Slope.Contact.WallAheadNormal = FVector(-0.7, 0.0, 0.7);
	FGFStaminaState Ramp = GFStaminaSim::Step(GFMovementFullState(), Slope, Tuning);
	TestFalse(TEXT("A slope is not a wall"), Ramp.bWallClimbing);

	FGFStaminaCommand Side = GFClimbCommand(0.05f, GFClimbInto, true);
	Side.Contact.bWallAhead = false;
	Side.Contact.bWallContact = true;
	Side.Contact.WallNormal = GFClimbNormal;
	TestTrue(TEXT("A side wall the input pushes into also climbs"), GFStaminaSim::Step(GFMovementFullState(), Side, Tuning).bWallClimbing);

	FGFStaminaCommand Kindle = GFClimbCommand(0.5f, GFClimbInto, true);
	Kindle.bInsideKindleRing = true;
	FGFStaminaState Free = GFStaminaSim::Step(GFMovementFullState(), Kindle, Tuning);
	TestTrue(TEXT("Hearth climbs"), Free.bWallClimbing);
	TestTrue(TEXT("Hearth climb is free"), GFMovementNear(Free.Stamina, Tuning.MaxStamina));

	FGFStaminaCommand BeatHop = GFClimbCommand(0.05f, GFClimbInto, true);
	BeatHop.bWallClimbAllowed = false;
	TestFalse(TEXT("Beat Hop runs never climb"), GFStaminaSim::Step(GFMovementFullState(), BeatHop, Tuning).bWallClimbing);

	FGFStaminaState Climbing = GFClimbingState(Tuning);
	FGFStaminaCommand Kick = GFClimbCommand(0.05f, GFClimbInto, false);
	Kick.bJumpPressed = true;
	FGFStaminaState Kicked = GFStaminaSim::Step(Climbing, Kick, Tuning);
	TestFalse(TEXT("Jump leaves the climb"), Kicked.bWallClimbing);
	TestTrue(TEXT("Jump off a climb is a wall-kick"), Kicked.bTraversalJump && !Kicked.bJumpAccepted);
	TestTrue(TEXT("Kick pushes off the wall with lift"), Kicked.bOverrideVelocity && Kicked.OverrideVelocity.X < 0.0 && Kicked.OverrideVelocity.Z > 0.0);
	TestEqual(TEXT("Kick resets the hop count"), Kicked.JumpsUsed, 0);
	FGFStaminaState AfterKick = GFStaminaSim::Step(Kicked, GFClimbCommand(0.05f, GFClimbInto, false), Tuning);
	TestFalse(TEXT("A kick does not grab the same wall again"), AfterKick.bWallClimbing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementWallClimbDropTest, "GFGame.Movement.WallClimb.Drop", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementWallClimbDropTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	using namespace GFWallClimbTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	const FGFStaminaState Climbing = GFClimbingState(Tuning);
	TestTrue(TEXT("Setup is a climb"), Climbing.bWallClimbing);

	FGFStaminaState Peel = GFStaminaSim::Step(Climbing, GFClimbCommand(0.05f, FVector::ZeroVector, false), Tuning);
	TestFalse(TEXT("Letting go of the stick drops the climb"), Peel.bWallClimbing);
	TestFalse(TEXT("A drop does not drive the velocity"), Peel.bOverrideVelocity);
	TestTrue(TEXT("Gravity is back after the drop"), GFMovementNear(GFStaminaSim::GravityScale(Peel, Tuning), 1.f));

	FGFStaminaState Away = GFStaminaSim::Step(Climbing, GFClimbCommand(0.05f, GFClimbNormal, false), Tuning);
	TestFalse(TEXT("Pulling away drops the climb"), Away.bWallClimbing);

	FGFStaminaCommand Gone = GFClimbCommand(0.05f, GFClimbInto, false);
	Gone.Contact.bWallAhead = false;
	FGFStaminaState Topped = GFStaminaSim::Step(Climbing, Gone, Tuning);
	TestFalse(TEXT("Losing the wall drops the climb"), Topped.bWallClimbing);
	TestFalse(TEXT("No stuck climb state"), Topped.bOverrideVelocity || Topped.bMantling);

	FGFStaminaState Empty = Climbing;
	Empty.Stamina = 1.f;
	Empty = GFStaminaSim::Step(Empty, GFClimbCommand(0.2f, GFClimbInto, false), Tuning);
	TestFalse(TEXT("An empty pool drops the climb"), Empty.bWallClimbing);
	TestTrue(TEXT("The climb spent the last point"), GFMovementNear(Empty.Stamina, 0.f));
	TestTrue(TEXT("Empty blocks a re-grab"), Empty.bWallClimbBlocked);

	FGFStaminaState Long = GFStaminaSim::Step(GFMovementFullState(), GFClimbCommand(Tuning.WallClimbMaxSeconds, GFClimbInto, true), Tuning);
	TestFalse(TEXT("The 1.5s window ends the climb"), Long.bWallClimbing);
	TestTrue(TEXT("Full window costs 18"), GFMovementNear(Long.Stamina, 182.f));
	TestTrue(TEXT("Still pushing into the wall stays blocked"), Long.bWallClimbBlocked);
	Long = GFStaminaSim::Step(Long, GFClimbCommand(0.05f, GFClimbInto, true), Tuning);
	TestFalse(TEXT("Landing while still pushing in does not re-climb"), Long.bWallClimbing);
	Long = GFStaminaSim::Step(Long, GFClimbCommand(0.05f, FVector::ZeroVector, true), Tuning);
	TestFalse(TEXT("Easing off the stick clears the block"), Long.bWallClimbBlocked);
	Long = GFStaminaSim::Step(Long, GFClimbCommand(0.05f, GFClimbInto, true), Tuning);
	TestTrue(TEXT("Pushing in again climbs again"), Long.bWallClimbing);

	FGFStaminaCommand Disabled = GFClimbCommand(0.05f, GFClimbInto, false);
	Disabled.bStaminaEnabled = false;
	FGFStaminaState Off = GFStaminaSim::Step(Climbing, Disabled, Tuning);
	TestFalse(TEXT("Mini-games drop the climb"), Off.bWallClimbing || Off.bMantling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementWallClimbToWallRunTest, "GFGame.Movement.WallClimb.ToWallRun", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementWallClimbToWallRunTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	using namespace GFWallClimbTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	const FGFStaminaState Climbing = GFClimbingState(Tuning);
	const float StaminaBefore = Climbing.Stamina;

	FGFStaminaCommand Turn = GFClimbCommand(0.1f, GFClimbAlong, false);
	Turn.HorizontalSpeed = 60.f;
	FGFStaminaState Run = GFStaminaSim::Step(Climbing, Turn, Tuning);
	TestFalse(TEXT("Turning along the wall leaves the climb"), Run.bWallClimbing);
	TestTrue(TEXT("Turning along the wall wall-runs"), Run.bWallRunning);
	TestTrue(TEXT("A slow climb carries the hand-off speed"), GFMovementNear(Run.WallRunSpeed, Tuning.WallClimbToRunSpeed));
	TestTrue(TEXT("The run uses the climbed wall"), Run.WallNormal.Equals(GFClimbNormal, 0.01));
	TestTrue(TEXT("The run goes along the wall and sticks in"), Run.bOverrideVelocity && Run.OverrideVelocity.Y > 0.0 && Run.OverrideVelocity.X > 0.0);
	TestTrue(TEXT("The run spends the one wall run budget"), GFMovementNear(Run.WallRunTimeLeft, Tuning.WallRunMaxSeconds - 0.1f));
	TestTrue(TEXT("The run drains 15/s"), GFMovementNear(Run.Stamina, StaminaBefore - 1.5f));

	FGFStaminaCommand Fast = Turn;
	Fast.HorizontalSpeed = 600.f;
	TestTrue(TEXT("A fast hand-off keeps its speed"), GFMovementNear(GFStaminaSim::Step(Climbing, Fast, Tuning).WallRunSpeed, 600.f));

	FGFStaminaCommand Back = GFClimbCommand(0.1f, FVector(0.0, -1.0, 0.0), false);
	TestTrue(TEXT("Either way along the wall hands off"), GFStaminaSim::Step(Climbing, Back, Tuning).WallRunDir.Y < 0.0);

	FGFStaminaCommand Stay;
	Stay = GFMovementCommand(Tuning.WallRunMaxSeconds);
	Stay.bGrounded = false;
	Stay.HorizontalSpeed = Run.WallRunSpeed;
	Stay.MoveDirection = GFClimbAlong;
	Stay.Contact.bWallContact = true;
	Stay.Contact.WallNormal = GFClimbNormal;
	Run = GFStaminaSim::Step(Run, Stay, Tuning);
	TestFalse(TEXT("The same budget runs out"), Run.bWallRunning);
	TestTrue(TEXT("No second timer: the budget is empty"), Run.WallRunTimeLeft <= KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Out of budget blocks the wall run"), Run.bWallRunBlocked);
	Stay.DeltaSeconds = 0.05f;
	Run = GFStaminaSim::Step(Run, Stay, Tuning);
	TestFalse(TEXT("No restart until the wall is left"), Run.bWallRunning);

	FGFStaminaState Blocked = Climbing;
	Blocked.bWallRunBlocked = true;
	Blocked = GFStaminaSim::Step(Blocked, Turn, Tuning);
	TestFalse(TEXT("A blocked visit cannot hand off"), Blocked.bWallRunning);
	TestFalse(TEXT("A failed hand-off still leaves the climb"), Blocked.bWallClimbing);

	FGFStaminaCommand Leave = GFMovementCommand(0.05f);
	Leave.bGrounded = false;
	Run = GFStaminaSim::Step(Run, Leave, Tuning);
	TestTrue(TEXT("Leaving the wall gives a new budget"), GFMovementNear(Run.WallRunTimeLeft, Tuning.WallRunMaxSeconds) && !Run.bWallRunBlocked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementWallClimbMantleTest, "GFGame.Movement.WallClimb.Mantle", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementWallClimbMantleTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	using namespace GFWallClimbTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();
	TestEqual(TEXT("Mantle cost"), Tuning.WallClimbMantleCost, 10.f);
	TestEqual(TEXT("Mantle reach"), Tuning.WallClimbMantleReach, 220.f);

	// Wall face at X = 50, lip 150 cm above the feet. The movement component's probe fills these from the world.
	const FVector Target(100.0, 0.0, 150.0 + 88.0 + 2.0);
	auto Ledge = [&Target](float Dt, const FVector& Location, bool bGrounded, float LipHeight)
	{
		FGFStaminaCommand Command = GFClimbCommand(Dt, GFClimbInto, bGrounded);
		Command.PlayerLocation = Location;
		Command.Contact.bLedge = true;
		Command.Contact.LedgeTarget = Target;
		Command.Contact.LedgeHeight = LipHeight;
		return Command;
	};

	FVector Location(0.0, 0.0, 88.0);
	FGFStaminaState State = GFStaminaSim::Step(GFMovementFullState(), Ledge(0.05f, Location, true, 150.f), Tuning);
	TestTrue(TEXT("A lip in reach mantles straight from the climb"), State.bMantling);
	TestFalse(TEXT("A mantle is not a climb"), State.bWallClimbing);
	TestTrue(TEXT("Mantle costs 10"), GFMovementNear(State.Stamina, 190.f));
	TestTrue(TEXT("Mantle rises first"), State.bOverrideVelocity && State.OverrideVelocity.Z > 0.0 && FMath::IsNearlyZero(State.OverrideVelocity.X));
	TestTrue(TEXT("No gravity while mantling"), FMath::IsNearlyZero(GFStaminaSim::GravityScale(State, Tuning)));

	int32 Steps = 0;
	bool bSawForward = false;
	bool bJumpRefused = true;
	while (State.bMantling && Steps < 40)
	{
		Location += State.OverrideVelocity * 0.05;
		FGFStaminaCommand Next = Ledge(0.05f, Location, false, 150.f);
		Next.Contact.bWallAhead = false;
		Next.Contact.bLedge = false;
		Next.bJumpPressed = Steps == 2;
		Next.bDashPressed = Steps == 3;
		State = GFStaminaSim::Step(State, Next, Tuning);
		if (Steps == 2 && (State.bJumpAccepted || !State.bMantling))
		{
			bJumpRefused = false;
		}
		TestFalse(TEXT("No dash during a mantle"), State.bDashing);
		bSawForward = bSawForward || (State.bMantling && State.OverrideVelocity.X > 0.0 && FMath::IsNearlyZero(State.OverrideVelocity.Z));
		++Steps;
	}
	TestFalse(TEXT("The mantle ends"), State.bMantling);
	TestTrue(TEXT("The mantle steps onto the lip after rising"), bSawForward);
	TestTrue(TEXT("A jump does not break the mantle"), bJumpRefused);
	TestTrue(TEXT("The mantle ends inside its time"), static_cast<float>(Steps) * 0.05f <= Tuning.WallClimbMantleMaxSeconds + 0.05f);
	TestTrue(TEXT("The mantle stands on the lip"), FVector::Dist(Location, Target) <= Tuning.WallClimbMantleArrive + 1.0);
	TestTrue(TEXT("The mantle stops dead at the end"), State.bOverrideVelocity && State.OverrideVelocity.IsNearlyZero());

	FGFStaminaCommand Walk = GFMovementCommand(0.05f);
	Walk.PlayerLocation = Location;
	Walk.MoveDirection = GFClimbInto;
	State = GFStaminaSim::Step(State, Walk, Tuning);
	TestFalse(TEXT("Grounded after the mantle"), State.bMantling || State.bWallClimbing || State.bOverrideVelocity);
	TestTrue(TEXT("Gravity is back on the lip"), GFMovementNear(GFStaminaSim::GravityScale(State, Tuning), 1.f));

	FGFStaminaState High = GFStaminaSim::Step(GFMovementFullState(), Ledge(0.05f, FVector(0.0, 0.0, 88.0), true, Tuning.WallClimbMantleReach + 50.f), Tuning);
	TestTrue(TEXT("A lip out of reach keeps climbing"), High.bWallClimbing && !High.bMantling);
	FGFStaminaState Low = GFStaminaSim::Step(GFMovementFullState(), Ledge(0.05f, FVector(0.0, 0.0, 88.0), true, Tuning.WallClimbMantleMinLip - 10.f), Tuning);
	TestFalse(TEXT("A lip below the minimum is not a mantle"), Low.bMantling);

	FGFStaminaState Poor = GFMovementFullState();
	Poor.Stamina = 5.f;
	Poor = GFStaminaSim::Step(Poor, Ledge(0.05f, FVector(0.0, 0.0, 88.0), true, 150.f), Tuning);
	TestTrue(TEXT("Too little stamina for the mantle keeps climbing"), Poor.bWallClimbing && !Poor.bMantling);

	FGFStaminaCommand KindleLedge = Ledge(0.05f, FVector(0.0, 0.0, 88.0), true, 150.f);
	KindleLedge.bInsideKindleRing = true;
	FGFStaminaState Free = GFMovementFullState();
	Free.Stamina = 5.f;
	Free = GFStaminaSim::Step(Free, KindleLedge, Tuning);
	TestTrue(TEXT("Hearth mantle is free"), Free.bMantling && GFMovementNear(Free.Stamina, Tuning.MaxStamina));

	FGFStaminaState Stuck = GFStaminaSim::Step(GFMovementFullState(), Ledge(0.05f, FVector(0.0, 0.0, 88.0), true, 150.f), Tuning);
	FGFStaminaCommand Pinned = Ledge(0.1f, FVector(0.0, 0.0, 88.0), false, 150.f);
	for (int32 Index = 0; Index < 10 && Stuck.bMantling; ++Index)
	{
		Stuck = GFStaminaSim::Step(Stuck, Pinned, Tuning);
	}
	TestFalse(TEXT("A mantle that never arrives times out"), Stuck.bMantling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementWallClimbBusyTest, "GFGame.Movement.WallClimb.BusyBlocks", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementWallClimbBusyTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	using namespace GFWallClimbTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();

	FGFStaminaState Grip = GFMovementFullState();
	Grip.bGripping = true;
	Grip.GripTimeLeft = 2.f;
	Grip.GripNormal = GFClimbNormal;
	Grip = GFStaminaSim::Step(Grip, GFClimbCommand(0.05f, GFClimbInto, true), Tuning);
	TestTrue(TEXT("The cling holds"), Grip.bGripping);
	TestFalse(TEXT("A cling blocks the climb"), Grip.bWallClimbing);

	FGFStaminaState Dash = GFMovementFullState();
	Dash.bDashing = true;
	Dash.DashTimeLeft = 0.2f;
	Dash = GFStaminaSim::Step(Dash, GFClimbCommand(0.05f, GFClimbInto, true), Tuning);
	TestTrue(TEXT("The dash keeps going"), Dash.bDashing);
	TestFalse(TEXT("A dash blocks the climb"), Dash.bWallClimbing);

	FGFStaminaState Swing = GFMovementFullState();
	Swing.bSwinging = true;
	Swing.bWasGrounded = false;
	Swing.GrappleAnchor = FVector(0.0, 0.0, 600.0);
	Swing.TravelLocation = FVector(0.0, 0.0, 88.0);
	Swing.RopeLength = 512.f;
	FGFStaminaCommand SwingHeld = GFClimbCommand(0.05f, GFClimbInto, false);
	SwingHeld.bGrappleSwingHeld = true;
	Swing.WallClimbCoyoteLeft = 0.1f;
	Swing = GFStaminaSim::Step(Swing, SwingHeld, Tuning);
	TestFalse(TEXT("A swing blocks the climb"), Swing.bWallClimbing);

	FGFStaminaState Climbing = GFClimbingState(Tuning);
	FGFStaminaCommand Pole = GFClimbCommand(0.05f, GFClimbInto, false);
	Pole.Contact.bSwivelContact = true;
	Pole.Contact.SwivelOrigin = FVector(60.0, 0.0, 88.0);
	Pole.HorizontalSpeed = 300.f;
	FGFStaminaState Orbit = GFStaminaSim::Step(Climbing, Pole, Tuning);
	TestTrue(TEXT("A pole still wins over the climb"), Orbit.bSwiveling && !Orbit.bWallClimbing);

	FGFStaminaCommand DashPress = GFClimbCommand(0.05f, GFClimbInto, false);
	DashPress.bDashPressed = true;
	FGFStaminaState NoDash = GFStaminaSim::Step(Climbing, DashPress, Tuning);
	TestTrue(TEXT("Climbing keeps going"), NoDash.bWallClimbing);
	TestFalse(TEXT("No dash while climbing"), NoDash.bDashing);

	FGFStaminaCommand Sprint = GFClimbCommand(0.05f, GFClimbInto, false);
	Sprint.bSprintHeld = true;
	TestFalse(TEXT("No sprint while climbing"), GFStaminaSim::Step(Climbing, Sprint, Tuning).bSprinting);

	FGFStaminaCommand Run = GFMovementCommand(0.05f);
	Run.bGrounded = false;
	Run.HorizontalSpeed = 500.f;
	Run.MoveDirection = GFClimbAlong;
	Run.Contact.bWallContact = true;
	Run.Contact.WallNormal = GFClimbNormal;
	FGFStaminaState WallRun = GFStaminaSim::Step(GFMovementFullState(), Run, Tuning);
	TestTrue(TEXT("Plain wall run is unchanged"), WallRun.bWallRunning && !WallRun.bWallClimbing);
	TestTrue(TEXT("A plain wall run is the current move"), WallRun.CurrentMove == EGFTraversalMove::WallRun);
	return true;
}

// #386: one current move. The cases below are the overlaps the independent flags allowed.
namespace GFCurrentMoveTestsPrivate
{
	int32 GFActiveMoveCount(const FGFStaminaState& State)
	{
		return (State.bDashing ? 1 : 0)
			+ (State.bGripping ? 1 : 0)
			+ (State.bWallRunning ? 1 : 0)
			+ (State.bWallClimbing ? 1 : 0)
			+ (State.bMantling ? 1 : 0)
			+ (State.bSwiveling ? 1 : 0)
			+ (State.bSwinging ? 1 : 0)
			+ (State.bYanking ? 1 : 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementCurrentMoveTest, "GFGame.Movement.CurrentMove", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementCurrentMoveTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	using namespace GFWallClimbTestsPrivate;
	using namespace GFCurrentMoveTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();

	const FGFStaminaState Climbing = GFClimbingState(Tuning);
	TestTrue(TEXT("A climb is the current move"), Climbing.CurrentMove == EGFTraversalMove::WallClimb);
	TestEqual(TEXT("A climb is the only move"), GFActiveMoveCount(Climbing), 1);

	// Exit velocity owns the tick. The old step started a dash on top of the kick, launch, or release.
	FGFStaminaCommand ClimbKick = GFClimbCommand(0.05f, GFClimbInto, false);
	ClimbKick.bJumpPressed = true;
	ClimbKick.bDashPressed = true;
	const FGFStaminaState Kicked = GFStaminaSim::Step(Climbing, ClimbKick, Tuning);
	TestFalse(TEXT("A climb kick is not also a dash"), Kicked.bDashing);
	TestTrue(TEXT("A climb kick keeps its exit velocity"), Kicked.bOverrideVelocity && !Kicked.OverrideVelocity.IsNearlyZero());
	TestTrue(TEXT("A climb kick leaves no move in charge"), Kicked.CurrentMove == EGFTraversalMove::None);
	TestEqual(TEXT("A climb kick is one writer"), GFActiveMoveCount(Kicked), 0);

	FGFStaminaState Grip = GFMovementFullState();
	Grip.bGripping = true;
	Grip.GripTimeLeft = 1.f;
	Grip.GripNormal = FVector(1.0, 0.0, 0.0);
	Grip.bWasGrounded = false;
	FGFStaminaCommand GripKick = GFMovementCommand(0.05f);
	GripKick.bGrounded = false;
	GripKick.bJumpPressed = true;
	GripKick.bDashPressed = true;
	const FGFStaminaState GripKicked = GFStaminaSim::Step(Grip, GripKick, Tuning);
	TestFalse(TEXT("A cling kick is not also a dash"), GripKicked.bDashing);
	TestTrue(TEXT("A cling kick keeps its exit velocity"), GripKicked.bOverrideVelocity && !GripKicked.bGripping);
	TestTrue(TEXT("A cling kick leaves no move in charge"), GripKicked.CurrentMove == EGFTraversalMove::None);

	FGFStaminaState Orbit = GFMovementFullState();
	FGFStaminaCommand Grab = GFMovementCommand(0.05f);
	Grab.bGrounded = false;
	Grab.HorizontalSpeed = 500.f;
	Grab.PlayerLocation = FVector(100.0, 0.0, 120.0);
	Grab.MoveDirection = FVector(0.0, 1.0, 0.0);
	Grab.Contact.bSwivelContact = true;
	Grab.Contact.SwivelOrigin = FVector(0.0, 0.0, 120.0);
	Orbit = GFStaminaSim::Step(Orbit, Grab, Tuning);
	TestTrue(TEXT("A pole orbit is the current move"), Orbit.CurrentMove == EGFTraversalMove::Swivel);
	FGFStaminaCommand Launch = Grab;
	Launch.bJumpPressed = true;
	Launch.bDashPressed = true;
	Launch.PlayerLocation = Orbit.TravelLocation;
	const FGFStaminaState Launched = GFStaminaSim::Step(Orbit, Launch, Tuning);
	TestFalse(TEXT("A pole launch is not also a dash"), Launched.bDashing);
	TestTrue(TEXT("A pole launch keeps its exit velocity"), Launched.bOverrideVelocity && !Launched.bSwiveling);
	TestTrue(TEXT("A pole launch leaves no move in charge"), Launched.CurrentMove == EGFTraversalMove::None);

	FGFStaminaState Rope = GFMovementFullState();
	Rope.bSwinging = true;
	Rope.bWasGrounded = false;
	Rope.GrappleAnchor = FVector(0.0, 0.0, 500.0);
	Rope.TravelLocation = FVector::ZeroVector;
	Rope.RopeLength = 500.f;
	Rope.SwingVelocity = FVector(400.0, 0.0, 0.0);
	FGFStaminaCommand Release = GFMovementCommand(0.05f);
	Release.bGrounded = false;
	Release.bDashPressed = true;
	const FGFStaminaState Released = GFStaminaSim::Step(Rope, Release, Tuning);
	TestFalse(TEXT("A rope release is not also a dash"), Released.bDashing);
	TestTrue(TEXT("A rope release keeps its exit velocity"), Released.bOverrideVelocity && !Released.bSwinging);
	TestTrue(TEXT("A rope release leaves no move in charge"), Released.CurrentMove == EGFTraversalMove::None);

	// A yank that arrives with a zero stop used to count as busy and block the climb on that same tick.
	FGFStaminaState Arrived = GFMovementFullState();
	Arrived.bYanking = true;
	Arrived.GrappleAnchor = FVector(0.0, 0.0, 100.0);
	Arrived.TravelLocation = FVector(0.0, 0.0, 100.0);
	FGFStaminaCommand Into = GFClimbCommand(0.05f, GFClimbInto, true);
	Into.PlayerLocation = FVector(0.0, 0.0, 100.0);
	const FGFStaminaState Climbed = GFStaminaSim::Step(Arrived, Into, Tuning);
	TestTrue(TEXT("Arriving on a yank does not block the climb"), Climbed.bWallClimbing && !Climbed.bYanking);
	TestTrue(TEXT("The climb is the current move"), Climbed.CurrentMove == EGFTraversalMove::WallClimb);
	TestEqual(TEXT("The climb is the only move"), GFActiveMoveCount(Climbed), 1);

	// The same arrival with no wall lets the dash start, and the zero stop does not stay on as a second writer.
	FGFStaminaState Stopped = GFMovementFullState();
	Stopped.bYanking = true;
	Stopped.bWasGrounded = false;
	Stopped.GrappleAnchor = FVector(0.0, 0.0, 100.0);
	Stopped.TravelLocation = FVector(0.0, 0.0, 100.0);
	FGFStaminaCommand Dash = GFMovementCommand(0.05f);
	Dash.bGrounded = false;
	Dash.bDashPressed = true;
	Dash.PlayerLocation = FVector(0.0, 0.0, 100.0);
	const FGFStaminaState Dashed = GFStaminaSim::Step(Stopped, Dash, Tuning);
	TestTrue(TEXT("A dash after a yank stop is the current move"), Dashed.bDashing && !Dashed.bYanking);
	TestFalse(TEXT("A dash does not keep the zero stop"), Dashed.bOverrideVelocity);
	TestTrue(TEXT("The dash is the current move"), Dashed.CurrentMove == EGFTraversalMove::Dash);
	TestEqual(TEXT("The dash is the only move"), GFActiveMoveCount(Dashed), 1);

	// A block always clears, including stamina: the old step kept running and kept climbing.
	FGFStaminaState BlockedRun = GFMovementFullState();
	BlockedRun.bWallRunning = true;
	BlockedRun.bWallRunBlocked = true;
	BlockedRun.bWasGrounded = false;
	BlockedRun.WallRunTimeLeft = Tuning.WallRunMaxSeconds;
	BlockedRun.WallRunSpeed = 500.f;
	FGFStaminaCommand Stay = GFMovementCommand(0.2f);
	Stay.bGrounded = false;
	Stay.HorizontalSpeed = 500.f;
	Stay.MoveDirection = FVector(0.0, 1.0, 0.0);
	Stay.Contact.bWallContact = true;
	Stay.Contact.WallNormal = FVector(1.0, 0.0, 0.0);
	const float RunStamina = BlockedRun.Stamina;
	const FGFStaminaState RunCleared = GFStaminaSim::Step(BlockedRun, Stay, Tuning);
	TestFalse(TEXT("A wall run block clears the run"), RunCleared.bWallRunning);
	TestTrue(TEXT("The wall run visit stays closed"), RunCleared.bWallRunBlocked);
	TestTrue(TEXT("A blocked wall run spends nothing"), GFMovementNear(RunCleared.Stamina, RunStamina));
	TestTrue(TEXT("A blocked wall run is not the current move"), RunCleared.CurrentMove == EGFTraversalMove::None);

	FGFStaminaState BlockedClimb = GFMovementFullState();
	BlockedClimb.bWallClimbing = true;
	BlockedClimb.bWallClimbBlocked = true;
	BlockedClimb.bWasGrounded = false;
	BlockedClimb.WallClimbTimeLeft = Tuning.WallClimbMaxSeconds;
	BlockedClimb.WallClimbCoyoteLeft = Tuning.WallClimbCoyoteSeconds;
	BlockedClimb.WallClimbNormal = GFClimbNormal;
	FGFStaminaCommand Hold = GFClimbCommand(0.2f, GFClimbInto, false);
	const float ClimbStamina = BlockedClimb.Stamina;
	const FGFStaminaState ClimbCleared = GFStaminaSim::Step(BlockedClimb, Hold, Tuning);
	TestFalse(TEXT("A wall climb block clears the climb"), ClimbCleared.bWallClimbing);
	TestTrue(TEXT("The wall climb visit stays closed"), ClimbCleared.bWallClimbBlocked);
	TestTrue(TEXT("A blocked wall climb spends nothing"), GFMovementNear(ClimbCleared.Stamina, ClimbStamina));
	TestFalse(TEXT("A blocked wall climb does not drive velocity"), ClimbCleared.bOverrideVelocity);
	TestTrue(TEXT("A blocked wall climb is not the current move"), ClimbCleared.CurrentMove == EGFTraversalMove::None);

	// Flags the old step left set together.
	FGFStaminaState BothRopes = GFMovementFullState();
	BothRopes.bSwinging = true;
	BothRopes.bYanking = true;
	BothRopes.bWasGrounded = false;
	BothRopes.GrappleAnchor = FVector(0.0, 0.0, 500.0);
	BothRopes.TravelLocation = FVector::ZeroVector;
	BothRopes.RopeLength = 500.f;
	BothRopes.SwingVelocity = FVector(100.0, 0.0, 0.0);
	FGFStaminaCommand HoldRope = GFMovementCommand(0.05f);
	HoldRope.bGrounded = false;
	HoldRope.bGrappleSwingHeld = true;
	const FGFStaminaState YankWon = GFStaminaSim::Step(BothRopes, HoldRope, Tuning);
	TestTrue(TEXT("Yank and swing collapse to the yank"), YankWon.bYanking && !YankWon.bSwinging);
	TestTrue(TEXT("The yank is the current move"), YankWon.CurrentMove == EGFTraversalMove::Yank);
	TestEqual(TEXT("Yank and swing are not both in charge"), GFActiveMoveCount(YankWon), 1);

	FGFStaminaState BothCling = GFMovementFullState();
	BothCling.bGripping = true;
	BothCling.bDashing = true;
	BothCling.DashTimeLeft = 0.2f;
	BothCling.GripTimeLeft = 1.f;
	BothCling.GripNormal = FVector(1.0, 0.0, 0.0);
	BothCling.bWasGrounded = false;
	FGFStaminaCommand HoldCling = GFMovementCommand(0.05f);
	HoldCling.bGrounded = false;
	FGFStaminaState ClingWon = GFStaminaSim::Step(BothCling, HoldCling, Tuning);
	TestTrue(TEXT("Grip and dash collapse to the grip"), ClingWon.bGripping && !ClingWon.bDashing);
	TestTrue(TEXT("The dash timer does not survive the grip"), ClingWon.DashTimeLeft <= 0.f);
	TestTrue(TEXT("The grip is the current move"), ClingWon.CurrentMove == EGFTraversalMove::Grip);
	ClingWon = GFStaminaSim::Step(ClingWon, HoldCling, Tuning);
	TestFalse(TEXT("The next tick does not revive the dash"), ClingWon.bDashing);
	TestTrue(TEXT("The grip is still the current move"), ClingWon.bGripping && ClingWon.CurrentMove == EGFTraversalMove::Grip);

	FGFStaminaState BothWall = GFMovementFullState();
	BothWall.bMantling = true;
	BothWall.bWallClimbing = true;
	BothWall.MantleTimeLeft = Tuning.WallClimbMantleMaxSeconds;
	BothWall.MantleTarget = FVector(100.0, 0.0, 400.0);
	BothWall.bWasGrounded = false;
	FGFStaminaCommand Rise = GFMovementCommand(0.05f);
	Rise.bGrounded = false;
	Rise.PlayerLocation = FVector(0.0, 0.0, 88.0);
	const FGFStaminaState MantleWon = GFStaminaSim::Step(BothWall, Rise, Tuning);
	TestTrue(TEXT("Mantle and climb collapse to the mantle"), MantleWon.bMantling && !MantleWon.bWallClimbing);
	TestTrue(TEXT("The mantle is the current move"), MantleWon.CurrentMove == EGFTraversalMove::Mantle);
	TestEqual(TEXT("Mantle and climb are not both in charge"), GFActiveMoveCount(MantleWon), 1);
	return true;
}

// #401: a ground press in the start clearing. Trees and rocks sit just outside the open floor, and the floor
// check flickers, so the press lands in the climb coyote window or on a climb that has not left the ground.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementJumpCoyoteWallTest, "GFGame.Movement.Jump.CoyoteWall", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementJumpCoyoteWallTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	using namespace GFWallClimbTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();

	FGFStaminaState Armed = GFStaminaSim::Step(GFMovementFullState(), GFMovementCommand(0.05f), Tuning);
	TestFalse(TEXT("Standing is not a climb"), Armed.bWallClimbing);
	TestTrue(TEXT("Standing arms coyote"), GFMovementNear(Armed.WallClimbCoyoteLeft, Tuning.WallClimbCoyoteSeconds));

	FGFStaminaCommand CoyoteJump = GFClimbCommand(0.05f, GFClimbInto, false);
	CoyoteJump.bJumpPressed = true;
	const FGFStaminaState CoyoteHop = GFStaminaSim::Step(Armed, CoyoteJump, Tuning);
	TestTrue(TEXT("A jump in the coyote window into a wall is accepted"), CoyoteHop.bJumpAccepted);
	TestFalse(TEXT("That press is not a kick"), CoyoteHop.bTraversalJump);
	TestFalse(TEXT("That press does not climb"), CoyoteHop.bWallClimbing);
	TestTrue(TEXT("Coyote hop matches jump speed"), GFMovementNear(CoyoteHop.JumpZSpeed, Tuning.JumpZVelocity));
	TestEqual(TEXT("Coyote hop is jump 1"), CoyoteHop.JumpsUsed, 1);
	TestTrue(TEXT("Coyote hop leaves no move in charge"), CoyoteHop.CurrentMove == EGFTraversalMove::None);

	const FGFStaminaState Climbing = GFClimbingState(Tuning);
	TestTrue(TEXT("Setup is a grounded climb"), Climbing.bWallClimbing && Climbing.CurrentMove == EGFTraversalMove::WallClimb);
	FGFStaminaCommand GroundJump = GFClimbCommand(0.05f, GFClimbInto, true);
	GroundJump.bJumpPressed = true;
	const FGFStaminaState GroundHop = GFStaminaSim::Step(Climbing, GroundJump, Tuning);
	TestTrue(TEXT("A jump while still on the floor is accepted"), GroundHop.bJumpAccepted);
	TestFalse(TEXT("A grounded climb press is not a kick"), GroundHop.bTraversalJump);
	TestFalse(TEXT("The hop leaves the climb"), GroundHop.bWallClimbing);
	TestTrue(TEXT("Grounded climb hop matches jump speed"), GFMovementNear(GroundHop.JumpZSpeed, Tuning.JumpZVelocity));
	TestEqual(TEXT("Grounded climb hop is jump 1"), GroundHop.JumpsUsed, 1);
	TestTrue(TEXT("Grounded climb hop leaves no move in charge"), GroundHop.CurrentMove == EGFTraversalMove::None);
	TestFalse(TEXT("The hop does not keep the climb speed"), GroundHop.bOverrideVelocity);

	FGFStaminaCommand StillIn = GFClimbCommand(0.05f, GFClimbInto, false);
	const FGFStaminaState After = GFStaminaSim::Step(GroundHop, StillIn, Tuning);
	TestFalse(TEXT("Holding into the wall after the hop does not re-climb"), After.bWallClimbing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMovementJumpPlainGroundTest, "GFGame.Movement.Jump.PlainGround", GFMovementTestsPrivate::GFMovementFlags)
bool FGFMovementJumpPlainGroundTest::RunTest(const FString&)
{
	using namespace GFMovementTestsPrivate;
	const UGFMovementTuning& Tuning = GFMovementTuningCDO();

	FGFStaminaCommand Hop = GFMovementCommand(0.05f);
	Hop.bJumpPressed = true;
	Hop.bGrounded = true;
	const FGFStaminaState State = GFStaminaSim::Step(GFMovementFullState(), Hop, Tuning);
	TestTrue(TEXT("A plain grounded press is accepted"), State.bJumpAccepted);
	TestFalse(TEXT("A plain grounded press is not a kick"), State.bTraversalJump);
	TestFalse(TEXT("A plain grounded press does not climb"), State.bWallClimbing || State.bMantling || State.bGripping);
	TestTrue(TEXT("Plain hop matches tuning jump speed"), GFMovementNear(State.JumpZSpeed, Tuning.JumpZVelocity));
	TestEqual(TEXT("Plain hop speed is the tuning value"), State.JumpZSpeed, Tuning.JumpZVelocity);
	TestEqual(TEXT("Plain hop is jump 1"), State.JumpsUsed, 1);
	TestTrue(TEXT("Plain hop is free"), GFMovementNear(State.Stamina, Tuning.MaxStamina));
	TestTrue(TEXT("Plain hop leaves no move in charge"), State.CurrentMove == EGFTraversalMove::None);
	return true;
}

#endif
