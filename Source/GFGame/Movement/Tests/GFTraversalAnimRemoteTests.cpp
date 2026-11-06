// #387: a simulated proxy only receives the climb phase. Two updates can both say "climbing" or both say
// "mantling" when the end and the next start happened in between, and a re-grab can land while Exit_Fall
// is still the timed climb claim. The driver has to play the new enter or mantle anyway.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Movement.TraversalAnim;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFAnimOwnerStack.h"
#include "GFCharacterMovementComponent.h"
#include "GFTraversalAnim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFTraversalAnimRemoteTestsPrivate
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	constexpr float EnterSeconds = 8.f / 30.f;
	constexpr float ExitFallSeconds = 12.f / 30.f;
	constexpr float MantleSeconds = 17.f / 30.f;
	constexpr float LoopSeconds = 10.f / 30.f;
	constexpr float WallRunSeconds = 16.f / 30.f;
	constexpr float Dt = 0.05f;

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

	FGFTraversalAnimDriver::FCues Frame(FGFTraversalAnimDriver& Driver, FGFAnimOwnerStack& Stack, FGFTraversalAnimInput In, const FFakeClips& Clips)
	{
		In.DeltaSeconds = Dt;
		FGFTraversalAnimDriver::FCues Cues = Driver.Tick(In, [&Clips](FName Key) { return Clips(Key); });
		Apply(Stack, Cues);
		Stack.Tick(Dt);
		return Cues;
	}

	FName TopKey(const FGFAnimOwnerStack& S)
	{
		const FGFAnimClaim* T = S.Top();
		return T ? T->Key : NAME_None;
	}

	uint32 TopSerial(const FGFAnimOwnerStack& S)
	{
		const FGFAnimClaim* T = S.Top();
		return T ? T->Serial : 0;
	}

	/** True when a Climbing or Mantling release sits ahead of a claim for Key. */
	bool ReleasesBeforeClaim(const FGFTraversalAnimDriver::FCues& Cues, EGFAnimOwner Owner, FName Key)
	{
		int32 ReleaseAt = INDEX_NONE;
		int32 ClaimAt = INDEX_NONE;
		for (int32 i = 0; i < Cues.Num(); ++i)
		{
			const FGFTraversalAnimCue& Cue = Cues[i];
			if (Cue.Kind == FGFTraversalAnimCue::EKind::Release && Cue.Owner == Owner)
			{
				ReleaseAt = i;
			}
			if (Cue.Kind == FGFTraversalAnimCue::EKind::Claim && Cue.Key == Key)
			{
				ClaimAt = i;
			}
		}
		return ReleaseAt != INDEX_NONE && ClaimAt != INDEX_NONE && ReleaseAt < ClaimAt;
	}

	bool HasRelease(const FGFTraversalAnimDriver::FCues& Cues)
	{
		for (const FGFTraversalAnimCue& Cue : Cues)
		{
			if (Cue.Kind == FGFTraversalAnimCue::EKind::Release)
			{
				return true;
			}
		}
		return false;
	}

	/** Seven frames at Dt: the first claims Enter, the last swaps to Loop. */
	void ClimbToLoop(FGFTraversalAnimDriver& Driver, FGFAnimOwnerStack& Stack, FGFTraversalAnimInput In, const FFakeClips& Clips)
	{
		Frame(Driver, Stack, In, Clips);
		for (int32 i = 0; i < 6; ++i)
		{
			Frame(Driver, Stack, In, Clips);
		}
	}

	FGFTraversalAnimInput ClimbInput(uint8 Seq)
	{
		FGFTraversalAnimInput In;
		In.bClimbing = true;
		In.ClimbSpeedCms = 350.f;
		In.EdgeSeq = Seq;
		return In;
	}

	FGFTraversalAnimInput MantleInput(uint8 Seq, float Lip)
	{
		FGFTraversalAnimInput In;
		In.bMantling = true;
		In.MantleLipCm = Lip;
		In.EdgeSeq = Seq;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimRemoteCoalescedTest, "GFGame.Movement.TraversalAnim.RemoteCoalesced", GFTraversalAnimRemoteTestsPrivate::Flags)
