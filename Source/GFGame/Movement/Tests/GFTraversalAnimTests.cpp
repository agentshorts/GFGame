// #356 traversal anim owners: wall run side, mantle Low / High (Decision #370), climb and wall run play rates, and the
// driver that claims Climbing / Mantling / WallRun from the movement state. Pure: no world, mesh or imported clip.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Movement.TraversalAnim;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFAnimMap.h"
#include "GFAnimOwnerStack.h"
#include "GFMovementTuning.h"
#include "GFStaminaSim.h"
#include "GFTraversalAnim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFTraversalAnimTestsPrivate
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** Manifest clip lengths (frames / 30 fps). */
	constexpr float EnterSeconds = 8.f / 30.f;
	constexpr float ExitFallSeconds = 12.f / 30.f;
	constexpr float MantleSeconds = 17.f / 30.f;
	constexpr float LoopSeconds = 10.f / 30.f;
	constexpr float WallRunSeconds = 16.f / 30.f;

	/** Every wallclimb_anims_v1 clip imported (the Low mantle only when bLow). */
	struct FFakeClips
	{
		bool bAll = true;
		bool bLow = false;
		bool bLoop = true;
		bool bEnter = true;

		FGFTraversalClipInfo operator()(FName Key) const
		{
			FGFTraversalClipInfo Info;
			if (!bAll)
			{
				return Info;
			}
			if (Key == GFTraversalAnim::ClimbEnter() && bEnter) { Info.Seconds = EnterSeconds; }
			else if (Key == GFTraversalAnim::ClimbLoop() && bLoop) { Info.Seconds = LoopSeconds; }
			else if (Key == GFTraversalAnim::ClimbExitFall()) { Info.Seconds = ExitFallSeconds; }
			else if (Key == GFTraversalAnim::MantleHigh()) { Info.Seconds = MantleSeconds; }
			else if (Key == GFTraversalAnim::MantleLow() && bLow) { Info.Seconds = MantleSeconds; }
			else if (Key == GFTraversalAnim::WallRunLoopL() || Key == GFTraversalAnim::WallRunLoopR()) { Info.Seconds = WallRunSeconds; }
			Info.NaturalSpeedCms = Info.IsAvailable() ? GFTraversalAnim::DefaultNaturalSpeedCms(Key) : 0.f;
			return Info;
		}
	};

	/** Applies cues to a stack the way AGFCharacter applies them to UGFSimpleAnimComponent. */
	void Apply(FGFAnimOwnerStack& Stack, const FGFTraversalAnimDriver::FCues& Cues)
	{
		for (const FGFTraversalAnimCue& Cue : Cues)
		{
			switch (Cue.Kind)
			{
			case FGFTraversalAnimCue::EKind::Claim: Stack.Claim(Cue.Owner, Cue.Key, Cue.bLoop, Cue.Seconds, Cue.PlayRate); break;
			case FGFTraversalAnimCue::EKind::Release: Stack.Release(Cue.Owner); break;
			case FGFTraversalAnimCue::EKind::SetPlayRate: Stack.SetPlayRate(Cue.Owner, Cue.PlayRate); break;
			default: break;
			}
		}
	}

	/** One frame: the driver, then the stack's own countdown (the anim component ticks after the character). */
	void Frame(FGFTraversalAnimDriver& Driver, FGFAnimOwnerStack& Stack, FGFTraversalAnimInput In, const FFakeClips& Clips, float Dt)
	{
		In.DeltaSeconds = Dt;
		Apply(Stack, Driver.Tick(In, [&Clips](FName Key) { return Clips(Key); }));
		Stack.Tick(Dt);
	}

	FName TopKey(const FGFAnimOwnerStack& S)
	{
		const FGFAnimClaim* T = S.Top();
		return T ? T->Key : NAME_None;
	}

	FGFStaminaState FullState()
	{
		FGFStaminaState State;
		State.ResetFull(*GetDefault<UGFMovementTuning>());
		return State;
	}

	FGFStaminaCommand AirCommand(float Dt)
	{
		FGFStaminaCommand Command;
		Command.DeltaSeconds = Dt;
		Command.bGrounded = false;
		Command.bStaminaEnabled = true;
		Command.CameraForward = FVector::ForwardVector;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimWallRunSideTest, "GFGame.Movement.TraversalAnim.WallRunSide", GFTraversalAnimTestsPrivate::Flags)
