// Animation ownership: the priority stack in UGFSimpleAnimComponent (ferry seat > fist scope > emote > Beat Hop >
// locomotion). Pure data plus the component's claim API, no world or mesh needed.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Visuals.AnimOwner;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFAnimOwnerStack.h"
#include "GFSimpleAnimComponent.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFAnimOwnerTestsPrivate
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static const FName Sit = TEXT("Player.Emote_Sit");
	static const FName Fist = TEXT("Player.Fist_Scope");
	static const FName Wave = TEXT("Player.Emote_Wave");
	static const FName Hop = TEXT("Player.Bhop_Air");

	FName TopKey(const FGFAnimOwnerStack& S)
	{
		const FGFAnimClaim* T = S.Top();
		return T ? T->Key : NAME_None;
	}

	int64 TopSerial(const FGFAnimOwnerStack& S)
	{
		const FGFAnimClaim* T = S.Top();
		return T ? int64(T->Serial) : 0;
	}

	int32 Own(EGFAnimOwner O)
	{
		return int32(O);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAnimOwnerPriorityOrderTest, "GFGame.Visuals.AnimOwner.PriorityOrder", GFAnimOwnerTestsPrivate::Flags)
bool FGFAnimOwnerPriorityOrderTest::RunTest(const FString&)
{
	TestTrue(TEXT("ferry seat above fist scope"), EGFAnimOwner::FerrySeat > EGFAnimOwner::FistScope);
	TestTrue(TEXT("fist scope above emote"), EGFAnimOwner::FistScope > EGFAnimOwner::Emote);
	TestTrue(TEXT("emote above Beat Hop"), EGFAnimOwner::Emote > EGFAnimOwner::BeatHop);
	TestTrue(TEXT("Beat Hop above locomotion"), EGFAnimOwner::BeatHop > EGFAnimOwner::Locomotion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAnimOwnerEmoteDuringFistTest, "GFGame.Visuals.AnimOwner.EmoteDuringFistScope", GFAnimOwnerTestsPrivate::Flags)
bool FGFAnimOwnerEmoteDuringFistTest::RunTest(const FString&)
{
	using namespace GFAnimOwnerTestsPrivate;
	FGFAnimOwnerStack S;
	TestTrue(TEXT("fist scope takes the mesh"), S.Claim(EGFAnimOwner::FistScope, Fist, true));
	const int64 FistSerial = TopSerial(S);
	TestFalse(TEXT("emote under fist scope is not on top"), S.Claim(EGFAnimOwner::Emote, Wave, false, 2.5f));
	TestEqual(TEXT("fist pose kept while the emote is live"), TopKey(S), Fist);
	TestEqual(TEXT("fist clip not restarted"), TopSerial(S), FistSerial);
	S.Tick(3.f);
	TestFalse(TEXT("emote expired underneath"), S.Has(EGFAnimOwner::Emote));
	TestEqual(TEXT("fist pose after the emote ends"), TopKey(S), Fist);
	TestEqual(TEXT("still the same fist clip"), TopSerial(S), FistSerial);

	// Release the fist while an emote is still waiting: the emote shows for the rest of its time.
	S.Claim(EGFAnimOwner::Emote, Wave, false, 2.f);
	S.Tick(0.5f);
	S.Release(EGFAnimOwner::FistScope);
	TestEqual(TEXT("emote restored after unscope"), Own(S.TopOwner()), Own(EGFAnimOwner::Emote));
	S.Tick(1.6f);
	TestEqual(TEXT("then locomotion"), Own(S.TopOwner()), Own(EGFAnimOwner::Locomotion));

	// Emote first, then scope: the fist pose replaces it, and unscoping before it expires restores the emote.
	S.Claim(EGFAnimOwner::Emote, Wave, true, 5.f);
	TestTrue(TEXT("fist scope over a playing emote"), S.Claim(EGFAnimOwner::FistScope, Fist, true));
	TestEqual(TEXT("fist on top"), TopKey(S), Fist);
	S.Tick(1.f);
	S.Release(EGFAnimOwner::FistScope);
	TestEqual(TEXT("emote back after unscope"), TopKey(S), Wave);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAnimOwnerSeatedTest, "GFGame.Visuals.AnimOwner.SeatedKeepsSit", GFAnimOwnerTestsPrivate::Flags)
bool FGFAnimOwnerSeatedTest::RunTest(const FString&)
{
	using namespace GFAnimOwnerTestsPrivate;
	FGFAnimOwnerStack S;
	S.Claim(EGFAnimOwner::FerrySeat, Sit, true);
	const int64 SitSerial = TopSerial(S);
	TestFalse(TEXT("fist scope while seated is not on top"), S.Claim(EGFAnimOwner::FistScope, Fist, true));
	TestFalse(TEXT("emote while seated is not on top"), S.Claim(EGFAnimOwner::Emote, Wave, false, 2.f));
	TestFalse(TEXT("Beat Hop while seated is not on top"), S.Claim(EGFAnimOwner::BeatHop, Hop, true, 0.2f));
	TestEqual(TEXT("sit kept"), TopKey(S), Sit);
	S.Release(EGFAnimOwner::FistScope);
	TestEqual(TEXT("fist release keeps the sit"), TopKey(S), Sit);
	S.Tick(2.5f);
	TestFalse(TEXT("emote expired"), S.Has(EGFAnimOwner::Emote));
	TestFalse(TEXT("unrenewed Beat Hop lease lapsed"), S.Has(EGFAnimOwner::BeatHop));
	TestEqual(TEXT("emote end keeps the sit"), TopKey(S), Sit);
	TestEqual(TEXT("sit never restarted"), TopSerial(S), SitSerial);
	S.Release(EGFAnimOwner::FerrySeat);
	TestEqual(TEXT("standing up hands back to locomotion"), Own(S.TopOwner()), Own(EGFAnimOwner::Locomotion));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAnimOwnerReleaseOrderTest, "GFGame.Visuals.AnimOwner.ReleaseOrder", GFAnimOwnerTestsPrivate::Flags)
bool FGFAnimOwnerReleaseOrderTest::RunTest(const FString&)
{
	using namespace GFAnimOwnerTestsPrivate;
	FGFAnimOwnerStack S;
	// Claimed lowest first and out of order; the stack sorts by owner, not by arrival.
	S.Claim(EGFAnimOwner::Emote, Wave, true, 100.f);
	S.Claim(EGFAnimOwner::BeatHop, Hop, true, 100.f);
	S.Claim(EGFAnimOwner::FerrySeat, Sit, true);
	S.Claim(EGFAnimOwner::FistScope, Fist, true);
	TestEqual(TEXT("four claims"), S.Num(), 4);
	TestEqual(TEXT("seat on top regardless of order"), Own(S.TopOwner()), Own(EGFAnimOwner::FerrySeat));

	S.Release(EGFAnimOwner::FerrySeat);
	TestEqual(TEXT("seat released -> fist"), TopKey(S), Fist);
	S.Release(EGFAnimOwner::Emote);
	TestEqual(TEXT("releasing a buried owner keeps the top"), TopKey(S), Fist);
	S.Release(EGFAnimOwner::FistScope);
	TestEqual(TEXT("fist released -> Beat Hop (emote already gone)"), TopKey(S), Hop);
	S.Release(EGFAnimOwner::BeatHop);
	TestEqual(TEXT("last release -> locomotion"), Own(S.TopOwner()), Own(EGFAnimOwner::Locomotion));
	TestFalse(TEXT("releasing twice is a no-op"), S.Release(EGFAnimOwner::BeatHop));

	// A restored owner gets its own clip back with its own settings.
	S.Claim(EGFAnimOwner::BeatHop, Hop, true, 100.f, 1.5f);
	const int64 HopSerial = TopSerial(S);
	S.Claim(EGFAnimOwner::Emote, Wave, false, 1.f);
	S.Tick(1.1f);
	const FGFAnimClaim* Back = S.Top();
	if (TestNotNull(TEXT("Beat Hop restored"), Back))
	{
		TestEqual(TEXT("restored key"), Back->Key, Hop);
		TestEqual(TEXT("restored rate"), Back->PlayRate, 1.5f);
		TestTrue(TEXT("restored loop"), Back->bLoop);
		TestEqual(TEXT("restored claim is the original one"), int64(Back->Serial), HopSerial);
	}

	// Claiming again replaces that owner's entry instead of stacking a second one.
	S.Claim(EGFAnimOwner::BeatHop, Wave, false, 100.f);
	TestEqual(TEXT("one entry per owner"), S.Num(), 1);
	TestEqual(TEXT("re-claim swaps the clip"), TopKey(S), Wave);
	TestNotEqual(TEXT("re-claim restarts (new serial)"), TopSerial(S), HopSerial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAnimOwnerLocomotionFallbackTest, "GFGame.Visuals.AnimOwner.LocomotionFallback", GFAnimOwnerTestsPrivate::Flags)
bool FGFAnimOwnerLocomotionFallbackTest::RunTest(const FString&)
{
	using namespace GFAnimOwnerTestsPrivate;
	FGFAnimOwnerStack S;
	TestNull(TEXT("empty stack has no top"), S.Top());
	TestEqual(TEXT("empty stack is locomotion"), Own(S.TopOwner()), Own(EGFAnimOwner::Locomotion));
	TestFalse(TEXT("locomotion can't be claimed"), S.Claim(EGFAnimOwner::Locomotion, Wave, true));
	TestFalse(TEXT("a None key is ignored"), S.Claim(EGFAnimOwner::Emote, NAME_None, true, 1.f));
	TestEqual(TEXT("still empty"), S.Num(), 0);

	// Beat Hop's lease: renewed each tick it stays, unrenewed it lapses and locomotion is back.
	S.Claim(EGFAnimOwner::BeatHop, Hop, true, 0.2f);
	for (int32 i = 0; i < 10; ++i)
	{
		S.Tick(0.1f);
		S.Renew(EGFAnimOwner::BeatHop, 0.2f);
	}
	TestEqual(TEXT("renewed lease holds"), Own(S.TopOwner()), Own(EGFAnimOwner::BeatHop));
	S.Tick(0.1f);
	TestEqual(TEXT("half a lease left"), Own(S.TopOwner()), Own(EGFAnimOwner::BeatHop));
	TestTrue(TEXT("lapse is reported"), S.Tick(0.15f));
	TestEqual(TEXT("lapsed lease -> locomotion"), Own(S.TopOwner()), Own(EGFAnimOwner::Locomotion));
	TestFalse(TEXT("renewing a lapsed claim fails"), S.Renew(EGFAnimOwner::BeatHop, 0.2f));

	// Untimed claims never time out; Renew doesn't turn them into timed ones.
	S.Claim(EGFAnimOwner::FistScope, Fist, true);
	S.Renew(EGFAnimOwner::FistScope, 0.1f);
	S.Tick(1000.f);
	TestEqual(TEXT("held claim survives any tick"), Own(S.TopOwner()), Own(EGFAnimOwner::FistScope));
	TestFalse(TEXT("zero tick does nothing"), S.Tick(0.f));
	S.Release(EGFAnimOwner::FistScope);
	TestNull(TEXT("released -> locomotion"), S.Top());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAnimOwnerComponentApiTest, "GFGame.Visuals.AnimOwner.ComponentApi", GFAnimOwnerTestsPrivate::Flags)
bool FGFAnimOwnerComponentApiTest::RunTest(const FString&)
{
	using namespace GFAnimOwnerTestsPrivate;
	// No owner and no mesh: the claims are tracked and nothing crashes when there is nothing to play on.
	UGFSimpleAnimComponent* Anim = NewObject<UGFSimpleAnimComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("component"), Anim))
	{
		return false;
	}
	TestEqual(TEXT("starts on locomotion"), Own(Anim->GetTopOwner()), Own(EGFAnimOwner::Locomotion));
	TestEqual(TEXT("no top key"), Anim->GetTopKey(), NAME_None);
	Anim->ClaimClip(EGFAnimOwner::FistScope, Fist, true);
	Anim->ClaimClip(EGFAnimOwner::Emote, Wave, false, 2.f);
	TestEqual(TEXT("emote during fist scope keeps the fist"), Anim->GetTopKey(), Fist);
	Anim->ClaimClip(EGFAnimOwner::FerrySeat, Sit, true);
	Anim->ReleaseClaim(EGFAnimOwner::FistScope);
	TestEqual(TEXT("fist release while seated keeps the sit"), Anim->GetTopKey(), Sit);
	Anim->ReleaseClaim(EGFAnimOwner::FerrySeat);
	TestEqual(TEXT("seat release restores the emote"), Own(Anim->GetTopOwner()), Own(EGFAnimOwner::Emote));
	TestTrue(TEXT("emote claim is there"), Anim->HasClaim(EGFAnimOwner::Emote));
	Anim->SetClaimPlayRate(EGFAnimOwner::Emote, 2.f);
	Anim->ReleaseClaim(EGFAnimOwner::Emote);
	TestEqual(TEXT("all released -> locomotion"), Own(Anim->GetTopOwner()), Own(EGFAnimOwner::Locomotion));
	TestFalse(TEXT("renew with nothing held"), Anim->RenewClaim(EGFAnimOwner::BeatHop, 0.2f));
	return true;
}

#endif
