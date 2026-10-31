// Aim, crosshair and shoulder camera. Pure data plus the character defaults.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Aim;Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "Aim/GFAimSim.h"
#include "GFCharacter.h"
#include "GFSaveGame.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AimFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool Near(float A, float B, float Tol = 0.02f)
	{
		return FMath::IsNearlyEqual(A, B, Tol);
	}

	int32 CountKind(const FGFCrosshairDraw& Draw, EGFCrosshairMarkKind Kind, bool bOutline)
	{
		int32 Count = 0;
		for (const FGFCrosshairMark& Mark : Draw.Marks)
		{
			if (Mark.Kind == Kind && Mark.bOutline == bOutline)
			{
				++Count;
			}
		}
		return Count;
	}

	const FGFCrosshairMark* FirstLine(const FGFCrosshairDraw& Draw)
	{
		for (const FGFCrosshairMark& Mark : Draw.Marks)
		{
			if (Mark.Kind == EGFCrosshairMarkKind::Line && !Mark.bOutline)
			{
				return &Mark;
			}
		}
		return nullptr;
	}

	bool LineIs(const FGFCrosshairMark& Mark, float X, float Y, float X2, float Y2)
	{
		return Near(Mark.X, X) && Near(Mark.Y, Y) && Near(Mark.X2, X2) && Near(Mark.Y2, Y2);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimCrosshairDefaultsTest, "GFGame.Aim.CrosshairDefaults", AimFlags)