bool FGFTraversalAnimWallRunSideTest::RunTest(const FString&)
{
	using namespace GFTraversalAnimTestsPrivate;
	// Running +Y: her right is -X, her left is +X. The normal points off the wall, toward her.
	const FVector Run(0.0, 1.0, 0.0);
	TestTrue(TEXT("wall at +X (normal -X) is on her left"), GFTraversalAnim::WallRunSideOf(Run, FVector(-1.0, 0.0, 0.0)) == EGFWallRunSide::Left);
	TestTrue(TEXT("wall at -X (normal +X) is on her right"), GFTraversalAnim::WallRunSideOf(Run, FVector(1.0, 0.0, 0.0)) == EGFWallRunSide::Right);
	TestTrue(TEXT("running the other way flips the side"), GFTraversalAnim::WallRunSideOf(-Run, FVector(-1.0, 0.0, 0.0)) == EGFWallRunSide::Right);
	TestTrue(TEXT("a tilted normal still reads by its flat part"), GFTraversalAnim::WallRunSideOf(Run, FVector(-0.7, 0.0, 0.7)) == EGFWallRunSide::Left);
	TestTrue(TEXT("no direction is no side"), GFTraversalAnim::WallRunSideOf(FVector::ZeroVector, FVector(1.0, 0.0, 0.0)) == EGFWallRunSide::None);
	TestTrue(TEXT("a normal along the run is no side"), GFTraversalAnim::WallRunSideOf(Run, FVector(0.0, -1.0, 0.0)) == EGFWallRunSide::None);

	TestEqual(TEXT("left plays Loop_L"), GFTraversalAnim::WallRunKey(EGFWallRunSide::Left), GFTraversalAnim::WallRunLoopL());
	TestEqual(TEXT("right plays Loop_R"), GFTraversalAnim::WallRunKey(EGFWallRunSide::Right), GFTraversalAnim::WallRunLoopR());
	TestEqual(TEXT("none plays nothing"), GFTraversalAnim::WallRunKey(EGFWallRunSide::None), FName(NAME_None));

	// From the sim, the same state UGFCharacterMovementComponent::GetWallRunSide reads.
	const UGFMovementTuning& Tuning = *GetDefault<UGFMovementTuning>();
	auto WallRun = [&Tuning](const FVector& Normal, const FVector& Move)
	{
		FGFStaminaCommand Command = AirCommand(0.1f);
		Command.HorizontalSpeed = 500.f;
		Command.MoveDirection = Move;
		Command.Contact.bWallContact = true;
		Command.Contact.WallNormal = Normal;
		return GFStaminaSim::Step(FullState(), Command, Tuning);
	};
	const FGFStaminaState Right = WallRun(FVector(1.0, 0.0, 0.0), Run);
	TestTrue(TEXT("sim wall runs (wall on -X)"), Right.bWallRunning);
	TestTrue(TEXT("sim: wall on -X running +Y is on her right"), GFTraversalAnim::WallRunSideOf(Right.WallRunDir, Right.WallNormal) == EGFWallRunSide::Right);
	const FGFStaminaState Left = WallRun(FVector(-1.0, 0.0, 0.0), Run);
	TestTrue(TEXT("sim wall runs (wall on +X)"), Left.bWallRunning);
	TestTrue(TEXT("sim: wall on +X running +Y is on her left"), GFTraversalAnim::WallRunSideOf(Left.WallRunDir, Left.WallNormal) == EGFWallRunSide::Left);
	const FGFStaminaState Back = WallRun(FVector(-1.0, 0.0, 0.0), -Run);
	TestTrue(TEXT("sim: same wall the other way is on her right"), GFTraversalAnim::WallRunSideOf(Back.WallRunDir, Back.WallNormal) == EGFWallRunSide::Right);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimMantleKeyTest, "GFGame.Movement.TraversalAnim.MantleLowHigh", GFTraversalAnimTestsPrivate::Flags)