bool FGFTraversalAnimRemoteCoalescedTest::RunTest(const FString&)
{
	using namespace GFTraversalAnimRemoteTestsPrivate;
	const FFakeClips Clips;

	// Owner path: EdgeSeq stays 0. A fresh climb does not release, and staying on the climb does not restart it.
	{
		FGFTraversalAnimDriver Driver;
		FGFAnimOwnerStack S;
		const FGFTraversalAnimDriver::FCues First = Frame(Driver, S, ClimbInput(0), Clips);
		TestEqual(TEXT("owner climb start plays Enter"), TopKey(S), GFTraversalAnim::ClimbEnter());
		TestFalse(TEXT("owner climb start does not release first"), HasRelease(First));
		ClimbToLoop(Driver, S, ClimbInput(0), Clips);
		TestEqual(TEXT("owner climb reaches the loop"), TopKey(S), GFTraversalAnim::ClimbLoop());
		const uint32 LoopSerial = TopSerial(S);
		const FGFTraversalAnimDriver::FCues Held = Frame(Driver, S, ClimbInput(0), Clips);
		TestEqual(TEXT("same phase and count stays on the loop"), TopKey(S), GFTraversalAnim::ClimbLoop());
		TestEqual(TEXT("same count does not re-claim the climb"), TopSerial(S), LoopSerial);
		TestFalse(TEXT("same count does not release the climb"), HasRelease(Held));
	}

	// Remote: climb ends and a new climb starts between two updates. Both samples are Climbing. The count moved.
	{
		FGFTraversalAnimDriver Driver;
		FGFAnimOwnerStack S;
		ClimbToLoop(Driver, S, ClimbInput(1), Clips);
		TestEqual(TEXT("remote setup is the climb loop"), TopKey(S), GFTraversalAnim::ClimbLoop());
		Frame(Driver, S, ClimbInput(1), Clips);
		TestEqual(TEXT("unchanged count between updates stays on the loop"), TopKey(S), GFTraversalAnim::ClimbLoop());

		const FGFTraversalAnimDriver::FCues Again = Frame(Driver, S, ClimbInput(2), Clips);
		TestEqual(TEXT("new climb between updates plays Enter"), TopKey(S), GFTraversalAnim::ClimbEnter());
		TestTrue(TEXT("new climb releases the loop before Enter"), ReleasesBeforeClaim(Again, EGFAnimOwner::Climbing, GFTraversalAnim::ClimbEnter()));
		for (int32 i = 0; i < 6; ++i)
		{
			Frame(Driver, S, ClimbInput(2), Clips);
		}
		TestEqual(TEXT("the new climb reaches its own loop"), TopKey(S), GFTraversalAnim::ClimbLoop());

		// The count wraps. 0 is a real value for a proxy, and it is still a new start.
		const FGFTraversalAnimDriver::FCues Wrapped = Frame(Driver, S, ClimbInput(255), Clips);
		TestEqual(TEXT("count 255 plays Enter again"), TopKey(S), GFTraversalAnim::ClimbEnter());
		TestTrue(TEXT("wrapped count releases before Enter"), ReleasesBeforeClaim(Wrapped, EGFAnimOwner::Climbing, GFTraversalAnim::ClimbEnter()));
		for (int32 i = 0; i < 6; ++i)
		{
			Frame(Driver, S, ClimbInput(255), Clips);
		}
		TestEqual(TEXT("count 255 settles on the loop"), TopKey(S), GFTraversalAnim::ClimbLoop());
		Frame(Driver, S, ClimbInput(0), Clips);
		TestEqual(TEXT("count wrap to 0 plays Enter"), TopKey(S), GFTraversalAnim::ClimbEnter());
	}

	// Remote: two mantles back to back. Both samples are Mantling. Same clip must restart, and a new lip must be read.
	{
		FFakeClips LowClips;
		LowClips.bLow = true;
		FGFTraversalAnimDriver Driver;
		FGFAnimOwnerStack S;
		Frame(Driver, S, MantleInput(1, 220.f), LowClips);
		TestEqual(TEXT("first mantle plays High"), TopKey(S), GFTraversalAnim::MantleHigh());
		const uint32 FirstSerial = TopSerial(S);
		Frame(Driver, S, MantleInput(1, 220.f), LowClips);
		TestEqual(TEXT("same mantle count keeps the claim"), TopSerial(S), FirstSerial);
		TestEqual(TEXT("same mantle count keeps High"), TopKey(S), GFTraversalAnim::MantleHigh());

		const FGFTraversalAnimDriver::FCues Next = Frame(Driver, S, MantleInput(2, 220.f), LowClips);
		TestEqual(TEXT("back to back mantles play High again"), TopKey(S), GFTraversalAnim::MantleHigh());
		TestNotEqual(TEXT("back to back mantles are a new claim"), TopSerial(S), FirstSerial);
		TestTrue(TEXT("back to back mantles release the previous one first"), ReleasesBeforeClaim(Next, EGFAnimOwner::Mantling, GFTraversalAnim::MantleHigh()));

		FGFTraversalAnimInput Off;
		Off.EdgeSeq = 2;
		Frame(Driver, S, Off, LowClips);
		TestNull(TEXT("mantle end releases"), S.Top());

		Frame(Driver, S, MantleInput(3, 90.f), LowClips);
		TestEqual(TEXT("a 90 cm lip plays Low"), TopKey(S), GFTraversalAnim::MantleLow());
		const FGFTraversalAnimDriver::FCues High = Frame(Driver, S, MantleInput(4, 220.f), LowClips);
		TestEqual(TEXT("the next mantle reads its own lip"), TopKey(S), GFTraversalAnim::MantleHigh());
		TestTrue(TEXT("lip change releases the Low mantle first"), ReleasesBeforeClaim(High, EGFAnimOwner::Mantling, GFTraversalAnim::MantleHigh()));
	}

	// A count change while she is wall running is not a climb. The final phase is the wall run.
	{
		FGFTraversalAnimDriver Driver;
		FGFAnimOwnerStack S;
		FGFTraversalAnimInput Run;
		Run.WallRunSide = EGFWallRunSide::Left;
		Run.WallRunSpeedCms = 450.f;
		Run.EdgeSeq = 4;
		Frame(Driver, S, Run, Clips);
		TestEqual(TEXT("wall run setup"), TopKey(S), GFTraversalAnim::WallRunLoopL());
		Run.EdgeSeq = 5;
		Frame(Driver, S, Run, Clips);
		TestEqual(TEXT("count change during a wall run stays on the wall run"), TopKey(S), GFTraversalAnim::WallRunLoopL());
		TestFalse(TEXT("count change during a wall run does not claim a climb"), S.Has(EGFAnimOwner::Climbing));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimRegrabFallOffTest, "GFGame.Movement.TraversalAnim.RegrabDuringFallOff", GFTraversalAnimRemoteTestsPrivate::Flags)
bool FGFTraversalAnimRegrabFallOffTest::RunTest(const FString&)
{
	using namespace GFTraversalAnimRemoteTestsPrivate;
	const FFakeClips Clips;

	// Owner (count stays 0) and a proxy that saw the fall (count moves on the re-grab) both drop Exit_Fall
	// before Enter. The fall clip still has most of its time left, so the enter is not the timer running out.
	auto Regrab = [this, &Clips](uint8 ClimbSeq, uint8 RegrabSeq, const TCHAR* Tag)
	{
		FGFTraversalAnimDriver Driver;
		FGFAnimOwnerStack S;
		ClimbToLoop(Driver, S, ClimbInput(ClimbSeq), Clips);
		TestEqual(FString::Printf(TEXT("%s setup is the loop"), Tag), TopKey(S), GFTraversalAnim::ClimbLoop());

		FGFTraversalAnimInput Off;
		Off.EdgeSeq = ClimbSeq;
		Frame(Driver, S, Off, Clips);
		TestEqual(FString::Printf(TEXT("%s letting go plays Exit_Fall"), Tag), TopKey(S), GFTraversalAnim::ClimbExitFall());
		Frame(Driver, S, Off, Clips);
		Frame(Driver, S, Off, Clips);
		const FGFAnimClaim* Fall = S.Top();
		TestEqual(FString::Printf(TEXT("%s fall clip is still playing"), Tag), TopKey(S), GFTraversalAnim::ClimbExitFall());
		TestTrue(FString::Printf(TEXT("%s fall clip still has time left"), Tag), Fall && Fall->bTimed && Fall->Remaining > 0.f);

		const FGFTraversalAnimDriver::FCues Again = Frame(Driver, S, ClimbInput(RegrabSeq), Clips);
		TestEqual(FString::Printf(TEXT("%s re-grab plays Enter"), Tag), TopKey(S), GFTraversalAnim::ClimbEnter());
		TestTrue(FString::Printf(TEXT("%s re-grab releases Exit_Fall before Enter"), Tag),
			ReleasesBeforeClaim(Again, EGFAnimOwner::Climbing, GFTraversalAnim::ClimbEnter()));
		for (int32 i = 0; i < 6; ++i)
		{
			Frame(Driver, S, ClimbInput(RegrabSeq), Clips);
		}
		TestEqual(FString::Printf(TEXT("%s re-grab reaches the loop"), Tag), TopKey(S), GFTraversalAnim::ClimbLoop());
	};
	Regrab(0, 0, TEXT("owner"));
	Regrab(1, 2, TEXT("proxy"));

	// No enter and no loop to replace the fall claim. The re-grab still has to drop Exit_Fall.
	{
		FGFTraversalAnimDriver Driver;
		FGFAnimOwnerStack S;
		ClimbToLoop(Driver, S, ClimbInput(1), Clips);
		FGFTraversalAnimInput Off;
		Off.EdgeSeq = 1;
		Frame(Driver, S, Off, Clips);
		TestEqual(TEXT("fall is up before the clipless re-grab"), TopKey(S), GFTraversalAnim::ClimbExitFall());

		FFakeClips NoClimbClips = Clips;
		NoClimbClips.bEnter = false;
		NoClimbClips.bLoop = false;
		FGFTraversalAnimInput Again = ClimbInput(2);
		Again.DeltaSeconds = Dt;
		const FGFTraversalAnimDriver::FCues Cues = Driver.Tick(Again, [&NoClimbClips](FName Key) { return NoClimbClips(Key); });
		Apply(S, Cues);
		S.Tick(Dt);
		TestNull(TEXT("re-grab with no climb clips drops Exit_Fall"), S.Top());
		TestTrue(TEXT("re-grab with no climb clips still releases the fall"), HasRelease(Cues));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTraversalAnimEdgeSeqTest, "GFGame.Movement.TraversalAnim.WallClimbEdgeSeq", GFTraversalAnimRemoteTestsPrivate::Flags)
bool FGFTraversalAnimEdgeSeqTest::RunTest(const FString&)
{
	using namespace GFTraversalAnimRemoteTestsPrivate;
	const auto Phase = [](EGFWallClimbPhase Value) { return static_cast<uint8>(Value); };

	FGFWallClimbNet Net;
	Net = GFAdvanceWallClimbNet(Net, false, false);
	TestEqual(TEXT("idle publishes None"), int32(Net.Phase), int32(Phase(EGFWallClimbPhase::None)));
	TestEqual(TEXT("idle does not count a start"), int32(Net.EdgeSeq), 0);

	Net = GFAdvanceWallClimbNet(Net, true, false);
	TestEqual(TEXT("climb start publishes Climbing"), int32(Net.Phase), int32(Phase(EGFWallClimbPhase::Climbing)));
	TestEqual(TEXT("climb start counts"), int32(Net.EdgeSeq), 1);
	Net = GFAdvanceWallClimbNet(Net, true, false);
	TestEqual(TEXT("holding the climb does not count again"), int32(Net.EdgeSeq), 1);

	Net = GFAdvanceWallClimbNet(Net, false, false);
	TestEqual(TEXT("climb end publishes None"), int32(Net.Phase), int32(Phase(EGFWallClimbPhase::None)));
	TestEqual(TEXT("climb end does not count"), int32(Net.EdgeSeq), 1);

	Net = GFAdvanceWallClimbNet(Net, true, false);
	TestEqual(TEXT("a new climb counts"), int32(Net.EdgeSeq), 2);
	Net = GFAdvanceWallClimbNet(Net, false, true);
	TestEqual(TEXT("mantle start publishes Mantling"), int32(Net.Phase), int32(Phase(EGFWallClimbPhase::Mantling)));
	TestEqual(TEXT("mantle start counts"), int32(Net.EdgeSeq), 3);
	Net = GFAdvanceWallClimbNet(Net, true, true);
	TestEqual(TEXT("mantle wins when both flags are set"), int32(Net.Phase), int32(Phase(EGFWallClimbPhase::Mantling)));
	TestEqual(TEXT("staying in the mantle does not count"), int32(Net.EdgeSeq), 3);

	Net = GFAdvanceWallClimbNet(Net, false, false);
	TestEqual(TEXT("mantle end keeps the count"), int32(Net.EdgeSeq), 3);
	Net = GFAdvanceWallClimbNet(Net, false, true);
	TestEqual(TEXT("the next mantle counts"), int32(Net.EdgeSeq), 4);

	Net.Phase = Phase(EGFWallClimbPhase::Climbing);
	Net.EdgeSeq = 255;
	Net = GFAdvanceWallClimbNet(Net, false, false);
	TestEqual(TEXT("ending on 255 keeps 255"), int32(Net.EdgeSeq), 255);
	Net = GFAdvanceWallClimbNet(Net, true, false);
	TestEqual(TEXT("the next start wraps to 0"), int32(Net.EdgeSeq), 0);
	TestEqual(TEXT("wrap still publishes Climbing"), int32(Net.Phase), int32(Phase(EGFWallClimbPhase::Climbing)));
	return true;
}

#endif