bool FGFAimCrosshairDefaultsTest::RunTest(const FString&)
{
	const FGFCrosshairSettings Defaults;
	TestTrue(TEXT("Shape is the old plus"), Defaults.Shape == EGFCrosshairShape::Plus);
	TestTrue(TEXT("White"), Defaults.Color.Equals(FLinearColor::White));
	TestEqual(TEXT("Arm 10"), Defaults.Size, 10.f);
	TestEqual(TEXT("Gap 4"), Defaults.Gap, 4.f);
	TestEqual(TEXT("Thickness 1.5"), Defaults.Thickness, 1.5f);
	TestEqual(TEXT("Alpha 0.9"), Defaults.Opacity, 0.9f);
	TestFalse(TEXT("Outline off"), Defaults.bOutline);
	TestFalse(TEXT("Centre dot off"), Defaults.bCenterDot);
	TestFalse(TEXT("Dynamic spread off"), Defaults.bDynamicSpread);
	TestTrue(TEXT("ADS crosshair on"), Defaults.bSeparateAdsCrosshair);

	const float CX = 640.f;
	const float CY = 360.f;
	const FGFCrosshairDraw Draw = GFBuildCrosshair(Defaults, CX, CY, 0.f);
	TestEqual(TEXT("Four arms, no outline, no dot"), Draw.Marks.Num(), 4);
	TestTrue(TEXT("Left arm"), Draw.Marks.Num() == 4 && LineIs(Draw.Marks[0], CX - 10.f, CY, CX - 4.f, CY));
	TestTrue(TEXT("Right arm"), Draw.Marks.Num() == 4 && LineIs(Draw.Marks[1], CX + 4.f, CY, CX + 10.f, CY));
	TestTrue(TEXT("Up arm"), Draw.Marks.Num() == 4 && LineIs(Draw.Marks[2], CX, CY - 10.f, CX, CY - 4.f));
	TestTrue(TEXT("Down arm"), Draw.Marks.Num() == 4 && LineIs(Draw.Marks[3], CX, CY + 4.f, CX, CY + 10.f));
	if (Draw.Marks.Num() == 4)
	{
		TestTrue(TEXT("Thickness 1.5"), Near(Draw.Marks[0].Thickness, 1.5f));
		TestTrue(TEXT("White ink"), Draw.Marks[0].Color.Equals(FLinearColor(1.f, 1.f, 1.f, 0.9f), 0.001f));
	}
	TestEqual(TEXT("Moving does not spread a fresh crosshair"), GFCrosshairGap(Defaults, true, true), 4.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimCrosshairShapesTest, "GFGame.Aim.CrosshairShapes", AimFlags)
bool FGFAimCrosshairShapesTest::RunTest(const FString&)
{
	auto Make = [](EGFCrosshairShape Shape)
	{
		FGFCrosshairSettings Settings;
		Settings.Shape = Shape;
		return GFBuildCrosshair(Settings, 100.f, 100.f, 0.f);
	};
	TestEqual(TEXT("Dot is one dot"), CountKind(Make(EGFCrosshairShape::Dot), EGFCrosshairMarkKind::Dot, false), 1);
	TestEqual(TEXT("Dot has no line"), CountKind(Make(EGFCrosshairShape::Dot), EGFCrosshairMarkKind::Line, false), 0);
	TestEqual(TEXT("Circle segments"), CountKind(Make(EGFCrosshairShape::Circle), EGFCrosshairMarkKind::Line, false), 24);
	TestEqual(TEXT("Plus and a dot"), CountKind(Make(EGFCrosshairShape::PlusDot), EGFCrosshairMarkKind::Line, false), 4);
	TestEqual(TEXT("PlusDot dot"), CountKind(Make(EGFCrosshairShape::PlusDot), EGFCrosshairMarkKind::Dot, false), 1);
	TestEqual(TEXT("T has three arms"), CountKind(Make(EGFCrosshairShape::T), EGFCrosshairMarkKind::Line, false), 3);
	TestEqual(TEXT("Chevron has two strokes"), CountKind(Make(EGFCrosshairShape::Chevron), EGFCrosshairMarkKind::Line, false), 2);

	FGFCrosshairSettings Outlined;
	Outlined.bOutline = true;
	Outlined.OutlinePx = 2.f;
	Outlined.bCenterDot = true;
	const FGFCrosshairDraw Ink = GFBuildCrosshair(Outlined, 50.f, 50.f, 0.f);
	TestEqual(TEXT("Outline under each arm"), CountKind(Ink, EGFCrosshairMarkKind::Line, true), 4);
	TestEqual(TEXT("Centre dot outline"), CountKind(Ink, EGFCrosshairMarkKind::Dot, true), 1);
	TestTrue(TEXT("Outline is black and thicker"), Ink.Marks.Num() > 0 && Ink.Marks[0].bOutline && Ink.Marks[0].Color.R < 0.01f && Ink.Marks[0].Thickness > Outlined.Thickness);

	FGFCrosshairSettings Spread = FGFCrosshairSettings::Defaults();
	Spread.bDynamicSpread = true;
	TestEqual(TEXT("Spread while moving"), GFCrosshairGap(Spread, true, false), 12.f);
	TestEqual(TEXT("Spread while firing"), GFCrosshairGap(Spread, false, true), 12.f);
	TestEqual(TEXT("Still gap when idle"), GFCrosshairGap(Spread, false, false), 4.f);
	const FGFCrosshairDraw Wide = GFBuildCrosshair(Spread, 0.f, 0.f, 8.f);
	TestTrue(TEXT("Wider gap"), Wide.Marks.Num() == 4 && Near(Wide.Marks[0].X2, -4.f - 8.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimShareCodeTest, "GFGame.Aim.ShareCode", AimFlags)
bool FGFAimShareCodeTest::RunTest(const FString&)
{
	FGFCrosshairSettings Imported;
	Imported.Size = 33.f;
	TestTrue(TEXT("Short code imports"), FGFCrosshairSettings::FromShareCode(TEXT("GF-X:plus/ffffff/10/4/1.5/90"), Imported));
	TestTrue(TEXT("Short code is today's plus"), Imported.Matches(FGFCrosshairSettings::Defaults()));

	FGFCrosshairSettings Weird;
	Weird.Shape = EGFCrosshairShape::Chevron;
	Weird.Color = FLinearColor::FromSRGBColor(FColor(255, 0, 255));
	Weird.Size = 22.f;
	Weird.Gap = 0.f;
	Weird.Thickness = 6.f;
	Weird.Opacity = 0.4f;
	Weird.bOutline = true;
	Weird.OutlinePx = 2.f;
	Weird.bCenterDot = true;
	Weird.bDynamicSpread = true;
	Weird.bSeparateAdsCrosshair = false;
	const FString Code = Weird.ToShareCode();
	FGFCrosshairSettings Back;
	TestTrue(TEXT("Full code imports"), FGFCrosshairSettings::FromShareCode(Code, Back));
	TestTrue(TEXT("Export then import matches"), Back.Matches(Weird));
	TestTrue(TEXT("Code starts with the prefix"), Code.StartsWith(TEXT("GF-X:chevron/ff00ff/")));

	FGFCrosshairSettings Untouched = Weird;
	TestFalse(TEXT("Rejects a bare sentence"), FGFCrosshairSettings::FromShareCode(TEXT("please be a crosshair"), Untouched));
	TestFalse(TEXT("Rejects an unknown shape"), FGFCrosshairSettings::FromShareCode(TEXT("GF-X:banana/ffffff/10/4/1.5/90"), Untouched));
	TestFalse(TEXT("Rejects a bad colour"), FGFCrosshairSettings::FromShareCode(TEXT("GF-X:plus/gggggg/10/4/1.5/90"), Untouched));
	TestTrue(TEXT("A rejected code does not clobber"), Untouched.Matches(Weird));

	FGFCrosshairSettings Clamped;
	TestTrue(TEXT("Oversized values import"), FGFCrosshairSettings::FromShareCode(TEXT("GF-X:plus/ffffff/400/80/0.1/500"), Clamped));
	TestEqual(TEXT("Size clamps to 40"), Clamped.Size, 40.f);
	TestEqual(TEXT("Gap clamps to 20"), Clamped.Gap, 20.f);
	TestEqual(TEXT("Thickness clamps to 1"), Clamped.Thickness, 1.f);
	TestEqual(TEXT("Opacity clamps to 1"), Clamped.Opacity, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimCrosshairSaveTest, "GFGame.Aim.CrosshairSave", AimFlags)
bool FGFAimCrosshairSaveTest::RunTest(const FString&)
{
	UGFCharacterSave* Save = NewObject<UGFCharacterSave>();
	TestTrue(TEXT("Fresh character crosshair is the default plus"), Save->Character.Crosshair.Matches(FGFCrosshairSettings::Defaults()));
	TestTrue(TEXT("Fresh aim prefs hold to aim"), Save->Character.AimUser.bHoldToAim && Save->Character.AimUser.bScopeOverlay);
	TestEqual(TEXT("Fresh sensitivity is 100%"), Save->Character.AimUser.SensitivityScale, 1.f);

	Save->Character.Crosshair.Shape = EGFCrosshairShape::Circle;
	Save->Character.Crosshair.Color = FLinearColor::FromSRGBColor(FColor(255, 255, 0));
	Save->Character.Crosshair.Size = 18.f;
	Save->Character.Crosshair.Gap = 2.f;
	Save->Character.Crosshair.Thickness = 3.f;
	Save->Character.Crosshair.Opacity = 0.55f;
	Save->Character.Crosshair.bOutline = true;
	Save->Character.Crosshair.OutlinePx = 2.f;
	Save->Character.Crosshair.bCenterDot = true;
	Save->Character.Crosshair.bDynamicSpread = true;
	Save->Character.Crosshair.bSeparateAdsCrosshair = false;
	Save->Character.AimUser.bHoldToAim = false;
	Save->Character.AimUser.SensitivityScale = 1.25f;
	Save->Character.AimUser.bScopeOverlay = false;
	Save->SaveVersion = static_cast<int32>(EGFSaveVersion::Latest);

	TArray<uint8> Bytes;
	TestTrue(TEXT("SaveGameToMemory"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	UGFCharacterSave* Loaded = Cast<UGFCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Loaded character save"), Loaded))
	{
		return false;
	}
	TestTrue(TEXT("Crosshair survives save and load"), Loaded->Character.Crosshair.Matches(Save->Character.Crosshair));
	TestTrue(TEXT("Aim prefs survive save and load"), Loaded->Character.AimUser.Matches(Save->Character.AimUser));
	TestEqual(TEXT("Save version is the crosshair version"), Loaded->SaveVersion, static_cast<int32>(EGFSaveVersion::Crosshair));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimShoulderTest, "GFGame.Aim.Shoulder", AimFlags)
bool FGFAimShoulderTest::RunTest(const FString&)
{
	TestTrue(TEXT("Shoulder swap is middle mouse"), AGFCharacter::DefaultShoulderSwapKey() == EKeys::MiddleMouseButton);
	TestTrue(TEXT("Q stays free for the grapple"), AGFCharacter::DefaultShoulderSwapKey() != EKeys::Q);

	FGFAimState State;
	const FGFAimTuning Tuning;
	const FVector Right = GFShoulderSocketOffset(State, Tuning);
	TestTrue(TEXT("Default offset puts the camera on the character's right"), Right.Equals(FVector(0.f, 60.f, 20.f)));

	GFAimPush(State, Tuning, EGFAimEvent::ShoulderSwap);
	TestTrue(TEXT("Swap starts at the current shoulder"), GFShoulderSocketOffset(State, Tuning).Equals(FVector(0.f, 60.f, 20.f), 0.01f));
	GFAimAdvance(State, Tuning, 0.075f);
	TestTrue(TEXT("Halfway through the 0.15s blend"), Near(GFShoulderSocketOffset(State, Tuning).Y, 0.f, 0.5f));
	GFAimAdvance(State, Tuning, 0.075f);
	TestTrue(TEXT("Other shoulder"), GFShoulderSocketOffset(State, Tuning).Equals(FVector(0.f, -60.f, 20.f), 0.01f));

	GFAimPush(State, Tuning, EGFAimEvent::ShoulderSwap);
	GFAimAdvance(State, Tuning, GFAim::ShoulderBlendSeconds);
	TestTrue(TEXT("Swap back"), GFShoulderSocketOffset(State, Tuning).Equals(FVector(0.f, 60.f, 20.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimParallaxTest, "GFGame.Aim.Parallax", AimFlags)
bool FGFAimParallaxTest::RunTest(const FString&)
{
	FGFAimShot Shot;
	Shot.CameraOrigin = FVector(0.f, 60.f, 100.f);
	Shot.CameraDir = FVector(1.f, 0.f, 0.f);
	Shot.Muzzle = FVector(40.f, 0.f, 90.f);
	Shot.MaxRange = GFAim::MaxAimRange;

	const int32 TargetId = 1;
	const int32 WallId = 2;
	TArray<FGFAimSphere> Clear;
	Clear.Add({ FVector(3000.f, 60.f, 100.f), 20.f, TargetId });
	Clear.Add({ FVector(150.f, 100.f, 100.f), 30.f, WallId });
	const FGFAimShotResult Open = GFResolveShot(Shot, Clear);
	TestEqual(TEXT("Camera looks at the far target"), Open.CameraHitId, TargetId);
	TestEqual(TEXT("The wall beside the body is not the shot"), Open.ImpactId, TargetId);
	TestFalse(TEXT("Nothing blocks the muzzle"), Open.bBlocked);
	TestTrue(TEXT("Impact is on the target, 30m out"), Open.Impact.X > 2900.f && Open.Impact.X < 3000.f);

	const FVector Aim = Open.AimPoint;
	const FVector Blocker = (Shot.Muzzle + Aim) * 0.5f;
	TArray<FGFAimSphere> Blocked = Clear;
	Blocked.Add({ Blocker, 15.f, 3 });
	const FGFAimShotResult Stopped = GFResolveShot(Shot, Blocked);
	TestEqual(TEXT("Camera still sees the target"), Stopped.CameraHitId, TargetId);
	TestEqual(TEXT("Muzzle hits the obstacle"), Stopped.ImpactId, 3);
	TestTrue(TEXT("Blocked flag"), Stopped.bBlocked);
	TestTrue(TEXT("Impact is the obstacle, not the target"), FVector::Dist(Stopped.Impact, Blocker) < 16.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimRightClickTest, "GFGame.Aim.RightClick", AimFlags)
bool FGFAimRightClickTest::RunTest(const FString&)
{
	TestTrue(TEXT("A block places"), GFResolvePrimaryUse(EGFAimHeldKind::Block, false) == EGFTriggerUse::PlaceBlock);
	TestTrue(TEXT("Fists aim"), GFResolvePrimaryUse(EGFAimHeldKind::Fists, false) == EGFTriggerUse::Aim);
	TestTrue(TEXT("A weapon aims"), GFResolvePrimaryUse(EGFAimHeldKind::Weapon, false) == EGFTriggerUse::Aim);
	TestTrue(TEXT("An ability aims"), GFResolvePrimaryUse(EGFAimHeldKind::Ability, false) == EGFTriggerUse::Aim);
	TestTrue(TEXT("A possessed arena dino aims even with a block selected"), GFResolvePrimaryUse(EGFAimHeldKind::Block, true) == EGFTriggerUse::Aim);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimQuickscopeTest, "GFGame.Aim.Quickscope", AimFlags)
bool FGFAimQuickscopeTest::RunTest(const FString&)
{
	const FGFAimTuning Tuning;
	FGFAimState State;
	State.Held = EGFAimHeldKind::Weapon;
	GFAimPush(State, Tuning, EGFAimEvent::AimPressed);
	const FGFAimStepOut Shot = GFAimPush(State, Tuning, EGFAimEvent::FirePressed);
	TestTrue(TEXT("Same-frame shot fires"), Shot.bFired);
	TestTrue(TEXT("Uses the ADS ray before the zoom finishes"), Shot.bAdsRay && State.PhaseAlpha < 0.01f);
	TestTrue(TEXT("Uses ADS accuracy"), Shot.bAdsAccuracy);
	TestTrue(TEXT("No quickscope penalty"), Near(Shot.Spread, Tuning.AdsSpread) && Shot.Spread < Tuning.HipSpread);
	TestFalse(TEXT("The click is not eaten"), Shot.bInputEaten);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimNoMinScopeTest, "GFGame.Aim.NoMinScope", AimFlags)
bool FGFAimNoMinScopeTest::RunTest(const FString&)
{
	const FGFAimTuning Tuning;
	auto SpreadAfter = [&Tuning](float Seconds)
	{
		FGFAimState State;
		State.Held = EGFAimHeldKind::Weapon;
		GFAimPush(State, Tuning, EGFAimEvent::AimPressed);
		GFAimAdvance(State, Tuning, Seconds);
		return GFAimPush(State, Tuning, EGFAimEvent::FirePressed).Spread;
	};
	const float Early = SpreadAfter(0.01f);
	const float Late = SpreadAfter(2.f);
	TestTrue(TEXT("0.01s matches a settled scope"), Near(Early, Late));
	TestTrue(TEXT("Both are the ADS spread"), Near(Early, Tuning.AdsSpread));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimTimingTest, "GFGame.Aim.AdsTiming", AimFlags)
bool FGFAimTimingTest::RunTest(const FString&)
{
	const FGFAimTuning Tuning;
	FGFAimState State;
	State.Held = EGFAimHeldKind::Weapon;
	GFAimPush(State, Tuning, EGFAimEvent::AimPressed);
	TestTrue(TEXT("Enter starts on the press"), State.Phase == EGFAdsPhase::Entering);
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		GFAimAdvance(State, Tuning, GFAim::FrameSeconds);
	}
	TestTrue(TEXT("Still entering one frame early"), State.Phase == EGFAdsPhase::Entering);
	TestTrue(TEXT("Not fully zoomed yet"), State.PhaseAlpha < 0.95f);
	GFAimAdvance(State, Tuning, GFAim::FrameSeconds);
	TestTrue(TEXT("Held at 0.10s"), State.Phase == EGFAdsPhase::Held);
	TestTrue(TEXT("Zoom finished"), Near(State.PhaseAlpha, 1.f, 0.001f));
	TestTrue(TEXT("Weapon FOV is 45"), Near(State.Fov, GFAim::WeaponAdsFov));

	GFAimPush(State, Tuning, EGFAimEvent::AimReleased);
	TestTrue(TEXT("Release starts the exit on that frame"), State.Phase == EGFAdsPhase::Leaving);
	TestTrue(TEXT("Exit has not skipped ahead"), Near(State.PhaseAlpha, 1.f, 0.001f));
	TestFalse(TEXT("Shots during the exit use the hip ray"), GFAimPush(State, Tuning, EGFAimEvent::FirePressed).bAdsRay);

	FGFAimState Leave;
	Leave.Held = EGFAimHeldKind::Weapon;
	GFAimPush(Leave, Tuning, EGFAimEvent::AimPressed);
	GFAimAdvance(Leave, Tuning, Tuning.AdsInSeconds);
	GFAimPush(Leave, Tuning, EGFAimEvent::AimReleased);
	for (int32 Frame = 0; Frame < 5; ++Frame)
	{
		GFAimAdvance(Leave, Tuning, GFAim::FrameSeconds);
	}
	TestTrue(TEXT("Still leaving one frame early"), Leave.Phase == EGFAdsPhase::Leaving);
	GFAimAdvance(Leave, Tuning, GFAim::FrameSeconds);
	TestTrue(TEXT("Hip after 0.10s"), Leave.Phase == EGFAdsPhase::Hip);
	TestTrue(TEXT("FOV back at 90"), Near(Leave.Fov, GFAim::HipFov));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimCancelTest, "GFGame.Aim.Cancel", AimFlags)
bool FGFAimCancelTest::RunTest(const FString&)
{
	const FGFAimTuning Tuning;
	FGFAimState State;
	State.Held = EGFAimHeldKind::Weapon;
	const FGFAimStepOut Fired = GFAimPush(State, Tuning, EGFAimEvent::FirePressed);
	TestTrue(TEXT("Fire lands"), Fired.bFired && !Fired.bInputEaten);
	TestTrue(TEXT("Fire recovery is open"), State.bHasRecovery && State.RecoveryLeft > 0.f);

	const FGFAimStepOut Aimed = GFAimPush(State, Tuning, EGFAimEvent::AimPressed);
	TestTrue(TEXT("ADS cancels the fire recovery"), Aimed.bCancelled && !Aimed.bInputEaten);
	TestTrue(TEXT("ADS started"), State.bAdsIntent);
	TestFalse(TEXT("Fire recovery is gone"), State.bHasRecovery);

	const int32 WeaponBefore = State.WeaponId;
	const FGFAimStepOut Swapped = GFAimPush(State, Tuning, EGFAimEvent::SwapWeapon);
	TestTrue(TEXT("Swap cancels the aim blend"), Swapped.bCancelled && !Swapped.bInputEaten);
	TestFalse(TEXT("Aim is interrupted"), State.bAdsIntent || State.Phase != EGFAdsPhase::Hip);
	TestEqual(TEXT("Weapon id advances"), State.WeaponId, WeaponBefore + 1);
	TestTrue(TEXT("Switch takes 0.15s"), State.bHasRecovery && Near(State.RecoveryLeft, GFAim::WeaponSwitchSeconds));

	const FGFAimStepOut Ability = GFAimPush(State, Tuning, EGFAimEvent::UseAbility);
	TestTrue(TEXT("Ability cancels the switch"), Ability.bCancelled && Ability.bAbility && !Ability.bInputEaten);
	TestFalse(TEXT("Switch recovery is gone"), State.bHasRecovery);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimInputBufferTest, "GFGame.Aim.InputBuffer", AimFlags)
bool FGFAimInputBufferTest::RunTest(const FString&)
{
	const FGFAimTuning Tuning;
	FGFAimState State;
	State.Held = EGFAimHeldKind::Weapon;
	GFAimPush(State, Tuning, EGFAimEvent::AimPressed);
	GFAimAdvance(State, Tuning, 0.02f);
	TestTrue(TEXT("0.08s of the blend remains"), State.Phase == EGFAdsPhase::Entering);
	const FGFAimStepOut Shot = GFAimPush(State, Tuning, EGFAimEvent::FirePressed);
	TestTrue(TEXT("The early click still fires"), Shot.bFired && !Shot.bInputEaten);
	TestTrue(TEXT("It is an ADS shot"), Shot.bAdsRay && Shot.bAdsAccuracy);
	TestTrue(TEXT("Same spread as a finished scope"), Near(Shot.Spread, Tuning.AdsSpread));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimFistsTest, "GFGame.Aim.Fists", AimFlags)
bool FGFAimFistsTest::RunTest(const FString&)
{
	const FGFAimTuning Tuning;
	FGFAimState Hip;
	Hip.Held = EGFAimHeldKind::Fists;
	const FGFAimStepOut HipPunch = GFAimPush(Hip, Tuning, EGFAimEvent::FirePressed);

	FGFAimState Scoped;
	Scoped.Held = EGFAimHeldKind::Fists;
	const FGFAimStepOut Enter = GFAimPush(Scoped, Tuning, EGFAimEvent::AimPressed);
	TestTrue(TEXT("Squint cue"), Enter.bFistCue);
	TestTrue(TEXT("Squint pose"), Scoped.Pose == GFAim::FistScopeAnim());
	TestTrue(TEXT("Target FOV is 80"), Near(Scoped.TargetFov, GFAim::FistAdsFov));
	TestFalse(TEXT("Fists do not get the sniper scope"), GFWantsScopeOverlay(Scoped));
	GFAimAdvance(Scoped, Tuning, Tuning.AdsInSeconds);
	TestTrue(TEXT("Zoom arrives at 80"), Near(Scoped.Fov, GFAim::FistAdsFov));
	const FGFAimStepOut ScopedPunch = GFAimPush(Scoped, Tuning, EGFAimEvent::FirePressed);
	TestTrue(TEXT("Scoped punch matches a hip punch"), Near(ScopedPunch.Damage, HipPunch.Damage));
	TestTrue(TEXT("Both use the hip punch damage"), Near(HipPunch.Damage, Tuning.HipPunchDamage));
	TestTrue(TEXT("Fist reach is the same number either way"), Near(Tuning.FistReach, GFAim::FistReach));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimSensitivityTest, "GFGame.Aim.Sensitivity", AimFlags)
bool FGFAimSensitivityTest::RunTest(const FString&)
{
	const float Ratio = FMath::Tan(FMath::DegreesToRadians(22.5f)) / FMath::Tan(FMath::DegreesToRadians(45.f));
	TestTrue(TEXT("45 degree scope at 100%"), Near(GFAimSensitivity(90.f, 45.f, 1.f), Ratio, 0.0001f));
	TestTrue(TEXT("Slider at 150%"), Near(GFAimSensitivity(90.f, 45.f, 1.5f), Ratio * 1.5f, 0.0001f));
	TestTrue(TEXT("Slider at 50%"), Near(GFAimSensitivity(90.f, 45.f, 0.5f), Ratio * 0.5f, 0.0001f));
	TestTrue(TEXT("Slider below 50% clamps"), Near(GFAimSensitivity(90.f, 45.f, 0.1f), Ratio * 0.5f, 0.0001f));
	TestTrue(TEXT("Sniper 20 degrees"), Near(GFAimSensitivity(90.f, 20.f, 1.f), FMath::Tan(FMath::DegreesToRadians(10.f)) / FMath::Tan(FMath::DegreesToRadians(45.f)), 0.0001f));

	FGFAimState Hip;
	const FGFAimTuning Tuning;
	GFAimAdvance(Hip, Tuning, 0.f);
	TestTrue(TEXT("Hip look is unchanged"), Near(Hip.LookScale, 1.f));
	Hip.Held = EGFAimHeldKind::Weapon;
	Hip.SensitivityScale = 1.25f;
	GFAimPush(Hip, Tuning, EGFAimEvent::AimPressed);
	TestTrue(TEXT("ADS look uses the formula and the slider"), Near(Hip.LookScale, GFAimSensitivity(90.f, 45.f, 1.25f), 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimEveryoneTest, "GFGame.Aim.AdsEveryone", AimFlags)
bool FGFAimEveryoneTest::RunTest(const FString&)
{
	const FGFAimTuning Tuning;
	FGFAimState Player;
	Player.Held = EGFAimHeldKind::Weapon;
	GFAimPush(Player, Tuning, EGFAimEvent::AimPressed);
	GFAimAdvance(Player, Tuning, Tuning.AdsInSeconds);
	TestTrue(TEXT("Player zoom is first-person FOV 45"), Near(Player.Fov, GFAim::WeaponAdsFov));
	TestTrue(TEXT("Player is in the aim camera"), Player.Phase == EGFAdsPhase::Held);

	FGFAimState Snout;
	Snout.Held = EGFAimHeldKind::Block;
	Snout.bSnoutZoom = true;
	TestTrue(TEXT("The dino's right click aims"), GFResolvePrimaryUse(Snout.Held, Snout.bSnoutZoom) == EGFTriggerUse::Aim);
	GFAimPush(Snout, Tuning, EGFAimEvent::AimPressed);
	GFAimAdvance(Snout, Tuning, Tuning.AdsInSeconds);
	TestTrue(TEXT("Snout zoom uses the same 45 degree aim"), Near(Snout.Fov, GFAim::WeaponAdsFov));
	TestFalse(TEXT("A dino does not play the fist squint"), Snout.bFistScope);

	FGFAimState Sniper;
	Sniper.Held = EGFAimHeldKind::Weapon;
	Sniper.Weapon = EGFWeaponProfile::Sniper;
	GFAimPush(Sniper, Tuning, EGFAimEvent::AimPressed);
	GFAimAdvance(Sniper, Tuning, Tuning.AdsInSeconds);
	TestTrue(TEXT("Sniper FOV is 20"), Near(Sniper.Fov, GFAim::SniperAdsFov));
	TestTrue(TEXT("Scope overlay defaults on"), GFWantsScopeOverlay(Sniper));
	TestFalse(TEXT("Overlay hides the crosshair"), GFShouldDrawCrosshair(FGFCrosshairSettings(), Sniper));
	Sniper.bScopeOverlayEnabled = false;
	TestFalse(TEXT("Overlay can be turned off"), GFWantsScopeOverlay(Sniper));
	TestTrue(TEXT("Separate ADS crosshair comes back"), GFShouldDrawCrosshair(FGFCrosshairSettings(), Sniper));
	FGFCrosshairSettings Hidden;
	Hidden.bSeparateAdsCrosshair = false;
	TestFalse(TEXT("Separate ADS crosshair can hide it"), GFShouldDrawCrosshair(Hidden, Sniper));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimVignetteTest, "GFGame.Aim.Vignette", AimFlags)
bool FGFAimVignetteTest::RunTest(const FString&)
{
	const TArray<FGFScreenRect> Fingers = GFFistVignette(800.f, 600.f);
	const TArray<FGFScreenRect> Scope = GFScopeVignette(800.f, 600.f);
	TestTrue(TEXT("Fingers have bands"), Fingers.Num() >= 4);
	TestTrue(TEXT("Scope has a ring"), Scope.Num() == 4);
	auto Covers = [](const TArray<FGFScreenRect>& Rects, float X, float Y)
	{
		for (const FGFScreenRect& Rect : Rects)
		{
			if (GFScreenRectContains(Rect, X, Y))
			{
				return true;
			}
		}
		return false;
	};
	TestFalse(TEXT("Fist centre stays clear"), Covers(Fingers, 400.f, 300.f));
	TestTrue(TEXT("Fist corner is covered"), Covers(Fingers, 2.f, 2.f));
	TestFalse(TEXT("Scope centre stays clear"), Covers(Scope, 400.f, 300.f));
	TestTrue(TEXT("Scope edge is covered"), Covers(Scope, 2.f, 300.f));
	TestEqual(TEXT("Empty canvas draws nothing"), GFFistVignette(0.f, 0.f).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAimCameraDefaultsTest, "GFGame.Aim.CameraDefaults", AimFlags)
bool FGFAimCameraDefaultsTest::RunTest(const FString&)
{
	const AGFCharacter* Body = GetDefault<AGFCharacter>();
	if (!TestNotNull(TEXT("Character defaults"), Body) || !TestNotNull(TEXT("Spring arm"), Body->SpringArm.Get()))
	{
		return false;
	}
	TestEqual(TEXT("Arm length 300"), Body->SpringArm->TargetArmLength, GFAim::ArmLength);
	TestTrue(TEXT("Socket offset (0, 60, 20)"), Body->SpringArm->SocketOffset.Equals(GFAim::ShoulderOffset()));
	TestTrue(TEXT("Camera lag on"), Body->SpringArm->bEnableCameraLag);
	TestEqual(TEXT("Lag speed 12"), Body->SpringArm->CameraLagSpeed, GFAim::CameraLagSpeed);
	TestFalse(TEXT("Rotation lag stays off"), Body->SpringArm->bEnableCameraRotationLag);
	TestTrue(TEXT("Arm collision stays on"), Body->SpringArm->bDoCollisionTest);
	TestTrue(TEXT("Pitch stays -20"), Near(Body->SpringArm->GetRelativeRotation().Pitch, -20.f));
	TestTrue(TEXT("Arm height stays 60"), Near(Body->SpringArm->GetRelativeLocation().Z, 60.f));
	TestTrue(TEXT("Third-person FOV stays 90"), Body->ThirdPersonCamera && Near(Body->ThirdPersonCamera->FieldOfView, 90.f));
	TestTrue(TEXT("First-person camera stays at the eye"), Body->FirstPersonCamera && Body->FirstPersonCamera->GetRelativeLocation().Equals(FVector(10.f, 0.f, 70.f)));

	const UCharacterMovementComponent* Move = Body->GetCharacterMovement();
	TestTrue(TEXT("Walk speed stays 500"), Move && Near(Move->MaxWalkSpeed, 500.f));
	TestTrue(TEXT("Jump stays 520"), Move && Near(Move->JumpZVelocity, 520.f));
	TestTrue(TEXT("Orient to movement stays on at rest"), Move && Move->bOrientRotationToMovement);
	TestEqual(TEXT("Build reach stays 800"), Body->BuildReach, 800.f);
	TestTrue(TEXT("Fade is full at 40cm"), Near(GFCameraFadeAlpha(40.f), 1.f));
	TestTrue(TEXT("Fade is gone inside 10cm"), Near(GFCameraFadeAlpha(10.f), 0.f));
	TestTrue(TEXT("Fade is half way"), Near(GFCameraFadeAlpha(25.f), 0.5f));
	return true;
}

#endif
