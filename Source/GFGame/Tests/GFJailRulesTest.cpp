// Jail permissions, handover, mute/emote queries, session persistence across rejoin, and the server-only flag.
// Pure cases call FGFJailRules with no world. The component cases use a bare authority world (no net driver).
#include "Misc/AutomationTest.h"
#include <initializer_list>
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "../Social/GFJailRules.h"
#include "../Social/GFJailSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFJailTest
{
	using C = FGFJailCodes;
	static const FName Cinder(TEXT("Cinderwrights"));
	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));

	FGFJailActor Actor(const TCHAR* Id, FName Faction, int32 Order, bool bHost = false)
	{
		FGFJailActor A;
		A.PlayerId = Id;
		A.FactionId = Faction;
		A.JoinOrder = Order;
		A.bHost = bHost;
		A.bOnline = true;
		return A;
	}

	FGFJailMember Member(const TCHAR* Id, int32 Order, bool bJailed = false)
	{
		FGFJailMember M;
		M.PlayerId = Id;
		M.JoinOrder = Order;
		M.bJailed = bJailed;
		return M;
	}

	TArray<FGFJailMember> Crew(std::initializer_list<FGFJailMember> In)
	{
		TArray<FGFJailMember> Out;
		for (const FGFJailMember& M : In)
		{
			Out.Add(M);
		}
		return Out;
	}

	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFJailSubsystem* Jail = nullptr;
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
				Jail = UGFJailSubsystem::Ensure(GS);
			}
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Jail component"), Jail);
		}

		AGFPlayerState* Add(const FString& Key, FName Faction, bool bHost = false)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
			if (!PS)
			{
				return nullptr;
			}
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
			if (Jail)
			{
				Jail->NotePlayerJoined(PS);
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

using namespace GFJailTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFJailPermissionTest, "GFGame.Social.Jail.Permissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFJailPermissionTest::RunTest(const FString&)
{
	// Host is in Cinderwrights but claimed last, so Leader (earliest claim) leads. Three factions, one loner.
	const FGFJailActor Host = Actor(TEXT("Host"), Cinder, 3, true);
	const FGFJailActor Leader = Actor(TEXT("Leader"), Cinder, 1);
	const FGFJailActor Member = Actor(TEXT("Member"), Cinder, 2);
	const FGFJailActor Member2 = Actor(TEXT("Member2"), Cinder, 4);
	const FGFJailActor OtherLeader = Actor(TEXT("OtherLeader"), Verdant, 1);
	const FGFJailActor Other = Actor(TEXT("Other"), Verdant, 2);
	const FGFJailActor RimeLeader = Actor(TEXT("RimeLeader"), Rime, 1);
	const FGFJailActor Loner = Actor(TEXT("Loner"), NAME_None, 0);

	FGFJailContext Ctx;
	Ctx.Players.Add(Host);
	Ctx.Players.Add(Leader);
	Ctx.Players.Add(Member);
	Ctx.Players.Add(Member2);
	Ctx.Players.Add(OtherLeader);
	Ctx.Players.Add(Other);
	Ctx.Players.Add(RimeLeader);
	Ctx.Players.Add(Loner);
	TestEqual(TEXT("First claimer leads Cinderwrights"), FGFJailRules::LeaderOf(Cinder, Ctx), FString(TEXT("Leader")));
	TestEqual(TEXT("Verdant has its own leader"), FGFJailRules::LeaderOf(Verdant, Ctx), FString(TEXT("OtherLeader")));
	TestEqual(TEXT("Rime has its own leader"), FGFJailRules::LeaderOf(Rime, Ctx), FString(TEXT("RimeLeader")));

	// --- Jail: every permission row ---
	TestEqual(TEXT("Host jails own faction"), FGFJailRules::CanJail(Host, Member, Ctx), C::Ok);
	TestEqual(TEXT("Host jails another faction"), FGFJailRules::CanJail(Host, Other, Ctx), C::Ok);
	TestEqual(TEXT("Host jails another leader"), FGFJailRules::CanJail(Host, RimeLeader, Ctx), C::Ok);
	TestEqual(TEXT("Host jails a loner"), FGFJailRules::CanJail(Host, Loner, Ctx), C::Ok);
	TestEqual(TEXT("Host cannot jail self"), FGFJailRules::CanJail(Host, Host, Ctx), C::Self);

	TestEqual(TEXT("Leader jails own member"), FGFJailRules::CanJail(Leader, Member, Ctx), C::Ok);
	TestEqual(TEXT("Leader jails another faction"), FGFJailRules::CanJail(Leader, Other, Ctx), C::NotAllowed);
	TestEqual(TEXT("Leader jails another leader"), FGFJailRules::CanJail(Leader, OtherLeader, Ctx), C::NotAllowed);
	TestEqual(TEXT("Leader jails the host in their faction"), FGFJailRules::CanJail(Leader, Host, Ctx), C::TargetIsHost);
	TestEqual(TEXT("Leader cannot jail self"), FGFJailRules::CanJail(Leader, Leader, Ctx), C::Self);

	TestEqual(TEXT("Normal member jails the leader"), FGFJailRules::CanJail(Member, Leader, Ctx), C::NotAllowed);
	TestEqual(TEXT("Normal member cannot jail self"), FGFJailRules::CanJail(Member, Member, Ctx), C::Self);
	TestEqual(TEXT("Normal member jails a faction mate"), FGFJailRules::CanJail(Member, Member2, Ctx), C::NotAllowed);
	TestEqual(TEXT("Normal member jails the host"), FGFJailRules::CanJail(Member, Host, Ctx), C::TargetIsHost);
	TestEqual(TEXT("Normal member jails another faction"), FGFJailRules::CanJail(Member, Other, Ctx), C::NotAllowed);
	TestEqual(TEXT("Loner cannot jail"), FGFJailRules::CanJail(Loner, Member, Ctx), C::NotAllowed);
	TestEqual(TEXT("Other leader cannot jail Cinderwrights"), FGFJailRules::CanJail(OtherLeader, Member, Ctx), C::NotAllowed);
	TestEqual(TEXT("Other leader jails own member"), FGFJailRules::CanJail(OtherLeader, Other, Ctx), C::Ok);

	FGFJailContext JailedMember = Ctx;
	JailedMember.JailedIds.Add(TEXT("Member"));
	TestEqual(TEXT("Already jailed"), FGFJailRules::CanJail(Host, Member, JailedMember), C::AlreadyJailed);

	// --- Release: same rows, target jailed. Not limited to the original jailer. ---
	auto Released = [&](const TCHAR* Id) -> FGFJailContext
	{
		FGFJailContext Next = Ctx;
		Next.JailedIds.Add(Id);
		return Next;
	};
	TestEqual(TEXT("Host releases own faction"), FGFJailRules::CanRelease(Host, Member, Released(TEXT("Member"))), C::Ok);
	TestEqual(TEXT("Host releases another faction"), FGFJailRules::CanRelease(Host, Other, Released(TEXT("Other"))), C::Ok);
	TestEqual(TEXT("Host releases a loner"), FGFJailRules::CanRelease(Host, Loner, Released(TEXT("Loner"))), C::Ok);
	TestEqual(TEXT("Host cannot release self"), FGFJailRules::CanRelease(Host, Host, Released(TEXT("Host"))), C::Self);
	TestEqual(TEXT("Leader releases own member"), FGFJailRules::CanRelease(Leader, Member, Released(TEXT("Member"))), C::Ok);
	TestEqual(TEXT("Leader releases another faction"), FGFJailRules::CanRelease(Leader, Other, Released(TEXT("Other"))), C::NotAllowed);
	TestEqual(TEXT("Leader releases the host"), FGFJailRules::CanRelease(Leader, Host, Released(TEXT("Host"))), C::TargetIsHost);
	TestEqual(TEXT("Leader cannot release self"), FGFJailRules::CanRelease(Leader, Leader, Released(TEXT("Leader"))), C::Self);
	TestEqual(TEXT("Normal member cannot release a faction mate"), FGFJailRules::CanRelease(Member2, Member, Released(TEXT("Member"))), C::NotAllowed);
	TestEqual(TEXT("Not jailed"), FGFJailRules::CanRelease(Host, Member, Ctx), C::NotJailed);

	FGFJailActor Nobody;
	TestEqual(TEXT("No actor"), FGFJailRules::CanJail(Nobody, Member, Ctx), C::NoSuchPlayer);

	// The component applies the same rows.
	FWorld W;
	if (!W.Init(*this, TEXT("GFJailPermissions")))
	{
		return false;
	}
	// Claim order is add order. Leader claims Cinderwrights first; the host claims it last and is not the leader.
	AGFPlayerState* L = W.Add(TEXT("Leader"), Cinder);
	AGFPlayerState* M = W.Add(TEXT("Member"), Cinder);
	AGFPlayerState* H = W.Add(TEXT("Host"), Cinder, true);
	AGFPlayerState* OL = W.Add(TEXT("OtherLeader"), Verdant);
	AGFPlayerState* O = W.Add(TEXT("Other"), Verdant);
	AGFPlayerState* R = W.Add(TEXT("RimeLeader"), Rime);
	TestEqual(TEXT("Component: first claimer"), W.Jail->LeaderOf(Cinder), FString(TEXT("Leader")));
	TestEqual(TEXT("Component: host jails Rime"), W.Jail->Jail(H, R), C::Ok);
	TestTrue(TEXT("Component: Rime flag"), R->IsJailed());
	TestEqual(TEXT("Component: host releases Rime"), W.Jail->Release(H, R), C::Ok);
	TestEqual(TEXT("Component: leader jails own member"), W.Jail->Jail(L, M), C::Ok);
	TestEqual(TEXT("Component: leader jails Verdant"), W.Jail->Jail(L, O), C::NotAllowed);
	TestFalse(TEXT("Component: Verdant not jailed"), O->IsJailed());
	TestEqual(TEXT("Component: leader jails the host"), W.Jail->Jail(L, H), C::TargetIsHost);
	TestFalse(TEXT("Component: host not jailed"), H->IsJailed());
	TestEqual(TEXT("Component: leader jails self"), W.Jail->Jail(L, L), C::Self);
	TestEqual(TEXT("Component: member jails the leader"), W.Jail->Jail(M, L), C::NotAllowed);
	TestEqual(TEXT("Component: other leader cannot release"), W.Jail->Release(OL, M), C::NotAllowed);
	TestEqual(TEXT("Component: host releases the leader's prisoner"), W.Jail->Release(H, M), C::Ok);
	TestEqual(TEXT("Component: release again"), W.Jail->Release(H, M), C::NotJailed);
	TestEqual(TEXT("Component: null target"), W.Jail->Jail(H, nullptr), C::NoSuchPlayer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFJailPersistTest, "GFGame.Social.Jail.Persistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFJailPersistTest::RunTest(const FString&)
{
	FWorld W;
	if (!W.Init(*this, TEXT("GFJailPersist")))
	{
		return false;
	}
	TestFalse(TEXT("No tick, so nothing can time out"), W.Jail->PrimaryComponentTick.bCanEverTick);
	AGFPlayerState* H = W.Add(TEXT("Host"), NAME_None, true);
	AGFPlayerState* L = W.Add(TEXT("Leader"), Verdant);
	AGFPlayerState* M = W.Add(TEXT("M"), Verdant);
	const int32 EngineId = M->GetPlayerId();
	TestEqual(TEXT("Leader jails M"), W.Jail->Jail(L, M), C::Ok);
	TestTrue(TEXT("Flag on the player"), M->IsJailed());
	TestTrue(TEXT("Set holds the persistent id"), W.Jail->IsJailed(TEXT("M")));
	for (int32 i = 0; i < 30; ++i)
	{
		W.World->Tick(LEVELTICK_All, 1.f);
	}
	TestTrue(TEXT("No timer: still jailed"), W.Jail->IsJailed(TEXT("M")));
	TestEqual(TEXT("Host can release someone the leader jailed"), W.Jail->Release(H, M), C::Ok);
	TestFalse(TEXT("Released"), W.Jail->IsJailed(TEXT("M")));
	TestFalse(TEXT("Player flag cleared"), M->IsJailed());

	TestEqual(TEXT("Host jails M"), W.Jail->Jail(H, M), C::Ok);
	M->bLoggedOut = true;
	W.GS->RemovePlayerState(M);
	M->Destroy();
	TestTrue(TEXT("Leaving does not release"), W.Jail->IsJailed(TEXT("M")));

	AGFPlayerState* Back = W.Add(TEXT("M"), NAME_None);
	W.GS->NotifyModesPlayerReady(Back);
	TestNotEqual(TEXT("Rejoin is a new player state"), Back->GetPlayerId(), EngineId);
	TestEqual(TEXT("Same persistent id"), Back->PlayerKey, FString(TEXT("M")));
	TestEqual(TEXT("Rejoin keeps the faction"), Back->FactionId, Verdant);
	TestTrue(TEXT("Rejoin: still in the set"), W.Jail->IsJailed(TEXT("M")));
	TestTrue(TEXT("Rejoin: replicated flag set again"), Back->IsJailed());
	TestTrue(TEXT("Rejoin: muted"), W.Jail->IsMuted(TEXT("M")));
	TestFalse(TEXT("Rejoin: no emote"), W.Jail->CanEmote(TEXT("M")));
	TestEqual(TEXT("Own leader can still release"), W.Jail->Release(L, Back), C::Ok);
	TestFalse(TEXT("Released after rejoin"), W.Jail->IsJailed(TEXT("M")));
	TestFalse(TEXT("Flag clear after rejoin"), Back->IsJailed());
	TestFalse(TEXT("Not muted"), W.Jail->IsMuted(TEXT("M")));
	TestTrue(TEXT("Can emote"), W.Jail->CanEmote(TEXT("M")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFJailHandoverTest, "GFGame.Social.Jail.LeaderHandover", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFJailHandoverTest::RunTest(const FString&)
{
	const FGFJailMember A = Member(TEXT("A"), 1);
	const FGFJailMember B = Member(TEXT("B"), 2);
	const FGFJailMember C = Member(TEXT("C"), 3);
	FGFJailMember AJailed = A;
	AJailed.bJailed = true;
	FGFJailMember BJailed = B;
	BJailed.bJailed = true;
	FGFJailMember CJailed = C;
	CJailed.bJailed = true;

	TestEqual(TEXT("Earliest claim leads"), FGFJailRules::ChooseLeader(Crew({ A, B, C })), FString(TEXT("A")));
	TestEqual(TEXT("Leader left: next earliest"), FGFJailRules::ChooseLeader(Crew({ B, C })), FString(TEXT("B")));
	TestEqual(TEXT("Skip a jailed member while someone is free"), FGFJailRules::ChooseLeader(Crew({ AJailed, B, CJailed })), FString(TEXT("B")));
	TestEqual(TEXT("All remaining jailed: earliest of them"), FGFJailRules::ChooseLeader(Crew({ AJailed, BJailed, CJailed })), FString(TEXT("A")));
	TestEqual(TEXT("Leader left and both remaining are jailed"), FGFJailRules::ChooseLeader(Crew({ BJailed, CJailed })), FString(TEXT("B")));
	TestEqual(TEXT("Only a jailed member remains"), FGFJailRules::ChooseLeader(Crew({ CJailed })), FString(TEXT("C")));
	TestEqual(TEXT("Nobody remains"), FGFJailRules::ChooseLeader(TArray<FGFJailMember>()), FString());
	FGFJailMember AlsoFirst = Member(TEXT("A2"), 1);
	TestEqual(TEXT("Equal order breaks ties by id"), FGFJailRules::ChooseLeader(Crew({ AlsoFirst, A })), FString(TEXT("A")));

	// Jailed leader is not chosen, so the next free member may release them. A later member still may not.
	FGFJailActor Leader = Actor(TEXT("A"), Cinder, 1);
	FGFJailActor Next = Actor(TEXT("B"), Cinder, 2);
	FGFJailActor Later = Actor(TEXT("C"), Cinder, 3);
	FGFJailContext Ctx;
	Ctx.Players.Add(Leader);
	Ctx.Players.Add(Next);
	Ctx.Players.Add(Later);
	Ctx.JailedIds.Add(TEXT("A"));
	TestEqual(TEXT("Handover while the old leader is jailed"), FGFJailRules::LeaderOf(Cinder, Ctx), FString(TEXT("B")));
	TestEqual(TEXT("New leader can release them"), FGFJailRules::CanRelease(Next, Leader, Ctx), C::Ok);
	TestEqual(TEXT("A later member still cannot"), FGFJailRules::CanRelease(Later, Leader, Ctx), C::NotAllowed);

	FWorld W;
	if (!W.Init(*this, TEXT("GFJailHandover")))
	{
		return false;
	}
	AGFPlayerState* H = W.Add(TEXT("Host"), NAME_None, true);
	AGFPlayerState* PA = W.Add(TEXT("A"), Cinder);
	AGFPlayerState* PB = W.Add(TEXT("B"), Cinder);
	AGFPlayerState* PC = W.Add(TEXT("C"), Cinder);
	TestEqual(TEXT("Matches the first claimer"), W.Jail->LeaderOf(Cinder), W.GS->FactionLeaderOf(Cinder));
	TestEqual(TEXT("Game state leader is A"), W.GS->FactionLeaderOf(Cinder), FString(TEXT("A")));

	TestEqual(TEXT("Host jails A"), W.Jail->Jail(H, PA), C::Ok);
	TestEqual(TEXT("Jailed leader is skipped"), W.Jail->LeaderOf(Cinder), FString(TEXT("B")));
	TestEqual(TEXT("A cannot jail from inside"), W.Jail->Jail(PA, PC), C::NotAllowed);
	TestEqual(TEXT("B jails C"), W.Jail->Jail(PB, PC), C::Ok);
	TestEqual(TEXT("Host releases A"), W.Jail->Release(H, PA), C::Ok);
	TestEqual(TEXT("Host releases C"), W.Jail->Release(H, PC), C::Ok);
	TestEqual(TEXT("A leads again once free"), W.Jail->LeaderOf(Cinder), FString(TEXT("A")));

	TestEqual(TEXT("Host jails B"), W.Jail->Jail(H, PB), C::Ok);
	TestEqual(TEXT("Host jails C"), W.Jail->Jail(H, PC), C::Ok);
	PA->bLoggedOut = true;
	TestEqual(TEXT("A left: earliest jailed member leads"), W.Jail->LeaderOf(Cinder), FString(TEXT("B")));
	TestEqual(TEXT("Game state leader stays the first claimer"), W.GS->FactionLeaderOf(Cinder), FString(TEXT("A")));
	TestEqual(TEXT("Jailed successor can release a faction mate"), W.Jail->Release(PB, PC), C::Ok);
	TestFalse(TEXT("C is out"), W.Jail->IsJailed(TEXT("C")));
	TestEqual(TEXT("Free member leads once someone is free"), W.Jail->LeaderOf(Cinder), FString(TEXT("C")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFJailMuteTest, "GFGame.Social.Jail.MuteEmote", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFJailMuteTest::RunTest(const FString&)
{
	FGFJailContext Ctx;
	Ctx.Players.Add(Actor(TEXT("M"), Cinder, 1));
	TestFalse(TEXT("Free: not muted"), FGFJailRules::IsMuted(TEXT("M"), Ctx));
	TestTrue(TEXT("Free: can emote"), FGFJailRules::CanEmote(TEXT("M"), Ctx));
	Ctx.JailedIds.Add(TEXT("M"));
	TestTrue(TEXT("Jailed: voice and text muted"), FGFJailRules::IsMuted(TEXT("M"), Ctx));
	TestFalse(TEXT("Jailed: emotes blocked"), FGFJailRules::CanEmote(TEXT("M"), Ctx));
	TestFalse(TEXT("Someone else is not muted"), FGFJailRules::IsMuted(TEXT("Other"), Ctx));
	TestTrue(TEXT("Someone else can emote"), FGFJailRules::CanEmote(TEXT("Other"), Ctx));

	FWorld W;
	if (!W.Init(*this, TEXT("GFJailMute")))
	{
		return false;
	}
	AGFPlayerState* H = W.Add(TEXT("Host"), NAME_None, true);
	AGFPlayerState* M = W.Add(TEXT("M"), Rime);
	TestFalse(TEXT("Query: not muted"), W.Jail->IsMuted(TEXT("M")));
	TestTrue(TEXT("Query: can emote"), W.Jail->CanEmote(TEXT("M")));
	TestEqual(TEXT("Jail"), W.Jail->Jail(H, M), C::Ok);
	TestTrue(TEXT("Query: muted"), W.Jail->IsMuted(M->PlayerKey));
	TestFalse(TEXT("Query: emote blocked"), W.Jail->CanEmote(M->PlayerKey));
	TestFalse(TEXT("Host is not muted"), W.Jail->IsMuted(H->PlayerKey));
	TestTrue(TEXT("Host can emote"), W.Jail->CanEmote(H->PlayerKey));
	TestEqual(TEXT("Release"), W.Jail->Release(H, M), C::Ok);
	TestFalse(TEXT("Query: mute lifted"), W.Jail->IsMuted(TEXT("M")));
	TestTrue(TEXT("Query: emote restored"), W.Jail->CanEmote(TEXT("M")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFJailAuthorityTest, "GFGame.Social.Jail.ServerAuthority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFJailAuthorityTest::RunTest(const FString&)
{
	FWorld W;
	if (!W.Init(*this, TEXT("GFJailAuthority")))
	{
		return false;
	}
	AGFPlayerState* H = W.Add(TEXT("Host"), NAME_None, true);
	AGFPlayerState* M = W.Add(TEXT("M"), Cinder);
	AGFPlayerState* O = W.Add(TEXT("O"), Verdant);
	TestEqual(TEXT("Server can jail"), W.Jail->Jail(H, M), C::Ok);
	TestTrue(TEXT("Flag set on the server"), M->IsJailed());

	// A simulated proxy is what a client game state is. Jail/Release must leave the replicated bits alone.
	W.GS->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("Client jail is refused"), W.Jail->Jail(H, O), C::NotAuthority);
	TestFalse(TEXT("Client did not jail O"), W.Jail->IsJailed(TEXT("O")));
	TestFalse(TEXT("O's flag stayed clear"), O->IsJailed());
	TestEqual(TEXT("Client release is refused"), W.Jail->Release(H, M), C::NotAuthority);
	TestTrue(TEXT("M stays jailed"), W.Jail->IsJailed(TEXT("M")));
	TestTrue(TEXT("M's replicated flag stays set"), M->IsJailed());

	M->SetRole(ROLE_SimulatedProxy);
	M->SetJailed(false);
	TestTrue(TEXT("Client SetJailed(false) does not clear the flag"), M->IsJailed());
	O->SetRole(ROLE_SimulatedProxy);
	O->SetJailed(true);
	TestFalse(TEXT("Client SetJailed(true) does not raise the flag"), O->IsJailed());

	W.GS->SetRole(ROLE_Authority);
	M->SetRole(ROLE_Authority);
	O->SetRole(ROLE_Authority);
	TestEqual(TEXT("Server release still works"), W.Jail->Release(H, M), C::Ok);
	TestFalse(TEXT("Server cleared the flag"), M->IsJailed());
	TestFalse(TEXT("Set cleared"), W.Jail->IsJailed(TEXT("M")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
