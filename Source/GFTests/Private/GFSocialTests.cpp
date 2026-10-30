// GFSocial headless tests: the pure rules (faction filter, safe-zone gating, scale limits and revert, the jail and
// kick / ban permission matrices, emotes and the noise rules), plus the server component in a bare world (no net):
// jail mutes chat and voice, release by the same power as jail (host / own leader, no timer, survives a rejoin), bans
// save and refuse a join, emotes and cooldowns, and
// the noise delegate drawing a wild dino.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFModeTypes.h"
#include "GFSafeZone.h"
#include "GFNoise.h"
#include "GFAnimMap.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GFSocialTypes.h"
#include "GFSocialComponent.h"
#include "GFSocialDirector.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSocialTest
{
	using C = FGFSocialCodes;
	static const FName FA(TEXT("F1"));
	static const FName FB(TEXT("F2"));

	FGFSocialPlayer P(const TCHAR* Key, FName Faction, bool bHost = false, bool bSafe = true, bool bLeader = false)
	{
		return FGFSocialPlayer(Key, Faction, bHost, bSafe, bLeader);
	}

	/** A bare authority world with a game state and the social director (no net, no pawns). */
	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFSocialComponent* Social = nullptr;
		int32 NextId = 100;

		bool Init(FAutomationTestBase& T, const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World)
			{
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			GS = World->SpawnActor<AGFGameState>();
			if (GS)
			{
				World->SetGameState(GS);
			}
			Social = AGFSocialDirector::GetSocial(World, true);
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Social director"), Social);
		}

		AGFPlayerState* Add(const FString& Key, FName Faction, bool bHost = false)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
			if (PS)
			{
				PS->PlayerKey = Key;
				PS->SetPlayerId(++NextId);
				PS->bIsHost = bHost;
				PS->SetPlayerName(Key);
				if (GS->PlayerArray.Find(PS) == INDEX_NONE)
				{
					GS->AddPlayerState(PS);
				}
				if (!Faction.IsNone())
				{
					GS->ServerClaimFaction(PS, Faction);
				}
			}
			return PS;
		}

		~FWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

