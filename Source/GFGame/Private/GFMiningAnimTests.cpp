// Mining clip choices (GFMiningAnim): the tap clip, the owner order, and every key the character asks for has a row.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.MiningAnim;Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "GFMiningAnim.h"
#include "GFAnimMap.h"
#include "Aim/GFAimSim.h"
#include "Animation/AnimSequenceBase.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningAnimChoiceTest, "GFGame.MiningAnim.Choice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMiningAnimChoiceTest::RunTest(const FString&)
{
	const float Feet = 100.f;
	TestEqual(TEXT("Block in front at chest height is punched"), GFMiningAnim::TapAnim(Feet + 120.f, 0.f, Feet), GFMiningAnim::Punch());
	TestEqual(TEXT("Side face at the shins is dug"), GFMiningAnim::TapAnim(Feet + 10.f, 0.f, Feet), GFMiningAnim::Dig());
	TestEqual(TEXT("Ground below the feet is dug"), GFMiningAnim::TapAnim(Feet - 50.f, 0.f, Feet), GFMiningAnim::Dig());
	TestEqual(TEXT("Top face of a block is dug"), GFMiningAnim::TapAnim(Feet + 60.f, 1.f, Feet), GFMiningAnim::Dig());
	TestTrue(TEXT("Mining plays with nothing above it"), GFMiningAnim::MayPlay(false, false));
	TestFalse(TEXT("Ferry seat wins"), GFMiningAnim::MayPlay(true, false));
	TestFalse(TEXT("Fist scope wins"), GFMiningAnim::MayPlay(false, true));
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

#endif