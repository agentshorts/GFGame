// Mining clip choices (GFMiningAnim): the tap clip, the field pose rules against the owner stack, and every key the
// character asks for has a row.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.MiningAnim;Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "GFMiningAnim.h"
#include "GFAnimMap.h"
#include "Aim/GFAimSim.h"
#include "Animation/AnimSequenceBase.h"
#include "GFAnimOwnerStack.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningAnimChoiceTest, "GFGame.MiningAnim.Choice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMiningAnimChoiceTest::RunTest(const FString&)
{
	const float Feet = 100.f;
	TestEqual(TEXT("Block in front at chest height is punched"), GFMiningAnim::TapAnim(Feet + 120.f, 0.f, Feet), GFMiningAnim::Punch());
	TestEqual(TEXT("Side face at the shins is dug"), GFMiningAnim::TapAnim(Feet + 10.f, 0.f, Feet), GFMiningAnim::Dig());
	TestEqual(TEXT("Ground below the feet is dug"), GFMiningAnim::TapAnim(Feet - 50.f, 0.f, Feet), GFMiningAnim::Dig());
	TestEqual(TEXT("Top face of a block is dug"), GFMiningAnim::TapAnim(Feet + 60.f, 1.f, Feet), GFMiningAnim::Dig());
	TestTrue(TEXT("Field start is a field key"), GFMiningAnim::IsFieldKey(GFMiningAnim::FieldStart()));
	TestTrue(TEXT("Field loop is a field key"), GFMiningAnim::IsFieldKey(GFMiningAnim::FieldLoop()));
	TestFalse(TEXT("Field end is a one-shot, not held"), GFMiningAnim::IsFieldKey(GFMiningAnim::FieldEnd()));
	TestFalse(TEXT("A claw is not a field key"), GFMiningAnim::IsFieldKey(GFMiningAnim::Claw()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningAnimRowsTest, "GFGame.MiningAnim.Rows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMiningAnimRowsTest::RunTest(const FString&)
{
	struct FKey { FName Key; bool bLoop; };
	const FKey Keys[] = {
		{ GFMiningAnim::Punch(), false }, { GFMiningAnim::Dig(), false }, { GFMiningAnim::Claw(), false },
		{ GFMiningAnim::Overcharge(), false }, { GFMiningAnim::ElementSwap(), false }, { GFMiningAnim::FieldStart(), false },
		{ GFMiningAnim::FieldLoop(), true }, { GFMiningAnim::FieldEnd(), false }, { GFAim::FistScopeAnim(), true },
	};
	for (const FKey& K : Keys)
	{
		const FGFAnimMapRow* Row = FGFAnimMap::Find(K.Key);
		if (!TestNotNull(FString::Printf(TEXT("%s row"), *K.Key.ToString()), Row))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s loop"), *K.Key.ToString()), Row->bLoop, K.bLoop);
		UAnimSequenceBase* Clip = FGFAnimMap::Load(K.Key);
		if (TestNotNull(FString::Printf(TEXT("%s clip loads"), *K.Key.ToString()), Clip))
		{
			TestTrue(FString::Printf(TEXT("%s has length"), *K.Key.ToString()), Clip->GetPlayLength() > 0.f);
			TestFalse(FString::Printf(TEXT("%s has no root motion"), *K.Key.ToString()), Clip->HasRootMotion());
		}
	}
	// One-shots must be short enough to hand the body back: the longest (Overcharge, 24 f at 30 fps) is under a second.
	if (UAnimSequenceBase* Over = FGFAnimMap::Load(GFMiningAnim::Overcharge()))
	{
		TestTrue(TEXT("Overcharge under 1 s"), Over->GetPlayLength() < 1.f);
	}
	return true;
}

namespace GFMiningAnimTestsPrivate
{
	/** What AGFCharacter::ApplyMiningCue does, against a bare owner stack. */
	void Apply(FGFMiningField& Field, FGFAnimOwnerStack& S, EGFMiningFieldCue Cue, float StartSeconds = 0.5f)
	{
		switch (Cue)
		{
		case EGFMiningFieldCue::PlayStart:
			S.Claim(EGFAnimOwner::Mining, GFMiningAnim::FieldStart(), false, StartSeconds);
			Field.Played(Cue, true, StartSeconds);
			break;
		case EGFMiningFieldCue::PlayLoop:
			S.Claim(EGFAnimOwner::Mining, GFMiningAnim::FieldLoop(), true);
			Field.Played(Cue, true, 0.f);
			break;
		case EGFMiningFieldCue::PlayEnd:
			S.Claim(EGFAnimOwner::Mining, GFMiningAnim::FieldEnd(), false, 0.3f);
			break;
		case EGFMiningFieldCue::ReleaseField:
			S.ReleaseKey(EGFAnimOwner::Mining, GFMiningAnim::FieldStart());
			S.ReleaseKey(EGFAnimOwner::Mining, GFMiningAnim::FieldLoop());
			break;
		default:
			break;
		}
	}

	FName MiningKey(const FGFAnimOwnerStack& S)
	{
		const FGFAnimClaim* C = S.Find(EGFAnimOwner::Mining);
		return C ? C->Key : NAME_None;
	}

	/** Holds LMB until the loop is up (tap time 0.2 s, start clip 0.5 s). */
	void HoldToLoop(FGFMiningField& Field, FGFAnimOwnerStack& S, float& Held)
	{
		Field.Press();
		for (int32 i = 0; i < 20; ++i)
		{
			Held += 0.1f;
			S.Tick(0.1f);
			Apply(Field, S, Field.Tick(0.1f, true, Held, 0.2f, false, MiningKey(S)));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFieldScopeReleaseTest, "GFGame.MiningAnim.FieldScopeRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMiningFieldScopeReleaseTest::RunTest(const FString&)
{
	using namespace GFMiningAnimTestsPrivate;
	FGFMiningField Field;
	FGFAnimOwnerStack S;
	float Held = 0.f;
	HoldToLoop(Field, S, Held);
	TestEqual(TEXT("holding reaches the loop"), MiningKey(S), GFMiningAnim::FieldLoop());

	// #304 pass 2: hold ADS with fists while the loop plays. The scope must drop the loop claim, not just the pose.
	S.Claim(EGFAnimOwner::FistScope, TEXT("Player.Fist_Scope"), true);
	Apply(Field, S, Field.Tick(0.1f, true, Held, 0.2f, true, MiningKey(S)));
	TestEqual(TEXT("scope releases the field loop"), MiningKey(S), NAME_None);
	TestEqual(TEXT("pose follows"), int32(Field.Pose), int32(EGFMiningFieldPose::None));

	// Release LMB, then ADS. Nothing is left to play the loop forever.
	Apply(Field, S, Field.End());
	S.Release(EGFAnimOwner::FistScope);
	TestEqual(TEXT("locomotion after both releases"), int32(S.TopOwner()), int32(EGFAnimOwner::Locomotion));

	// End with the pose already down still drops a stray field claim.
	S.Claim(EGFAnimOwner::Mining, GFMiningAnim::FieldLoop(), true);
	Apply(Field, S, Field.End());
	TestEqual(TEXT("End always clears a field claim"), MiningKey(S), NAME_None);

	// ...but never a tap one-shot.
	S.Claim(EGFAnimOwner::Mining, GFMiningAnim::Punch(), false, 0.4f);
	Apply(Field, S, Field.End());
	TestEqual(TEXT("End leaves a punch alone"), MiningKey(S), GFMiningAnim::Punch());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFieldOwnerTest, "GFGame.MiningAnim.FieldOwnOwner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMiningFieldOwnerTest::RunTest(const FString&)
{
	using namespace GFMiningAnimTestsPrivate;
	const FName Wave = TEXT("Player.Emote_Wave");
	FGFMiningField Field;
	FGFAnimOwnerStack S;
	float Held = 0.f;
	HoldToLoop(Field, S, Held);

	// An emote over a held loop: the emote shows, and the loop is back when it ends.
	S.Claim(EGFAnimOwner::Emote, Wave, false, 1.f);
	TestEqual(TEXT("emote on top"), int32(S.TopOwner()), int32(EGFAnimOwner::Emote));
	TestEqual(TEXT("loop kept underneath"), MiningKey(S), GFMiningAnim::FieldLoop());
	S.Tick(1.1f);
	TestEqual(TEXT("loop restored after the emote"), int32(S.TopOwner()), int32(EGFAnimOwner::Mining));

	// A claw one-shot replaces the loop; once it runs out the held field loops again.
	S.Claim(EGFAnimOwner::Mining, GFMiningAnim::Claw(), false, 0.3f);
	S.Tick(0.4f);
	Apply(Field, S, Field.Tick(0.1f, true, Held, 0.2f, false, MiningKey(S)));
	TestEqual(TEXT("loop resumes after a one-shot"), MiningKey(S), GFMiningAnim::FieldLoop());

	// Ending the field while an emote plays does not cut the emote.
	S.Claim(EGFAnimOwner::Emote, Wave, false, 2.f);
	Apply(Field, S, Field.End());
	TestEqual(TEXT("emote survives the field end"), int32(S.TopOwner()), int32(EGFAnimOwner::Emote));
	TestEqual(TEXT("end clip queued underneath"), MiningKey(S), GFMiningAnim::FieldEnd());
	return true;
}

#endif