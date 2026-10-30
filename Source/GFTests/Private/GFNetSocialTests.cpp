// GFSocial over the network (host + 2 clients): factions and leaders replicate; a cross-faction Grow works and
// replicates the new size, a same-faction one is refused; leaving safety reverts it; Grab pulls the target; a faction
// the host jails a member (teleported underground, chat muted, voice stub muted, drip loop on that client only) and
// the member's faction leader releases them (release isn't limited to the jailer); emotes replicate to everyone; an emote outside safety in the open world draws a raptor to the
// emoter, one in a rail mode doesn't; a non-host can't kick; the host bans a client (kicked, refused at join).
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFModeTypes.h"
#include "GFSafeZone.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GFSocialComponent.h"
#include "GFSocialDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetSocialTest
{
	static const FName F1(TEXT("F1"));
	static const FName F2(TEXT("F2"));

	struct FRun
	{
		FString Host, C0, C1;
		int32 Id = 0;
		int32 Id2 = 0;
		int32 SeenBefore = 0;
		int32 NoisesBefore = 0;
		int32 ChatResultsBefore = 0;
		FVector C1Before = FVector::ZeroVector;
		TWeakObjectPtr<AGFDino> Raptor;
		TWeakObjectPtr<AGFDino> Raptor2;
	};

	AGFPlayerState* Client(int32 i) { return LocalPlayerState(ClientWorlds()[i]); }
	AGFPlayerState* OnServer(const FString& Key) { return PlayerStateByKey(ServerWorld(), Key); }
	UGFSocialComponent* ServerSocial() { return AGFSocialDirector::GetSocial(ServerWorld(), true); }
	UGFSocialComponent* ClientSocial(int32 i) { return AGFSocialDirector::GetSocial(ClientWorlds()[i], false); }

	void Place(const FString& Key, float X, float Y)
	{
		AGFPlayerState* PS = OnServer(Key);
		if (APawn* Pawn = PS ? PS->GetPawn() : nullptr)
		{
			Pawn->TeleportTo(FVector(X, Y, Pawn->GetActorLocation().Z), FRotator::ZeroRotator, false, true);
		}
	}

	/** A verb from client i aimed at Target (by player id). */
	int32 Ask(int32 i, FName Verb, const FString& TargetKey, FName Name = NAME_None)
	{
		FGFModeRequest R = FGFModeRequest::Make(UGFSocialComponent::ModeName, Verb);
		if (!TargetKey.IsEmpty())
		{
			const AGFPlayerState* T = PlayerStateByKey(ClientWorlds()[i], TargetKey);
			R.Ints.Add(T ? T->GetPlayerId() : -1);
		}
		if (!Name.IsNone())
		{
			R.Names.Add(Name);
		}
		return Client(i)->RequestMode(R);
	}

	const FGFModeResult* Result(int32 i, int32 Id) { return Client(i)->ModeResults.Find(Id); }

	float ScaleSeen(UWorld* World, const FString& Key)
	{
		const AGFPlayerState* PS = PlayerStateByKey(World, Key);
		const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		return Pawn ? Pawn->GetActorScale3D().X : -1.f;
	}

	AGFDino* SpawnRaptor(float X, float Y)
	{
		UWorld* World = ServerWorld();
		const APawn* Ref = LocalPlayerState(World)->GetPawn();
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), FVector(X, Y, Ref ? Ref->GetActorLocation().Z : 200.f), FRotator::ZeroRotator, P);
		if (Dino)
		{
			Dino->SetSpecies(TEXT("Raptor"));
			Dino->GetCharacterMovement()->GravityScale = 0.f;
			Dino->GetCharacterMovement()->MaxWalkSpeed = 0.f; // stays put: the test reads its state, not its chase
		}
		return Dino;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetSocialTest, "GFGame.Social.Net", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetSocialTest::RunTest(const FString&)
{
	using namespace GFNetSocialTest;
	using C = FGFSocialCodes;
	// The banned client's own world logs the dropped connection as an error; that's the point of the ban.
	AddExpectedError(TEXT("BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 0);
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FRun> Run = MakeShared<FRun>();

	Do(Ctx, [this, Run]()
	{
		Run->Host = LocalPlayerState(ServerWorld())->PlayerKey;
		Run->C0 = Client(0)->PlayerKey;
		Run->C1 = Client(1)->PlayerKey;
		UGFSocialComponent* S = ServerSocial();
		if (!TestNotNull(TEXT("Social director (spawned by the game mode)"), S))
		{
			return;
		}
		S->Tuning.EmoteCooldownSeconds = 0.3f;
		S->Tuning.PowerCooldownSeconds = 0.3f;
		// Safe territory: one circle at the hub centre (the territory system / giant event provide it in play).
		FGFSafeRegion R;
		R.Id = TEXT("Home");
		R.BaseRadius = 1500.f;
		UGFSafeZoneSubsystem::Get(ServerWorld())->SetOverride(MakeShared<FGFSafeCircles>(TArray<FGFSafeRegion>{ R }));
		AGFGameState* GS = GameState(ServerWorld());
		TestEqual(TEXT("Host takes F2"), GS->ServerClaimFaction(LocalPlayerState(ServerWorld()), F2), FName(TEXT("Ok")));
		TestEqual(TEXT("C0 takes F1 first (leader)"), GS->ServerClaimFaction(OnServer(Run->C0), F1), FName(TEXT("Ok")));
		Place(Run->Host, 0.f, 0.f);
		Place(Run->C0, 300.f, 0.f);
		Place(Run->C1, -300.f, 0.f);
		// C1 joins F1 over the net with the Player verb.
		FGFModeRequest Req = FGFModeRequest::Make(TEXT("Player"), TEXT("ClaimFaction"));
		Req.Names.Add(F1);
		Run->Id = Client(1)->RequestMode(Req);
	});
	WaitUntil(Ctx, TEXT("factions and leaders on both clients"), 15.f, [Run]()
	{
		for (int32 i = 0; i < 2; ++i)
		{
			AGFGameState* GS = GameState(ClientWorlds()[i]);
			const AGFPlayerState* H = PlayerStateByKey(ClientWorlds()[i], Run->Host);
			const AGFPlayerState* A = PlayerStateByKey(ClientWorlds()[i], Run->C0);
			const AGFPlayerState* B = PlayerStateByKey(ClientWorlds()[i], Run->C1);
			if (!ClientSocial(i) || !GS || !H || !A || !B || H->FactionId != F2 || A->FactionId != F1 || B->FactionId != F1 || GS->FactionLeaderOf(F1) != Run->C0)
			{
				return false;
			}
		}
		return true;
	});

	// --- Powers: cross-faction only, inside safety ---
	Do(Ctx, [Run]()
	{
		Run->Id = Ask(1, TEXT("Grow"), Run->Host);   // F1 -> F2
		Run->Id2 = Ask(1, TEXT("Shrink"), Run->C0);  // F1 -> F1
	});
	WaitUntil(Ctx, TEXT("power answers"), 10.f, [Run]() { return Result(1, Run->Id) && Result(1, Run->Id2); });
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Grow on another faction: ok"), Result(1, Run->Id)->bOk);
		TestEqual(TEXT("Shrink on own faction: refused"), Result(1, Run->Id2)->Code, C::SameFaction);
		TestTrue(TEXT("Server: host at 1.25x"), FMath::IsNearlyEqual(ServerSocial()->ScaleOf(Run->Host), 1.25f));
	});
	WaitUntil(Ctx, TEXT("both clients see the host at 1.25x"), 10.f, [Run]()
	{
		return FMath::IsNearlyEqual(ScaleSeen(ClientWorlds()[0], Run->Host), 1.25f, 0.01f) && FMath::IsNearlyEqual(ScaleSeen(ClientWorlds()[1], Run->Host), 1.25f, 0.01f)
			&& FMath::IsNearlyEqual(ScaleSeen(ClientWorlds()[1], Run->C0), 1.f, 0.01f);
	});
	// Leaving safety reverts it.
	Do(Ctx, [Run]() { Place(Run->Host, 2500.f, 0.f); });
	WaitUntil(Ctx, TEXT("scale reverts outside safety (server and clients)"), 10.f, [Run]()
	{
		return ServerSocial()->ScaleOf(Run->Host) == 1.f && FMath::IsNearlyEqual(ScaleSeen(ServerWorld(), Run->Host), 1.f, 0.01f)
			&& FMath::IsNearlyEqual(ScaleSeen(ClientWorlds()[0], Run->Host), 1.f, 0.01f) && FMath::IsNearlyEqual(ScaleSeen(ClientWorlds()[1], Run->Host), 1.f, 0.01f);
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Outside safety: no powers"), ServerSocial()->ServerPower(LocalPlayerState(ServerWorld()), OnServer(Run->C1), EGFSocialPower::Grow), C::NotSafe);
		Place(Run->Host, 0.f, 0.f);
		Place(Run->C1, -1000.f, 0.f);
	});
	Wait(Ctx, 0.5f);
	// Grab: the host (F2) tethers C1 (F1) and pulls them in.
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Grab"), ServerSocial()->ServerPower(LocalPlayerState(ServerWorld()), OnServer(Run->C1), EGFSocialPower::Grab), C::Ok);
	});
	WaitUntil(Ctx, TEXT("C1 pulled to the host and the tether replicated"), 10.f, [Run]()
	{
		const APawn* H = OnServer(Run->Host)->GetPawn();
		const APawn* T = OnServer(Run->C1)->GetPawn();
		const UGFSocialComponent* CS = ClientSocial(1);
		return H && T && FVector::Dist2D(H->GetActorLocation(), T->GetActorLocation()) < 500.f && CS && CS->Tethers.Num() == 1;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Drop"), ServerSocial()->ServerDrop(LocalPlayerState(ServerWorld())), C::Ok);
		TestEqual(TEXT("Nothing held"), ServerSocial()->Tethers.Num(), 0);
		Place(Run->C1, -300.f, 0.f);
	});
	Wait(Ctx, 0.5f);

	// --- Jail: the host jails C1 (F1); C1's leader C0 releases them ---
	Do(Ctx, [Run]() { Run->Id2 = Ask(1, TEXT("Jail"), Run->Host); }); // a member can't jail (and nobody jails the host)
	WaitUntil(Ctx, TEXT("C1's jail attempt answered"), 10.f, [Run]() { return Result(1, Run->Id2) != nullptr; });
	Do(Ctx, [this, Run]()
	{
		Run->C1Before = OnServer(Run->C1)->GetPawn()->GetActorLocation();
		TestEqual(TEXT("Host jails C1"), ServerSocial()->ServerJail(LocalPlayerState(ServerWorld()), OnServer(Run->C1)), C::Ok);
	});
	WaitUntil(Ctx, TEXT("C1 jailed: underground, drip on C1's client"), 15.f, [Run]()
	{
		const APawn* P = OnServer(Run->C1)->GetPawn();
		return Result(1, Run->Id2) && P && P->GetActorLocation().Z < -4000.f && ClientSocial(1) && ClientSocial(1)->bDripPlaying;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Jailer recorded"), ServerSocial()->FindJail(Run->C1) ? ServerSocial()->FindJail(Run->C1)->JailerKey : FString(), Run->Host);
		TestEqual(TEXT("Member can't jail the host"), Result(1, Run->Id2)->Code, C::TargetIsHost);
		TestTrue(TEXT("Voice muted (stub)"), ServerSocial()->Voice->IsVoiceMuted(Run->C1));
		TestFalse(TEXT("No drip for anyone else"), ClientSocial(0)->bDripPlaying);
		TestNotNull(TEXT("Cell"), ServerSocial()->Cell.Get());
		Run->ChatResultsBefore = Client(1)->ChatResults.Num();
		Client(1)->RequestChat(TEXT("let me out"));
		Run->Id2 = Ask(1, TEXT("Emote"), FString(), TEXT("Hello"));
	});
	WaitUntil(Ctx, TEXT("jailed chat answered"), 10.f, [Run]() { return Client(1)->ChatResults.Num() > Run->ChatResultsBefore && Result(1, Run->Id2); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Chat muted"), Client(1)->ChatResults.Last(), C::Muted);
		TestEqual(TEXT("No emotes in jail"), Result(1, Run->Id2)->Code, C::Jailed);
		TestFalse(TEXT("Nothing in the chat log from C1"), GameState(ServerWorld())->ChatLog.ContainsByPredicate([Run](const FGFChatMessage& M) { return M.SenderKey == Run->C1; }));
		Run->Id = Ask(0, TEXT("Unjail"), Run->C1); // C0 didn't jail them, but leads their faction
	});
	WaitUntil(Ctx, TEXT("C1 released by their leader: back, drip off"), 15.f, [Run]()
	{
		const APawn* P = OnServer(Run->C1)->GetPawn();
		return Result(0, Run->Id) && P && FVector::Dist(P->GetActorLocation(), Run->C1Before) < 300.f && ClientSocial(1) && !ClientSocial(1)->bDripPlaying;
	});
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Leader releases a member the host jailed"), Result(0, Run->Id)->bOk);
		TestFalse(TEXT("Voice back"), ServerSocial()->Voice->IsVoiceMuted(Run->C1));
		Run->ChatResultsBefore = Client(1)->ChatResults.Num();
		Client(1)->RequestChat(TEXT("free"));
	});
	WaitUntil(Ctx, TEXT("chat after release"), 10.f, [Run]()
	{
		const AGFGameState* GS = GameState(ClientWorlds()[0]);
		return Client(1)->ChatResults.Num() > Run->ChatResultsBefore && GS && GS->ChatLog.ContainsByPredicate([Run](const FGFChatMessage& M) { return M.SenderKey == Run->C1 && M.Text == TEXT("free"); });
	});

	// --- Emotes: replicate to everyone; quiet inside safety ---
	Do(Ctx, [Run]()
	{
		Run->SeenBefore = ClientSocial(1)->EmotesSeen;
		Run->NoisesBefore = ServerSocial()->NoisesRaised;
		Run->Id = Ask(0, TEXT("Emote"), FString(), TEXT("Hello"));
	});
	WaitUntil(Ctx, TEXT("C1 sees C0's Hello"), 10.f, [Run]()
	{
		const UGFSocialComponent* S = ClientSocial(1);
		return S && S->EmotesSeen > Run->SeenBefore && S->LastEmoteSeen.Key == Run->C0 && S->LastEmoteSeen.EmoteId == FName(TEXT("Hello"));
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Hello waves"), ClientSocial(1)->LastEmoteSeen.AnimKey, FName(TEXT("Player.Emote_Wave")));
		TestFalse(TEXT("Inside safety: no noise"), ClientSocial(1)->LastEmoteSeen.bNoise);
		TestEqual(TEXT("No noise raised"), ServerSocial()->NoisesRaised, Run->NoisesBefore);
		// The newbie trap: out in the open world, a raptor 1500 away.
		Place(Run->C0, 2500.f, 0.f);
		Run->Raptor = SpawnRaptor(2500.f, 1500.f);
		Run->Raptor2 = SpawnRaptor(2500.f, -1500.f);
	});
	WaitUntil(Ctx, TEXT("raptors on client 0"), 10.f, [Run]()
	{
		return Run->Raptor.IsValid() && Run->Raptor2.IsValid() && Run->Raptor->Taming->GetTameState() != EGFTameState::Hostile;
	});
	Wait(Ctx, 0.5f);
	Do(Ctx, [Run]() { Run->Id = Ask(0, TEXT("Emote"), FString(), TEXT("Heart")); });
	WaitUntil(Ctx, TEXT("the raptor comes for C0 (seen on C0's client)"), 10.f, [Run]()
	{
		if (!Result(0, Run->Id) || !Run->Raptor.IsValid())
		{
			return false;
		}
		for (TActorIterator<AGFDino> It(ClientWorlds()[0]); It; ++It)
		{
			if (It->Taming && It->Taming->GetTameState() == EGFTameState::Hostile && It->Taming->GetTargetKey() == Run->C0)
			{
				return true;
			}
		}
		return false;
	});
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Emote outside ok"), Result(0, Run->Id)->bOk);
		TestEqual(TEXT("One noise"), ServerSocial()->NoisesRaised, Run->NoisesBefore + 1);
		TestEqual(TEXT("At C0"), Run->Raptor->Taming->GetTargetKey(), Run->C0);
		TestTrue(TEXT("Heard it"), Run->Raptor->Taming->NoisesHeard == 1 && Run->Raptor2->Taming->NoisesHeard == 1);
		// Rail mode: same spot, no noise. Calm raptor 2 down first so it could hear again.
		Run->Raptor2->Destroy();
		Run->Raptor2 = SpawnRaptor(2500.f, -1500.f);
		ServerSocial()->ServerSetPlayerMode(Run->C0, TEXT("Moba"));
		Run->NoisesBefore = ServerSocial()->NoisesRaised;
	});
	Wait(Ctx, 0.5f);
	Do(Ctx, [Run]() { Run->Id = Ask(0, TEXT("Emote"), FString(), TEXT("ThumbsUp")); });
	WaitUntil(Ctx, TEXT("rail-mode emote answered"), 10.f, [Run]() { return Result(0, Run->Id) != nullptr; });
	Wait(Ctx, 0.5f);
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Emote in the MOBA ok"), Result(0, Run->Id)->bOk);
		TestEqual(TEXT("MOBA: no noise"), ServerSocial()->NoisesRaised, Run->NoisesBefore);
		TestTrue(TEXT("Raptor 2 stays calm"), Run->Raptor2.IsValid() && Run->Raptor2->Taming->NoisesHeard == 0 && Run->Raptor2->Taming->GetTameState() != EGFTameState::Hostile);
		ServerSocial()->ServerSetPlayerMode(Run->C0, UGFSocialComponent::DefaultPlayerMode);
		Run->Id = Ask(0, TEXT("Kick"), Run->C1);
	});

	// --- Kick / ban: host only ---
	WaitUntil(Ctx, TEXT("non-host kick answered"), 10.f, [Run]() { return Result(0, Run->Id) != nullptr; });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("A leader can't kick"), Result(0, Run->Id)->Code, C::NotAllowed);
		TestEqual(TEXT("Host bans C1"), ServerSocial()->ServerBan(LocalPlayerState(ServerWorld()), OnServer(Run->C1)), C::Ok);
		FString Why;
		TestTrue(TEXT("C1 refused at join"), GameState(ServerWorld())->IsJoinRefused(Run->C1, Why));
		TestEqual(TEXT("A kick went out"), ServerSocial()->KicksDone, 1);
	});
	WaitUntil(Ctx, TEXT("C1 gone from the server"), 15.f, [Run]()
	{
		const AGFPlayerState* PS = OnServer(Run->C1);
		return !PS || PS->bLoggedOut || GameState(ServerWorld())->PlayerArray.Num() == 2;
	});
	Do(Ctx, [this, Run]()
	{
		TMap<FName, FGFModeBlob> Saved = GameState(ServerWorld())->GatherModeStates();
		TestTrue(TEXT("Ban in the world save"), Saved.Contains(UGFSocialComponent::ModeName));
	});
	Stop(Ctx);
	return true;
}

#endif