bool FGFTraversalAnimMantleKeyTest::RunTest(const FString&)
{
	using namespace GFTraversalAnimTestsPrivate;
	TestEqual(TEXT("Decision #370 Low limit"), GFTraversalAnim::MantleLowMaxLipCm, 120.f);
	TestEqual(TEXT("Decision #370 High lip"), GFTraversalAnim::MantleHighLipCm, 220.f);
	TestEqual(TEXT("High key is the authored Mantle_Up"), GFTraversalAnim::MantleHigh(), FName(TEXT("Player.Mantle_Up")));
	TestEqual(TEXT("Low key name is reserved"), GFTraversalAnim::MantleLow(), FName(TEXT("Player.Mantle_Up_Low")));

	// Both clips in.
	TestEqual(TEXT("30 cm lip is Low"), GFTraversalAnim::MantleKey(30.f, true), GFTraversalAnim::MantleLow());
	TestEqual(TEXT("120 cm lip is Low"), GFTraversalAnim::MantleKey(120.f, true), GFTraversalAnim::MantleLow());
	TestEqual(TEXT("121 cm lip is High"), GFTraversalAnim::MantleKey(121.f, true), GFTraversalAnim::MantleHigh());
	TestEqual(TEXT("220 cm lip is High"), GFTraversalAnim::MantleKey(220.f, true), GFTraversalAnim::MantleHigh());
	TestEqual(TEXT("unknown lip (remote copy) is High"), GFTraversalAnim::MantleKey(-1.f, true), GFTraversalAnim::MantleHigh());
	// Only Mantle_Up in the map today: every lip plays High.
	TestEqual(TEXT("no Low clip: 60 cm lip plays High"), GFTraversalAnim::MantleKey(60.f, false), GFTraversalAnim::MantleHigh());

	// The sim keeps the lip the mantle started from.
	const UGFMovementTuning& Tuning = *GetDefault<UGFMovementTuning>();
	auto Mantle = [&Tuning](float Lip)
	{
		FGFStaminaCommand Command = AirCommand(0.05f);
		Command.bGrounded = true;
		Command.MoveDirection = FVector(1.0, 0.0, 0.0);
		Command.PlayerLocation = FVector(0.0, 0.0, 88.0);
		Command.Contact.bWallAhead = true;
		Command.Contact.WallAheadNormal = FVector(-1.0, 0.0, 0.0);
		Command.Contact.bLedge = true;
		Command.Contact.LedgeTarget = FVector(100.0, 0.0, Lip + 90.0);
		Command.Contact.LedgeHeight = Lip;
		return GFStaminaSim::Step(FullState(), Command, Tuning);
	};
	const FGFStaminaState Low = Mantle(100.f);
	TestTrue(TEXT("100 cm lip mantles"), Low.bMantling);
	TestEqual(TEXT("sim keeps the 100 cm lip"), Low.MantleLipHeight, 100.f);
	TestEqual(TEXT("100 cm lip picks Low"), GFTraversalAnim::MantleKey(Low.MantleLipHeight, true), GFTraversalAnim::MantleLow());
	const FGFStaminaState High = Mantle(200.f);
	TestTrue(TEXT("200 cm lip mantles"), High.bMantling);
	TestEqual(TEXT("200 cm lip picks High"), GFTraversalAnim::MantleKey(High.MantleLipHeight, true), GFTraversalAnim::MantleHigh());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimPlayRateTest, "GFGame.Movement.TraversalAnim.PlayRate", GFTraversalAnimTestsPrivate::Flags)
