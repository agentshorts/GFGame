// Mining clip choices (GFMiningAnim): the tap clip, the field pose rules against the owner stack, contact times, and
// every key the character asks for has a row.
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

namespace
{
	FName ContactMiningKey(const FGFAnimOwnerStack& Stack)
	{
		const FGFAnimClaim* Claim = Stack.Find(EGFAnimOwner::Mining);
		return Claim ? Claim->Key : NAME_None;
	}

	/** What BeginMiningContact does after the clip has claimed: note the new claim, then arm when the key has a contact. */
	void ArmContact(FGFAnimOwnerStack& Stack, FGFMiningContact& Contact, FName Key, float ClipSeconds, EGFMiningContactLand Land)
	{
		Stack.Claim(EGFAnimOwner::Mining, Key, false, ClipSeconds);
		Contact.NoteClaim(Key);
		const float Seconds = GFMiningAnim::ContactSeconds(Key);
		if (GFMiningAnim::DelayLand(true, Seconds))
		{
			Contact.Arm(Key, Seconds, Land);
		}
	}

	EGFMiningContactLand StepContact(FGFMiningContact& Contact, FGFAnimOwnerStack& Stack, float DeltaSeconds)
	{
		// The character reads the Mining key, then the anim component expires claims afterwards.
		const EGFMiningContactLand Land = Contact.Tick(DeltaSeconds, ContactMiningKey(Stack));
		Stack.Tick(DeltaSeconds);
		return Land;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningContactSecondsTest, "GFGame.MiningAnim.ContactSeconds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMiningContactSecondsTest::RunTest(const FString&)
{
	struct FContact
	{
		FName Key;
		int32 Frame;
	};
	const FContact Contacts[] = {
		{ GFMiningAnim::Punch(), 6 },
		{ GFMiningAnim::Dig(), 9 },
		{ GFMiningAnim::Claw(), 7 },
		{ GFMiningAnim::Overcharge(), 13 },
		{ GFMiningAnim::ElementSwap(), 6 },
	};
	for (const FContact& Row : Contacts)
	{
		TestEqual(FString::Printf(TEXT("%s contact frame"), *Row.Key.ToString()), GFMiningAnim::ContactFrame(Row.Key), Row.Frame);
		const float Seconds = static_cast<float>(Row.Frame) / GFMiningAnim::ClipFps;
		TestEqual(FString::Printf(TEXT("%s contact seconds"), *Row.Key.ToString()), GFMiningAnim::ContactSeconds(Row.Key), Seconds);
		TestTrue(FString::Printf(TEXT("%s delays when the clip claims"), *Row.Key.ToString()), GFMiningAnim::DelayLand(true, Seconds));
		TestFalse(FString::Printf(TEXT("%s lands now when the clip is missing"), *Row.Key.ToString()), GFMiningAnim::DelayLand(false, Seconds));
	}

	const FName NoContact[] = {
		GFMiningAnim::FieldStart(),
		GFMiningAnim::FieldLoop(),
		GFMiningAnim::FieldEnd(),
		GFAim::FistScopeAnim(),
		FName(TEXT("Player.No_Such_Clip")),
		NAME_None,
	};
	for (const FName Key : NoContact)
	{
		const FString Label = Key.IsNone() ? TEXT("None") : Key.ToString();
		TestEqual(FString::Printf(TEXT("%s has no contact frame"), *Label), GFMiningAnim::ContactFrame(Key), 0);
		TestEqual(FString::Printf(TEXT("%s contact seconds are 0"), *Label), GFMiningAnim::ContactSeconds(Key), 0.f);
		TestFalse(FString::Printf(TEXT("%s does not delay"), *Label), GFMiningAnim::DelayLand(true, GFMiningAnim::ContactSeconds(Key)));
	}
	TestEqual(TEXT("Punch contact is 0.2 s"), GFMiningAnim::ContactSeconds(GFMiningAnim::Punch()), 0.2f);
	TestEqual(TEXT("Dig contact is 0.3 s"), GFMiningAnim::ContactSeconds(GFMiningAnim::Dig()), 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningContactScheduleTest, "GFGame.MiningAnim.ContactSchedule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMiningContactScheduleTest::RunTest(const FString&)
{
	const FName Punch = GFMiningAnim::Punch();
	const FName Claw = GFMiningAnim::Claw();
	const float PunchContact = GFMiningAnim::ContactSeconds(Punch);
	const float ClawContact = GFMiningAnim::ContactSeconds(Claw);

	FGFAnimOwnerStack Stack;
	FGFMiningContact Contact;
	ArmContact(Stack, Contact, Punch, 0.4f, EGFMiningContactLand::RemoveBlock);
	TestTrue(TEXT("punch land is waiting"), Contact.IsPending());
	TestEqual(TEXT("waiting on the punch claim"), Contact.PendingKey(), Punch);
	TestEqual(TEXT("land fires at the contact"), int32(StepContact(Contact, Stack, PunchContact)), int32(EGFMiningContactLand::RemoveBlock));
	TestFalse(TEXT("land fires once"), Contact.IsPending());
	TestEqual(TEXT("a later tick does not land again"), int32(StepContact(Contact, Stack, 1.f)), int32(EGFMiningContactLand::None));

	ArmContact(Stack, Contact, Punch, 0.4f, EGFMiningContactLand::RemoveBlock);
	TestEqual(TEXT("half the contact does not land"), int32(StepContact(Contact, Stack, PunchContact * 0.5f)), int32(EGFMiningContactLand::None));
	TestTrue(TEXT("still waiting at half contact"), Contact.IsPending());
	TestEqual(TEXT("punch claim still held"), ContactMiningKey(Stack), Punch);
	TestEqual(TEXT("the rest of the contact lands"), int32(StepContact(Contact, Stack, PunchContact)), int32(EGFMiningContactLand::RemoveBlock));

	// Releasing the Mining claim before contact skips the land, and putting the key back does not revive it.
	ArmContact(Stack, Contact, Punch, 0.4f, EGFMiningContactLand::RemoveBlock);
	Stack.Release(EGFAnimOwner::Mining);
	TestEqual(TEXT("released claim skips the land"), int32(StepContact(Contact, Stack, PunchContact)), int32(EGFMiningContactLand::None));
	TestFalse(TEXT("release clears the wait"), Contact.IsPending());
	Stack.Claim(EGFAnimOwner::Mining, Punch, false, 0.4f);
	TestEqual(TEXT("the same key without a new arm does not land"), int32(StepContact(Contact, Stack, PunchContact)), int32(EGFMiningContactLand::None));

	// A new Mining claim replaces the punch. The punch land is dropped. The claw lands on its own contact.
	ArmContact(Stack, Contact, Punch, 0.4f, EGFMiningContactLand::RemoveBlock);
	TestEqual(TEXT("early punch frame does not land"), int32(StepContact(Contact, Stack, 0.05f)), int32(EGFMiningContactLand::None));
	ArmContact(Stack, Contact, Claw, 0.4f, EGFMiningContactLand::Claw);
	TestEqual(TEXT("replacement waits on the claw"), Contact.PendingKey(), Claw);
	TestEqual(TEXT("claw contact is not the punch land"), int32(StepContact(Contact, Stack, ClawContact)), int32(EGFMiningContactLand::Claw));
	TestFalse(TEXT("punch land did not survive the replacement"), Contact.IsPending());

	// Replacing the claim and not arming the new one skips the old land.
	ArmContact(Stack, Contact, Punch, 0.4f, EGFMiningContactLand::RemoveBlock);
	Contact.NoteClaim(Claw);
	Stack.Claim(EGFAnimOwner::Mining, Claw, false, 0.4f);
	TestFalse(TEXT("replace without a land clears the wait"), Contact.IsPending());
	TestEqual(TEXT("unarmed replacement does not land"), int32(StepContact(Contact, Stack, 1.f)), int32(EGFMiningContactLand::None));

	// The claim expiring before contact is a release. The land does not fire into an empty Mining slot.
	Stack.Claim(EGFAnimOwner::Mining, Punch, false, 0.1f);
	Contact.NoteClaim(Punch);
	Contact.Arm(Punch, PunchContact, EGFMiningContactLand::RemoveBlock);
	TestEqual(TEXT("short clip has not connected yet"), int32(StepContact(Contact, Stack, 0.1f)), int32(EGFMiningContactLand::None));
	TestEqual(TEXT("short clip expired"), ContactMiningKey(Stack), NAME_None);
	TestEqual(TEXT("expired claim skips the land"), int32(StepContact(Contact, Stack, PunchContact)), int32(EGFMiningContactLand::None));

	// An owner above Mining covers the body. The Mining claim is still the punch, so the land still fires.
	ArmContact(Stack, Contact, Punch, 0.4f, EGFMiningContactLand::RemoveBlock);
	Stack.Claim(EGFAnimOwner::Emote, TEXT("Player.Emote_Wave"), false, 2.f);
	TestEqual(TEXT("emote shows over the punch"), int32(Stack.TopOwner()), int32(EGFAnimOwner::Emote));
	TestEqual(TEXT("covered punch still lands"), int32(StepContact(Contact, Stack, PunchContact)), int32(EGFMiningContactLand::RemoveBlock));
	TestEqual(TEXT("emote stays up after the land"), int32(Stack.TopOwner()), int32(EGFAnimOwner::Emote));

	Contact.Arm(GFMiningAnim::FieldLoop(), 0.f, EGFMiningContactLand::RemoveBlock);
	TestFalse(TEXT("zero contact is not armed"), Contact.IsPending());
	Contact.Arm(Punch, PunchContact, EGFMiningContactLand::None);
	TestFalse(TEXT("an empty land is not armed"), Contact.IsPending());
	return true;
}

#endif