using namespace GFSocialTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialFactionFilterTest, "GFGame.Social.FactionFilter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialFactionFilterTest::RunTest(const FString&)
{
	const FGFSocialTuning T;
	const FGFSocialPlayer A = P(TEXT("A"), FA), A2 = P(TEXT("A2"), FA), B = P(TEXT("B"), FB), None = P(TEXT("N"), NAME_None);
	TestEqual(TEXT("Other faction: ok"), FGFSocialRules::CheckPower(A, B, 100.f, false, T), C::Ok);
	TestEqual(TEXT("And the other way"), FGFSocialRules::CheckPower(B, A, 100.f, false, T), C::Ok);
	TestEqual(TEXT("Own faction: never"), FGFSocialRules::CheckPower(A, A2, 100.f, false, T), C::SameFaction);
	TestEqual(TEXT("Never yourself"), FGFSocialRules::CheckPower(A, A, 0.f, false, T), C::Self);
	TestEqual(TEXT("Target without a faction: denied"), FGFSocialRules::CheckPower(A, None, 100.f, false, T), C::NoFaction);
	TestEqual(TEXT("Caster without a faction: denied"), FGFSocialRules::CheckPower(None, A, 100.f, false, T), C::NoFaction);
	TestEqual(TEXT("Out of range"), FGFSocialRules::CheckPower(A, B, T.PowerRange + 1.f, false, T), C::OutOfRange);
	TestEqual(TEXT("On cooldown"), FGFSocialRules::CheckPower(A, B, 100.f, true, T), C::Cooldown);
	FGFSocialPlayer JailedA = A;
	JailedA.bJailed = true;
	TestEqual(TEXT("Jailed caster"), FGFSocialRules::CheckPower(JailedA, B, 100.f, false, T), C::Jailed);
	TestEqual(TEXT("Jailed target"), FGFSocialRules::CheckPower(B, JailedA, 100.f, false, T), C::TargetJailed);

	// Factions on the game state: opaque ids, first claimer leads, seats capped.
	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialFactions")))
	{
		return false;
	}
	AGFPlayerState* L = W.Add(TEXT("L"), FA);
	AGFPlayerState* M = W.Add(TEXT("M"), FA);
	AGFPlayerState* X = W.Add(TEXT("X"), FB);
	TestEqual(TEXT("First claimer leads"), W.GS->FactionLeaderOf(FA), FString(TEXT("L")));
	TestTrue(TEXT("Leader"), W.GS->IsFactionLeader(L));
	TestFalse(TEXT("Member isn't"), W.GS->IsFactionLeader(M));
	TestTrue(TEXT("Other faction has its own leader"), W.GS->IsFactionLeader(X));
	TestEqual(TEXT("Same again"), W.GS->ServerClaimFaction(M, FA), FName(TEXT("Same")));
	W.Add(TEXT("M2"), FA);
	TestEqual(TEXT("Full at 3"), W.GS->ServerClaimFaction(W.Add(TEXT("M3"), NAME_None), FA), FName(TEXT("Full")));
	TestEqual(TEXT("Switch"), W.GS->ServerClaimFaction(M, FB), FName(TEXT("Ok")));
	TestEqual(TEXT("Leadership doesn't move with a member"), W.GS->FactionLeaderOf(FB), FString(TEXT("X")));
	// Through the "Player" mode verb too.
	AGFPlayerState* Y = W.Add(TEXT("Y"), NAME_None);
	FGFModeRequest Req = FGFModeRequest::Make(TEXT("Player"), TEXT("ClaimFaction"));
	Req.Names.Add(TEXT("F3"));
	TestTrue(TEXT("ClaimFaction verb"), W.GS->ServerHandleModeRequest(Y, Req).bOk);
	TestEqual(TEXT("Y's faction"), Y->FactionId, FName(TEXT("F3")));
	TestEqual(TEXT("Y leads F3"), W.GS->FactionLeaderOf(TEXT("F3")), FString(TEXT("Y")));
	// Powers through the component pick up the faction filter (no pawns here, so they stop at NoSuchPlayer first).
	TestEqual(TEXT("No pawn: no power"), W.Social->ServerPower(L, X, EGFSocialPower::Grow), C::NoSuchPlayer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialSafeZoneTest, "GFGame.Social.SafeZoneGating", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialSafeZoneTest::RunTest(const FString&)
{
	const FGFSocialTuning T;
	TestEqual(TEXT("Both safe: ok"), FGFSocialRules::CheckPower(P(TEXT("A"), FA), P(TEXT("B"), FB), 100.f, false, T), C::Ok);
	TestEqual(TEXT("Caster outside"), FGFSocialRules::CheckPower(P(TEXT("A"), FA, false, false), P(TEXT("B"), FB), 100.f, false, T), C::NotSafe);
	TestEqual(TEXT("Target outside"), FGFSocialRules::CheckPower(P(TEXT("A"), FA), P(TEXT("B"), FB, false, false), 100.f, false, T), C::TargetNotSafe);
	TestEqual(TEXT("Host outside is still outside"), FGFSocialRules::CheckPower(P(TEXT("H"), FA, true, false), P(TEXT("B"), FB), 100.f, false, T), C::NotSafe);

	// The world's zone comes from UGFSafeZoneSubsystem (now in GFCore): override first, else the newest provider.
	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialSafe")))
	{
		return false;
	}
	UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(W.World);
	if (!TestNotNull(TEXT("Safe-zone subsystem"), Zones))
	{
		return false;
	}
	TestFalse(TEXT("No zone: nothing is safe"), Zones->IsInsideSafe(FVector::ZeroVector));
	FGFSafeRegion R;
	R.Id = TEXT("R1");
	R.BaseRadius = 1000.f;
	Zones->SetOverride(MakeShared<FGFSafeCircles>(TArray<FGFSafeRegion>{ R }));
	TestTrue(TEXT("Inside"), Zones->IsInsideSafe(FVector(500.f, 0.f, -9000.f)));
	TestFalse(TEXT("Outside"), Zones->IsInsideSafe(FVector(1500.f, 0.f, 0.f)));
	Zones->SetOverride(nullptr);
	TestTrue(TEXT("Override cleared"), Zones->GetZone() == nullptr);
	// No pawn = not safe.
	TestFalse(TEXT("A player without a pawn isn't safe"), W.Social->IsPlayerSafe(W.Add(TEXT("A"), FA)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialScaleTest, "GFGame.Social.Scale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialScaleTest::RunTest(const FString&)
{
	FGFSocialTuning T;
	TestEqual(TEXT("Clamp low"), FGFSocialRules::ClampScale(0.1f, T), 0.5f);
	TestEqual(TEXT("Clamp high"), FGFSocialRules::ClampScale(9.f, T), 2.f);
	FGFScaleBook Book;
	TestEqual(TEXT("Default 1x"), Book.Get(TEXT("A")), 1.f);
	TestTrue(TEXT("One grow"), FMath::IsNearlyEqual(Book.Step(TEXT("A"), true, T), 1.25f));
	for (int32 i = 0; i < 10; ++i)
	{
		Book.Step(TEXT("A"), true, T);
	}
	TestEqual(TEXT("Grow caps at 2x"), Book.Get(TEXT("A")), 2.f);
	for (int32 i = 0; i < 20; ++i)
	{
		Book.Step(TEXT("B"), false, T);
	}
	TestEqual(TEXT("Shrink stops at 0.5x"), Book.Get(TEXT("B")), 0.5f);
	Book.Step(TEXT("C"), true, T);
	TestEqual(TEXT("Grow then shrink is exactly 1x again"), Book.Step(TEXT("C"), false, T), 1.f);
	TestEqual(TEXT("...and drops out of the book"), Book.Entries.Num(), 2);
	// Revert: only the ones outside safety.
	const TArray<FString> Reverted = Book.RevertUnsafe([](const FString& Key) { return Key == TEXT("A"); });
	TestEqual(TEXT("B reverted"), Reverted.Num(), 1);
	TestEqual(TEXT("B back to 1x"), Book.Get(TEXT("B")), 1.f);
	TestEqual(TEXT("A (still safe) keeps 2x"), Book.Get(TEXT("A")), 2.f);
	Book.Reset(TEXT("A"));
	TestEqual(TEXT("Reset"), Book.Get(TEXT("A")), 1.f);
	// Cooldowns.
	FGFCooldowns Cd;
	TestFalse(TEXT("Fresh"), Cd.IsCooling(TEXT("A"), TEXT("Grow"), 0.0));
	Cd.Start(TEXT("A"), TEXT("Grow"), 10.0, 2.0);
	TestTrue(TEXT("Cooling"), Cd.IsCooling(TEXT("A"), TEXT("Grow"), 11.0));
	TestFalse(TEXT("Per power"), Cd.IsCooling(TEXT("A"), TEXT("Shrink"), 11.0));
	TestFalse(TEXT("Per player"), Cd.IsCooling(TEXT("B"), TEXT("Grow"), 11.0));
	TestFalse(TEXT("Done"), Cd.IsCooling(TEXT("A"), TEXT("Grow"), 12.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialJailPermissionsTest, "GFGame.Social.JailPermissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialJailPermissionsTest::RunTest(const FString&)
{
	const FGFSocialPlayer Host = P(TEXT("H"), FA, true), Leader = P(TEXT("L"), FA, false, true, true), Member = P(TEXT("M"), FA),
		OtherLeader = P(TEXT("OL"), FB, false, true, true), Other = P(TEXT("O"), FB), Loner = P(TEXT("N"), NAME_None);
	// Host: anyone but themselves.
	TestEqual(TEXT("Host jails a member"), FGFSocialRules::CanJail(Host, Member), C::Ok);
	TestEqual(TEXT("Host jails another faction's leader"), FGFSocialRules::CanJail(Host, OtherLeader), C::Ok);
	TestEqual(TEXT("Host jails a faction-less player"), FGFSocialRules::CanJail(Host, Loner), C::Ok);
	TestEqual(TEXT("Not yourself"), FGFSocialRules::CanJail(Host, Host), C::Self);
	// Leader: own faction only, never the host.
	TestEqual(TEXT("Leader jails own member"), FGFSocialRules::CanJail(Leader, Member), C::Ok);
	TestEqual(TEXT("Leader can't jail another faction"), FGFSocialRules::CanJail(Leader, Other), C::NotAllowed);
	TestEqual(TEXT("Leader can't jail the host, even in their faction"), FGFSocialRules::CanJail(Leader, Host), C::TargetIsHost);
	TestEqual(TEXT("Leader can't jail another leader"), FGFSocialRules::CanJail(Leader, OtherLeader), C::NotAllowed);
	// Everyone else: denied.
	TestEqual(TEXT("Member can't jail"), FGFSocialRules::CanJail(Member, Leader), C::NotAllowed);
	TestEqual(TEXT("Member can't jail another member"), FGFSocialRules::CanJail(Other, Member), C::NotAllowed);
	TestEqual(TEXT("Faction-less can't jail"), FGFSocialRules::CanJail(Loner, Member), C::NotAllowed);
	FGFSocialPlayer Jailed = Member;
	Jailed.bJailed = true;
	TestEqual(TEXT("Already jailed"), FGFSocialRules::CanJail(Host, Jailed), C::AlreadyJailed);
	FGFSocialPlayer JailedLeader = Leader;
	JailedLeader.bJailed = true;
	TestEqual(TEXT("A jailed leader has no jail power"), FGFSocialRules::CanJail(JailedLeader, Member), C::Jailed);

	// Release: exactly the same matrix (design rule), on a jailed target. Who jailed them doesn't matter.
	FGFSocialPlayer JMember = Member, JOther = Other, JOtherLeader = OtherLeader, JLoner = Loner, JLeader = Leader;
	JMember.bJailed = JOther.bJailed = JOtherLeader.bJailed = JLoner.bJailed = JLeader.bJailed = true;
	TestEqual(TEXT("Host releases a member"), FGFSocialRules::CanRelease(Host, JMember), C::Ok);
	TestEqual(TEXT("Host releases another faction's leader"), FGFSocialRules::CanRelease(Host, JOtherLeader), C::Ok);
	TestEqual(TEXT("Host releases a faction-less player"), FGFSocialRules::CanRelease(Host, JLoner), C::Ok);
	TestEqual(TEXT("Leader releases own member"), FGFSocialRules::CanRelease(Leader, JMember), C::Ok);
	TestEqual(TEXT("Leader can't release another faction"), FGFSocialRules::CanRelease(Leader, JOther), C::NotAllowed);
	TestEqual(TEXT("Leader can't release another leader"), FGFSocialRules::CanRelease(Leader, JOtherLeader), C::NotAllowed);
	TestEqual(TEXT("Member can't release a faction mate"), FGFSocialRules::CanRelease(Member, JLeader), C::NotAllowed);
	TestEqual(TEXT("Other faction's member can't release"), FGFSocialRules::CanRelease(Other, JMember), C::NotAllowed);
	TestEqual(TEXT("Faction-less can't release"), FGFSocialRules::CanRelease(Loner, JMember), C::NotAllowed);
	TestEqual(TEXT("A prisoner can't release themselves"), FGFSocialRules::CanRelease(JMember, JMember), C::Self);
	TestEqual(TEXT("A jailed leader can't release from inside"), FGFSocialRules::CanRelease(JailedLeader, JMember), C::Jailed);
	TestEqual(TEXT("Not jailed"), FGFSocialRules::CanRelease(Host, Member), C::NotJailed);
	for (const FGFSocialPlayer* By : { &Host, &Leader, &Member, &Other, &OtherLeader, &Loner })
	{
		for (const FGFSocialPlayer* Target : { &Host, &Leader, &Member, &Other, &OtherLeader, &Loner })
		{
			FGFSocialPlayer JT = *Target;
			JT.bJailed = true;
			const bool bJailOk = FGFSocialRules::CanJail(*By, *Target) == C::Ok;
			const bool bReleaseOk = FGFSocialRules::CanRelease(*By, JT) == C::Ok;
			if (bJailOk != bReleaseOk)
			{
				AddError(FString::Printf(TEXT("Jail and release disagree for %s -> %s"), *By->Key, *Target->Key));
			}
		}
	}

	// The same matrix through the component (leader from the game state's first claimer).
	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialJailPerm")))
	{
		return false;
	}
	AGFPlayerState* H = W.Add(TEXT("H"), FB, true); // host in F2, so F1's leader is L
	AGFPlayerState* L = W.Add(TEXT("L"), FA);
	AGFPlayerState* M = W.Add(TEXT("M"), FA);
	AGFPlayerState* O = W.Add(TEXT("O"), FB);
	TestEqual(TEXT("Member denied"), W.Social->ServerJail(M, L), C::NotAllowed);
	TestEqual(TEXT("Leader denied on the other faction"), W.Social->ServerJail(L, O), C::NotAllowed);
	TestEqual(TEXT("Leader denied on the host"), W.Social->ServerJail(L, H), C::TargetIsHost);
	TestEqual(TEXT("Leader jails own member"), W.Social->ServerJail(L, M), C::Ok);
	TestTrue(TEXT("M jailed"), W.Social->IsJailed(TEXT("M")));
	TestEqual(TEXT("Jailer recorded"), W.Social->FindJail(TEXT("M"))->JailerKey, FString(TEXT("L")));
	TestEqual(TEXT("Host jails O"), W.Social->ServerJail(H, O), C::Ok);
	TestEqual(TEXT("Twice"), W.Social->ServerJail(H, O), C::AlreadyJailed);
	TestNotNull(TEXT("Cell spawned"), W.Social->Cell.Get());
	// Through the mode verb.
	FGFModeRequest Req = FGFModeRequest::Make(UGFSocialComponent::ModeName, TEXT("Jail"));
	Req.Ints.Add(L->GetPlayerId());
	TestEqual(TEXT("Verb: a jailed member can't jail anyone"), W.Social->HandleModeRequest(M, Req).Code, C::Jailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialMuteTest, "GFGame.Social.Mute", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialMuteTest::RunTest(const FString&)
{
	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialMute")))
	{
		return false;
	}
	AGFPlayerState* H = W.Add(TEXT("H"), FA, true);
	AGFPlayerState* M = W.Add(TEXT("M"), FB);
	TestEqual(TEXT("Chat ok"), W.GS->ServerHandleChat(M, TEXT("  hello  ")), FName(TEXT("Ok")));
	TestEqual(TEXT("Chat logged, trimmed"), W.GS->ChatLog.Num() > 0 ? W.GS->ChatLog.Last().Text : FString(), FString(TEXT("hello")));
	TestEqual(TEXT("Empty refused"), W.GS->ServerHandleChat(M, TEXT("   ")), FName(TEXT("Empty")));
	TestEqual(TEXT("Jail"), W.Social->ServerJail(H, M), C::Ok);
	const int32 Before = W.GS->ChatLog.Num();
	TestEqual(TEXT("Jailed: chat muted"), W.GS->ServerHandleChat(M, TEXT("let me out")), C::Muted);
	TestEqual(TEXT("Nothing logged"), W.GS->ChatLog.Num(), Before);
	TestEqual(TEXT("Others still chat"), W.GS->ServerHandleChat(H, TEXT("quiet in there")), FName(TEXT("Ok")));
	TestTrue(TEXT("Voice muted (stub)"), W.Social->Voice && W.Social->Voice->IsVoiceMuted(TEXT("M")));
	TestEqual(TEXT("Jailed: no emotes"), W.Social->ServerEmote(M, TEXT("Hello")), C::Jailed);
	TestEqual(TEXT("Release"), W.Social->ServerRelease(H, M), C::Ok);
	TestEqual(TEXT("Chat back"), W.GS->ServerHandleChat(M, TEXT("thanks")), FName(TEXT("Ok")));
	TestFalse(TEXT("Voice back"), W.Social->Voice->IsVoiceMuted(TEXT("M")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialReleaseTest, "GFGame.Social.Release", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialReleaseTest::RunTest(const FString&)
{
	// Design rule: release uses jail power (host: anyone; leader: own faction), not limited to the jailer; no timer.
	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialRelease")))
	{
		return false;
	}
	AGFPlayerState* H = W.Add(TEXT("H"), FA, true);
	AGFPlayerState* L = W.Add(TEXT("L"), FB); // F2's leader
	AGFPlayerState* M = W.Add(TEXT("M"), FB);
	AGFPlayerState* M2 = W.Add(TEXT("M2"), FB);
	AGFPlayerState* OL = W.Add(TEXT("OL"), FName(TEXT("F3"))); // another faction's leader
	TestTrue(TEXT("L leads F2"), W.GS->IsFactionLeader(L));
	TestEqual(TEXT("Leader jails M"), W.Social->ServerJail(L, M), C::Ok);
	TestEqual(TEXT("Faction mate can't release"), W.Social->ServerRelease(M2, M), C::NotAllowed);
	TestEqual(TEXT("Another faction's leader can't release"), W.Social->ServerRelease(OL, M), C::NotAllowed);
	TestEqual(TEXT("The prisoner can't release themselves"), W.Social->ServerRelease(M, M), C::Self);
	TestEqual(TEXT("Jailer (leader) releases"), W.Social->ServerRelease(L, M), C::Ok);
	TestFalse(TEXT("Free"), W.Social->IsJailed(TEXT("M")));
	TestEqual(TEXT("Release again"), W.Social->ServerRelease(L, M), C::NotJailed);
	TestEqual(TEXT("Leader jails M2"), W.Social->ServerJail(L, M2), C::Ok);
	TestEqual(TEXT("Host releases a leader's prisoner"), W.Social->ServerRelease(H, M2), C::Ok);
	TestEqual(TEXT("Host jails M"), W.Social->ServerJail(H, M), C::Ok);
	TestEqual(TEXT("Leader releases the host's prisoner (own faction)"), W.Social->ServerRelease(L, M), C::Ok);
	FGFModeRequest Req = FGFModeRequest::Make(UGFSocialComponent::ModeName, TEXT("Unjail"));
	Req.Ints.Add(M->GetPlayerId());
	TestEqual(TEXT("Verb: release a free player"), W.Social->HandleModeRequest(L, Req).Code, C::NotJailed);

	// No timer: still jailed after a long while (the server tick runs, nothing lets them out).
	TestEqual(TEXT("Host jails M again"), W.Social->ServerJail(H, M), C::Ok);
	for (int32 i = 0; i < 120; ++i)
	{
		W.World->Tick(LEVELTICK_All, 1.f); // two simulated minutes
	}
	TestTrue(TEXT("No timer: still jailed"), W.Social->IsJailed(TEXT("M")));

	// Survives a rejoin within the session: leave, then a new player state with the same stable key.
	W.Social->OnModePlayerLeft(M);
	W.GS->RemovePlayerState(M);
	M->Destroy();
	TestTrue(TEXT("Still jailed after leaving"), W.Social->IsJailed(TEXT("M")));
	AGFPlayerState* Back = W.Add(TEXT("M"), NAME_None);
	W.GS->NotifyModesPlayerReady(Back);
	TestEqual(TEXT("Rejoin keeps the faction"), Back->FactionId, FB);
	TestTrue(TEXT("Rejoin: still jailed"), W.Social->IsJailed(TEXT("M")));
	TestTrue(TEXT("Rejoin: voice muted again"), W.Social->Voice && W.Social->Voice->IsVoiceMuted(TEXT("M")));
	TestEqual(TEXT("Rejoin: chat muted"), W.GS->ServerHandleChat(Back, TEXT("I'm back")), C::Muted);
	TestEqual(TEXT("Rejoin: no emotes"), W.Social->ServerEmote(Back, TEXT("Hello")), C::Jailed);
	TestEqual(TEXT("Rejoin: own leader can still release"), W.Social->ServerRelease(L, Back), C::Ok);
	TestFalse(TEXT("Released"), W.Social->IsJailed(TEXT("M")));
	TestFalse(TEXT("Voice back"), W.Social->Voice->IsVoiceMuted(TEXT("M")));

	// Session only: never written to the world save.
	TestEqual(TEXT("Jail M once more"), W.Social->ServerJail(H, Back), C::Ok);
	TArray<uint8> Blob;
	W.Social->CaptureModeState(Blob);
	TestEqual(TEXT("Jail isn't written to the world save"), Blob.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialKickBanTest, "GFGame.Social.KickBan", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialKickBanTest::RunTest(const FString&)
{
	const FGFSocialPlayer Host = P(TEXT("H"), FA, true), Leader = P(TEXT("L"), FB, false, true, true), Member = P(TEXT("M"), FB);
	TestEqual(TEXT("Host kicks"), FGFSocialRules::CanKickOrBan(Host, Member), C::Ok);
	TestEqual(TEXT("Host kicks a leader"), FGFSocialRules::CanKickOrBan(Host, Leader), C::Ok);
	TestEqual(TEXT("Leader can't (host only for now)"), FGFSocialRules::CanKickOrBan(Leader, Member), C::NotAllowed);
	TestEqual(TEXT("Member can't"), FGFSocialRules::CanKickOrBan(Member, Leader), C::NotAllowed);
	TestEqual(TEXT("Nobody kicks the host"), FGFSocialRules::CanKickOrBan(Leader, Host), C::TargetIsHost);
	TestEqual(TEXT("Not yourself"), FGFSocialRules::CanKickOrBan(Host, Host), C::Self);

	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialBan")))
	{
		return false;
	}
	AGFPlayerState* H = W.Add(TEXT("H"), FA, true);
	AGFPlayerState* L = W.Add(TEXT("L"), FB);
	AGFPlayerState* M = W.Add(TEXT("SteamLike-123"), FB);
	TestEqual(TEXT("Leader can't ban"), W.Social->ServerBan(L, M), C::NotAllowed);
	TestEqual(TEXT("Leader can't kick"), W.Social->ServerKick(L, M), C::NotAllowed);
	TestEqual(TEXT("Host bans"), W.Social->ServerBan(H, M), C::Ok);
	TestTrue(TEXT("Banned by stable key"), W.Social->IsBanned(TEXT("SteamLike-123")));
	TestEqual(TEXT("Ban records who and by whom"), W.Social->Bans.Num() == 1 ? W.Social->Bans[0].ByKey : FString(), FString(TEXT("H")));
	FString Why;
	TestTrue(TEXT("Join refused (game state gate)"), W.GS->IsJoinRefused(TEXT("SteamLike-123"), Why));
	TestFalse(TEXT("Reason given"), Why.IsEmpty());
	TestFalse(TEXT("Others still welcome"), W.GS->IsJoinRefused(TEXT("Someone"), Why));

	// Saved with the world (mode blob "Social") and back.
	TMap<FName, FGFModeBlob> Saved = W.GS->GatherModeStates();
	TestTrue(TEXT("In the world save"), Saved.Contains(UGFSocialComponent::ModeName));
	W.Social->Bans.Reset();
	TestFalse(TEXT("Cleared"), W.Social->IsBanned(TEXT("SteamLike-123")));
	W.GS->RestoreModeStates(Saved);
	TestTrue(TEXT("Restored from the save"), W.Social->IsBanned(TEXT("SteamLike-123")));

	// Unban: same permission function.
	TestEqual(TEXT("Leader can't unban"), W.Social->ServerUnban(L, TEXT("SteamLike-123")), C::NotAllowed);
	FGFModeRequest Req = FGFModeRequest::Make(UGFSocialComponent::ModeName, TEXT("Unban"));
	Req.Names.Add(TEXT("SteamLike-123"));
	TestTrue(TEXT("Host unbans (verb)"), W.Social->HandleModeRequest(H, Req).bOk);
	TestFalse(TEXT("Welcome again"), W.GS->IsJoinRefused(TEXT("SteamLike-123"), Why));
	TestEqual(TEXT("Unban twice"), W.Social->ServerUnban(H, TEXT("SteamLike-123")), C::NotBanned);
	TArray<uint8> Blob;
	W.Social->CaptureModeState(Blob);
	TestEqual(TEXT("Empty ban list: nothing saved"), Blob.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialEmotesTest, "GFGame.Social.Emotes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialEmotesTest::RunTest(const FString&)
{
	const TArray<FName> Ids = FGFSocialRules::EmoteIds();
	for (const TCHAR* Id : { TEXT("Heart"), TEXT("ThumbsUp"), TEXT("ThumbsDown"), TEXT("Yes"), TEXT("No"), TEXT("Bye"), TEXT("Hello") })
	{
		const FGFEmoteRow* Row = FGFSocialRules::FindEmote(Id);
		TestNotNull(*FString::Printf(TEXT("Emote %s in DT_Emotes"), Id), Row);
		if (Row)
		{
			TestFalse(*FString::Printf(TEXT("%s has an icon"), Id), Row->Icon.IsEmpty());
			TestTrue(*FString::Printf(TEXT("%s shows a while"), Id), Row->Seconds > 0.f);
		}
	}
	TestTrue(TEXT("At least the 7 asked-for emotes"), Ids.Num() >= 7);
	// Clips where DT_AnimMap has them, the bubble otherwise.
	auto Anim = [](const TCHAR* Id) { const FGFEmoteRow* R = FGFSocialRules::FindEmote(Id); return R ? FGFSocialRules::EmoteAnimKey(*R) : FName(TEXT("?")); };
	TestEqual(TEXT("Hello waves"), Anim(TEXT("Hello")), FName(TEXT("Player.Emote_Wave")));
	TestEqual(TEXT("Bye waves"), Anim(TEXT("Bye")), FName(TEXT("Player.Emote_Wave")));
	TestEqual(TEXT("Thumbs up cheers"), Anim(TEXT("ThumbsUp")), FName(TEXT("Player.Emote_Cheer")));
	TestEqual(TEXT("Sit sits"), Anim(TEXT("Sit")), FName(TEXT("Player.Emote_Sit")));
	TestEqual(TEXT("Dance dances"), Anim(TEXT("Dance")), FName(TEXT("Player.Emote_Dance")));
	TestEqual(TEXT("Laugh laughs"), Anim(TEXT("Laugh")), FName(TEXT("Player.Emote_Laugh")));
	TestEqual(TEXT("Heart: bubble"), Anim(TEXT("Heart")), FName());
	TestEqual(TEXT("Yes: bubble"), Anim(TEXT("Yes")), FName());
	FGFEmoteRow Missing;
	Missing.AnimKey = TEXT("Player.Emote_NotAClip");
	TestEqual(TEXT("Unknown anim key falls back to the bubble"), FGFSocialRules::EmoteAnimKey(Missing), FName());
	for (const FName& Id : Ids)
	{
		const FGFEmoteRow* R = FGFSocialRules::FindEmote(Id);
		if (R && !R->AnimKey.IsNone() && !FGFAnimMap::Find(R->AnimKey))
		{
			AddError(FString::Printf(TEXT("%s names a DT_AnimMap key that isn't there: %s"), *Id.ToString(), *R->AnimKey.ToString()));
		}
	}

	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialEmotes")))
	{
		return false;
	}
	AGFPlayerState* A = W.Add(TEXT("A"), NAME_None); // anyone, faction or not, anywhere
	AGFPlayerState* B = W.Add(TEXT("B"), FB);
	const int32 Seen = W.Social->EmotesSeen;
	TestEqual(TEXT("Emote"), W.Social->ServerEmote(A, TEXT("Heart")), C::Ok);
	TestEqual(TEXT("Logged for replication"), W.Social->Emotes.Num() > 0 ? W.Social->Emotes.Last().EmoteId : FName(), FName(TEXT("Heart")));
	TestEqual(TEXT("Shown on the server too"), W.Social->EmotesSeen, Seen + 1);
	TestEqual(TEXT("Short cooldown"), W.Social->ServerEmote(A, TEXT("Yes")), C::Cooldown);
	TestEqual(TEXT("Others aren't on your cooldown"), W.Social->ServerEmote(B, TEXT("Yes")), C::Ok);
	TestEqual(TEXT("Unknown emote"), W.Social->ServerEmote(B, TEXT("Moonwalk")), C::UnknownEmote);
	// The log is capped (cooldown off; a fresh player so no earlier cooldown is pending).
	W.Social->Tuning.EmoteCooldownSeconds = 0.f;
	AGFPlayerState* Spammer = W.Add(TEXT("S"), FA);
	for (int32 i = 0; i < UGFSocialComponent::MaxEmotes + 4; ++i)
	{
		W.Social->ServerEmote(Spammer, TEXT("Hello"));
	}
	TestEqual(TEXT("Log capped"), W.Social->Emotes.Num(), UGFSocialComponent::MaxEmotes);
	AGFPlayerState* D = W.Add(TEXT("D"), FA);
	FGFModeRequest Req = FGFModeRequest::Make(UGFSocialComponent::ModeName, TEXT("Emote"));
	Req.Names.Add(TEXT("ThumbsDown"));
	TestTrue(TEXT("Verb"), W.Social->HandleModeRequest(D, Req).bOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialNoiseTest, "GFGame.Social.Noise", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialNoiseTest::RunTest(const FString&)
{
	// Mode flags (DT_ModeRules): free-roaming enemies on, rail modes off.
	TestTrue(TEXT("Open world: on"), FGFSocialRules::EmoteAttractsDanger(TEXT("OpenWorld")));
	TestTrue(TEXT("Dino zone: on"), FGFSocialRules::EmoteAttractsDanger(TEXT("DinoZone")));
	TestFalse(TEXT("MOBA lanes: off"), FGFSocialRules::EmoteAttractsDanger(TEXT("Moba")));
	TestFalse(TEXT("Rhythm: off"), FGFSocialRules::EmoteAttractsDanger(TEXT("Rhythm")));
	TestFalse(TEXT("Unknown mode: off"), FGFSocialRules::EmoteAttractsDanger(TEXT("NotAMode")));
	TestTrue(TEXT("Outside safety in the open world: noise"), FGFSocialRules::EmoteMakesNoise(false, TEXT("OpenWorld")));
	TestFalse(TEXT("Inside safety: quiet"), FGFSocialRules::EmoteMakesNoise(true, TEXT("OpenWorld")));
	TestFalse(TEXT("Outside safety in the MOBA: quiet"), FGFSocialRules::EmoteMakesNoise(false, TEXT("Moba")));
	const FGFModeRulesRow* Open = FGFSocialRules::FindModeRules(TEXT("OpenWorld"));
	TestTrue(TEXT("Noise radius"), Open && Open->NoiseRadius > 1000.f);
	// The dino side (UGFTamingComponent listens to FGFNoise).
	TestTrue(TEXT("Wild in range hears"), UGFTamingComponent::ShouldHearNoise(EGFTameState::Wild, false, 1000.f, 2500.f));
	TestTrue(TEXT("Alert in range hears"), UGFTamingComponent::ShouldHearNoise(EGFTameState::Alert, false, 1000.f, 2500.f));
	TestFalse(TEXT("Out of range"), UGFTamingComponent::ShouldHearNoise(EGFTameState::Wild, false, 3000.f, 2500.f));
	TestFalse(TEXT("Tamed ignores"), UGFTamingComponent::ShouldHearNoise(EGFTameState::Tamed, false, 10.f, 2500.f));
	TestFalse(TEXT("Fled ignores"), UGFTamingComponent::ShouldHearNoise(EGFTameState::Fled, false, 10.f, 2500.f));
	TestFalse(TEXT("Calming Mist wins"), UGFTamingComponent::ShouldHearNoise(EGFTameState::Wild, true, 10.f, 2500.f));

	// A real dino in a bare world.
	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialNoise")))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFDino* Near = W.World->SpawnActor<AGFDino>(AGFDino::StaticClass(), FVector(1500.f, 0.f, 100.f), FRotator::ZeroRotator, Params);
	AGFDino* Far = W.World->SpawnActor<AGFDino>(AGFDino::StaticClass(), FVector(9000.f, 0.f, 100.f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Dinos"), Near) || !TestNotNull(TEXT("Far dino"), Far))
	{
		return false;
	}
	Near->SetSpecies(TEXT("Raptor"));
	Far->SetSpecies(TEXT("Raptor"));
	FGFNoiseEvent Noise;
	Noise.Location = FVector::ZeroVector;
	Noise.Radius = 2500.f;
	Noise.SourceKey = TEXT("Newbie");
	Noise.Tag = TEXT("Emote");
	FGFNoise::Raise(nullptr, Noise);
	TestEqual(TEXT("No world: nothing"), Near->Taming->NoisesHeard, 0);
	FGFNoise::Raise(W.World, Noise);
	TestTrue(TEXT("Near raptor turns hostile"), Near->Taming->GetTameState() == EGFTameState::Hostile);
	TestEqual(TEXT("...at the noise maker"), Near->Taming->GetTargetKey(), FString(TEXT("Newbie")));
	TestTrue(TEXT("Far raptor doesn't hear it"), Far->Taming->GetTameState() != EGFTameState::Hostile);
	FGFNoiseEvent Second = Noise;
	Second.SourceKey = TEXT("Other");
	FGFNoise::Raise(W.World, Second);
	TestEqual(TEXT("Already hostile: keeps its target"), Near->Taming->GetTargetKey(), FString(TEXT("Newbie")));

	// The emote side: a player with no pawn never makes noise (no location); bNoise is only set with a pawn outside safety.
	AGFPlayerState* A = W.Add(TEXT("A"), FA);
	const int32 Before = W.Social->NoisesRaised;
	TestEqual(TEXT("Emote"), W.Social->ServerEmote(A, TEXT("Hello")), C::Ok);
	TestEqual(TEXT("No pawn: no noise"), W.Social->NoisesRaised, Before);
	TestEqual(TEXT("Default player mode"), W.Social->GetPlayerMode(TEXT("A")), FName(TEXT("OpenWorld")));
	W.Social->ServerSetPlayerMode(TEXT("A"), TEXT("Moba"));
	TestEqual(TEXT("Mode set"), W.Social->GetPlayerMode(TEXT("A")), FName(TEXT("Moba")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSocialLeaderHandoverTest, "GFGame.Social.JailLeaderHandover", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSocialLeaderHandoverTest::RunTest(const FString&)
{
	auto Seat = [](const TCHAR* Key, int32 Order, bool bJailed = false)
	{
		FGFSocialPlayer M;
		M.Key = Key;
		M.ClaimOrder = Order;
		M.bJailed = bJailed;
		return M;
	};
	TArray<FGFSocialPlayer> S;
	S.Add(Seat(TEXT("A"), 1));
	S.Add(Seat(TEXT("B"), 2));
	S.Add(Seat(TEXT("C"), 3));
	TestEqual(TEXT("Earliest claim"), FGFSocialRules::ChooseLeader(S), FString(TEXT("A")));
	S.RemoveAt(0);
	TestEqual(TEXT("Leader left"), FGFSocialRules::ChooseLeader(S), FString(TEXT("B")));
	S.Empty();
	S.Add(Seat(TEXT("A"), 1, true));
	S.Add(Seat(TEXT("B"), 2));
	S.Add(Seat(TEXT("C"), 3, true));
	TestEqual(TEXT("Skip a jailed member"), FGFSocialRules::ChooseLeader(S), FString(TEXT("B")));
	S[1].bJailed = true;
	TestEqual(TEXT("All jailed: earliest of them"), FGFSocialRules::ChooseLeader(S), FString(TEXT("A")));
	S.RemoveAt(0);
	TestEqual(TEXT("Left, and both remaining are jailed"), FGFSocialRules::ChooseLeader(S), FString(TEXT("B")));
	S.Empty();
	S.Add(Seat(TEXT("A"), 0));
	S.Add(Seat(TEXT("B"), 2));
	TestEqual(TEXT("No claim order sorts last"), FGFSocialRules::ChooseLeader(S), FString(TEXT("B")));
	S.Empty();
	TestEqual(TEXT("Nobody online"), FGFSocialRules::ChooseLeader(S), FString());

	FWorld W;
	if (!W.Init(*this, TEXT("GFSocialHandover")))
	{
		return false;
	}
	AGFPlayerState* H = W.Add(TEXT("H"), NAME_None, true);
	AGFPlayerState* A = W.Add(TEXT("A"), FA);
	AGFPlayerState* B = W.Add(TEXT("B"), FA);
	AGFPlayerState* C = W.Add(TEXT("C"), FA);
	TestTrue(TEXT("First claim leads"), W.Social->Describe(A).bLeader);
	TestFalse(TEXT("Later claim does not"), W.Social->Describe(B).bLeader);
	TestEqual(TEXT("Game state leader is still the first claimer"), W.GS->FactionLeaderOf(FA), FString(TEXT("A")));
	TestEqual(TEXT("Host jails A"), W.Social->ServerJail(H, A), C::Ok);
	TestTrue(TEXT("Next free member leads"), W.Social->Describe(B).bLeader);
	TestEqual(TEXT("That member can jail"), W.Social->ServerJail(B, C), C::Ok);
	TestEqual(TEXT("Host jails B"), W.Social->ServerJail(H, B), C::Ok);
	TestTrue(TEXT("All jailed: earliest claim leads"), W.Social->Describe(A).bLeader);
	A->bLoggedOut = true;
	TestTrue(TEXT("A left: earliest jailed member leads"), W.Social->Describe(B).bLeader);
	TestFalse(TEXT("C is later"), W.Social->Describe(C).bLeader);
	return true;
}

#endif