bool FGFTraversalAnimPlayRateTest::RunTest(const FString&)
{
	const UGFMovementTuning& Tuning = *GetDefault<UGFMovementTuning>();
	TestEqual(TEXT("climb loop natural speed is the climb speed"), GFTraversalAnim::DefaultNaturalSpeedCms(GFTraversalAnim::ClimbLoop()), Tuning.WallClimbSpeed);
	TestEqual(TEXT("wall run L natural speed"), GFTraversalAnim::DefaultNaturalSpeedCms(GFTraversalAnim::WallRunLoopL()), 450.f);
	TestEqual(TEXT("wall run R natural speed"), GFTraversalAnim::DefaultNaturalSpeedCms(GFTraversalAnim::WallRunLoopR()), 450.f);
	TestEqual(TEXT("one-shots have no natural speed"), GFTraversalAnim::DefaultNaturalSpeedCms(GFTraversalAnim::MantleHigh()), 0.f);

	TestEqual(TEXT("climb at 350 plays at 1.0"), FGFAnimMap::RateFromNaturalSpeed(350.f, 350.f), 1.f);
	TestTrue(TEXT("climb at 175 plays at 0.5"), FMath::IsNearlyEqual(FGFAnimMap::RateFromNaturalSpeed(175.f, 350.f), 0.5f));
	TestTrue(TEXT("wall run at 600 plays at 1.33"), FMath::IsNearlyEqual(FGFAnimMap::RateFromNaturalSpeed(600.f, 450.f), 600.f / 450.f));
	TestEqual(TEXT("clamped low"), FGFAnimMap::RateFromNaturalSpeed(10.f, 450.f), FGFAnimMap::MinPlayRate);
	TestEqual(TEXT("clamped high"), FGFAnimMap::RateFromNaturalSpeed(5000.f, 450.f), FGFAnimMap::MaxPlayRate);
	TestEqual(TEXT("no natural speed plays at 1.0"), FGFAnimMap::RateFromNaturalSpeed(500.f, 0.f), 1.f);
	TestEqual(TEXT("clamp matches PlayRateFor"), FGFAnimMap::MinPlayRate, 0.25f);
	TestEqual(TEXT("clamp matches PlayRateFor (high)"), FGFAnimMap::MaxPlayRate, 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimDriverClimbTest, "GFGame.Movement.TraversalAnim.DriverClimb", GFTraversalAnimTestsPrivate::Flags)
bool FGFTraversalAnimDriverClimbTest::RunTest(const FString&)
{
	using namespace GFTraversalAnimTestsPrivate;
	const FFakeClips Clips;
	FGFTraversalAnimDriver Driver;
	FGFAnimOwnerStack S;
	FGFTraversalAnimInput Climb;
	Climb.bClimbing = true;
	Climb.ClimbSpeedCms = 350.f;

	Frame(Driver, S, Climb, Clips, 0.05f);
	TestEqual(TEXT("climb start plays Enter"), TopKey(S), GFTraversalAnim::ClimbEnter());
	TestEqual(TEXT("as the Climbing owner"), int32(S.TopOwner()), int32(EGFAnimOwner::Climbing));
	bool bGap = false;
	for (int32 i = 0; i < 6; ++i)
	{
		Frame(Driver, S, Climb, Clips, 0.05f);
		bGap = bGap || S.Top() == nullptr;
	}
	TestFalse(TEXT("no locomotion frame between Enter and Loop"), bGap);
	TestEqual(TEXT("then the loop"), TopKey(S), GFTraversalAnim::ClimbLoop());
	const FGFAnimClaim* Loop = S.Top();
	if (TestNotNull(TEXT("loop claim"), Loop))
	{
		TestTrue(TEXT("loop is held and looping"), Loop->bLoop && !Loop->bTimed);
		TestEqual(TEXT("loop at 350 plays at 1.0"), Loop->PlayRate, 1.f);
	}
	FGFTraversalAnimInput Slow = Climb;
	Slow.ClimbSpeedCms = 175.f;
	Frame(Driver, S, Slow, Clips, 0.05f);
	TestTrue(TEXT("slower climb slows the loop"), S.Top() && FMath::IsNearlyEqual(S.Top()->PlayRate, 0.5f));

	// Let go: Exit_Fall, then it runs out and locomotion is back.
	FGFTraversalAnimInput Off;
	Frame(Driver, S, Off, Clips, 0.05f);
	TestEqual(TEXT("dropping off plays Exit_Fall"), TopKey(S), GFTraversalAnim::ClimbExitFall());
	for (int32 i = 0; i < 10; ++i)
	{
		Frame(Driver, S, Off, Clips, 0.05f);
	}
	TestNull(TEXT("Exit_Fall runs out to locomotion"), S.Top());

	// A climb over a fist scope and an emote: traversal shows, the scope comes back after.
	S.Claim(EGFAnimOwner::FistScope, TEXT("Player.FistScope"), true);
	S.Claim(EGFAnimOwner::Emote, TEXT("Player.Emote_Wave"), false, 5.f);
	Frame(Driver, S, Climb, Clips, 0.05f);
	TestEqual(TEXT("climb shows over the fist scope"), TopKey(S), GFTraversalAnim::ClimbEnter());
	FGFTraversalAnimInput Run;
	Run.WallRunSide = EGFWallRunSide::Left;
	Run.WallRunSpeedCms = 450.f;
	Frame(Driver, S, Run, Clips, 0.05f);
	TestEqual(TEXT("climb hands off to the wall run (no Exit_Fall)"), TopKey(S), GFTraversalAnim::WallRunLoopL());
	TestFalse(TEXT("climb claim dropped on the hand-off"), S.Has(EGFAnimOwner::Climbing));
	Frame(Driver, S, Off, Clips, 0.05f);
	TestEqual(TEXT("fist scope back after the wall run"), TopKey(S), FName(TEXT("Player.FistScope")));

	// No clips imported (today): nothing is ever claimed, locomotion keeps the body.
	FFakeClips None;
	None.bAll = false;
	FGFTraversalAnimDriver Bare;
	FGFAnimOwnerStack Empty;
	Frame(Bare, Empty, Climb, None, 0.05f);
	Frame(Bare, Empty, Climb, None, 0.5f);
	Frame(Bare, Empty, Off, None, 0.05f);
	Frame(Bare, Empty, Run, None, 0.05f);
	FGFTraversalAnimInput MantleIn;
	MantleIn.bMantling = true;
	Frame(Bare, Empty, MantleIn, None, 0.05f);
	TestEqual(TEXT("missing clips claim nothing"), Empty.Num(), 0);

	// Enter in, loop missing: Enter plays out and hands back to locomotion.
	FFakeClips NoLoop;
	NoLoop.bLoop = false;
	FGFTraversalAnimDriver Half;
	FGFAnimOwnerStack HalfStack;
	for (int32 i = 0; i < 12; ++i)
	{
		Frame(Half, HalfStack, Climb, NoLoop, 0.05f);
	}
	TestNull(TEXT("no loop clip: Enter ends to locomotion"), HalfStack.Top());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimDriverMantleRunTest, "GFGame.Movement.TraversalAnim.DriverMantleAndWallRun", GFTraversalAnimTestsPrivate::Flags)
bool FGFTraversalAnimDriverMantleRunTest::RunTest(const FString&)
{
	using namespace GFTraversalAnimTestsPrivate;
	FFakeClips Clips;
	FGFTraversalAnimDriver Driver;
	FGFAnimOwnerStack S;
	FGFTraversalAnimInput Climb;
	Climb.bClimbing = true;
	Climb.ClimbSpeedCms = 350.f;
	for (int32 i = 0; i < 8; ++i)
	{
		Frame(Driver, S, Climb, Clips, 0.05f);
	}
	TestEqual(TEXT("setup: climbing loop"), TopKey(S), GFTraversalAnim::ClimbLoop());

	// Low lip with only Mantle_Up in the map: High plays.
	FGFTraversalAnimInput Mantle;
	Mantle.bMantling = true;
	Mantle.MantleLipCm = 90.f;
	Frame(Driver, S, Mantle, Clips, 0.05f);
	TestEqual(TEXT("no Low clip: a 90 cm lip plays Mantle_Up"), TopKey(S), GFTraversalAnim::MantleHigh());
	TestEqual(TEXT("as the Mantling owner"), int32(S.TopOwner()), int32(EGFAnimOwner::Mantling));
	TestFalse(TEXT("the climb loop is let go"), S.Has(EGFAnimOwner::Climbing));
	for (int32 i = 0; i < 20; ++i)
	{
		Frame(Driver, S, Mantle, Clips, 0.05f);
	}
	TestEqual(TEXT("the mantle is held for the whole mantle"), TopKey(S), GFTraversalAnim::MantleHigh());
	FGFTraversalAnimInput Off;
	Frame(Driver, S, Off, Clips, 0.05f);
	TestNull(TEXT("mantle end: no Exit_Fall, straight to locomotion"), S.Top());

	// With the Low clip in, a 90 cm lip plays Low and a 220 cm lip plays High.
	Clips.bLow = true;
	Frame(Driver, S, Mantle, Clips, 0.05f);
	TestEqual(TEXT("Low clip in: 90 cm lip plays Mantle_Up_Low"), TopKey(S), GFTraversalAnim::MantleLow());
	Frame(Driver, S, Off, Clips, 0.05f);
	Mantle.MantleLipCm = 220.f;
	Frame(Driver, S, Mantle, Clips, 0.05f);
	TestEqual(TEXT("220 cm lip plays Mantle_Up"), TopKey(S), GFTraversalAnim::MantleHigh());
	Frame(Driver, S, Off, Clips, 0.05f);

	// Wall run: side picks the loop, a side change swaps it, the rate follows the run speed.
	FGFTraversalAnimInput Run;
	Run.WallRunSide = EGFWallRunSide::Right;
	Run.WallRunSpeedCms = 450.f;
	Frame(Driver, S, Run, Clips, 0.05f);
	TestEqual(TEXT("wall on her right plays Loop_R"), TopKey(S), GFTraversalAnim::WallRunLoopR());
	TestEqual(TEXT("as the WallRun owner"), int32(S.TopOwner()), int32(EGFAnimOwner::WallRun));
	TestTrue(TEXT("450 plays at 1.0"), S.Top() && FMath::IsNearlyEqual(S.Top()->PlayRate, 1.f));
	Run.WallRunSpeedCms = 600.f;
	Frame(Driver, S, Run, Clips, 0.05f);
	TestTrue(TEXT("600 plays at 1.33"), S.Top() && FMath::IsNearlyEqual(S.Top()->PlayRate, 600.f / 450.f));
	Run.WallRunSide = EGFWallRunSide::Left;
	Frame(Driver, S, Run, Clips, 0.05f);
	TestEqual(TEXT("wall on her left swaps to Loop_L"), TopKey(S), GFTraversalAnim::WallRunLoopL());
	TestEqual(TEXT("one WallRun claim"), S.Num(), 1);
	Frame(Driver, S, Off, Clips, 0.05f);
	TestNull(TEXT("wall run end releases"), S.Top());

	// Reset drops anything held.
	Frame(Driver, S, Run, Clips, 0.05f);
	Apply(S, Driver.Reset());
	TestNull(TEXT("reset releases the wall run"), S.Top());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimNotifyNamesTest, "GFGame.Movement.TraversalAnim.NotifyNames", GFTraversalAnimTestsPrivate::Flags)
bool FGFTraversalAnimNotifyNamesTest::RunTest(const FString&)
{
	const TArray<GFTraversalAnim::FProposedNotify> Notifies = GFTraversalAnim::ProposedNotifies();
	TestEqual(TEXT("ten proposed notifies (manifest)"), Notifies.Num(), 10);
	const TSet<FName> Clips = {
		GFTraversalAnim::ClimbEnter(), GFTraversalAnim::ClimbLoop(), GFTraversalAnim::ClimbExitFall(),
		GFTraversalAnim::MantleHigh(), GFTraversalAnim::WallRunLoopL(), GFTraversalAnim::WallRunLoopR(),
	};
	TSet<FString> Seen;
	for (const GFTraversalAnim::FProposedNotify& N : Notifies)
	{
		TestTrue(FString::Printf(TEXT("%s is on a traversal clip"), *N.Notify.ToString()), Clips.Contains(N.Key));
		TestTrue(FString::Printf(TEXT("%s frame is 1-based"), *N.Notify.ToString()), N.Frame >= 1);
		const FString Pair = N.Key.ToString() + TEXT("/") + N.Notify.ToString();
		TestFalse(FString::Printf(TEXT("%s listed once"), *Pair), Seen.Contains(Pair));
		Seen.Add(Pair);
	}
	TestEqual(TEXT("loop plants the right hand on f1"), Notifies[1].Notify, GFTraversalAnim::Notify::ClimbHandPlantR());
	TestEqual(TEXT("mantle lets go on f10"), Notifies[9].Frame, 10);
	return true;
}

#endif
