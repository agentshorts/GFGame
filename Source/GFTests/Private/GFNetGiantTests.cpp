// Big Grumble over the network (host + 2 clients): it goes for the newest player, everybody sees it (name and size
// from data) and its all-chat lines, it stays out of safe territory, a client who attacks it gets eaten (belly cam
// on that client only), it leaves at once with a Snacked line, nobody else can be eaten, the faint hook respawns
// the eaten player at the circle edge after the belly cam, and the ledger and pawns' damage are never touched.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFModeTypes.h"
#include "GFGiantDirector.h"
#include "GFGiantEventComponent.h"
#include "GFGiantDino.h"
#include "GFGiantBellyCam.h"
#include "GFFaintHandler.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetGiantTest
{
	struct FRun
	{
		FString Host, C0, C1;
		int32 LedgerBefore = 0;
		int32 HitId = 0;
		int32 LateHitId = 0;
		double EatenAt = 0.0;
	};

	AGFPlayerState* Client(int32 i) { return LocalPlayerState(ClientWorlds()[i]); }
	AGFPlayerState* OnServer(const FString& Key) { return PlayerStateByKey(ServerWorld(), Key); }
	UGFGiantEventComponent* ServerDirector() { return AGFGiantDirector::GetEvent(ServerWorld(), false); }
	UGFGiantEventComponent* ClientDirector(int32 i) { return AGFGiantDirector::GetEvent(ClientWorlds()[i], false); }

	void Place(const FString& Key, float X, float Y)
	{
		AGFPlayerState* PS = OnServer(Key);
		if (APawn* Pawn = PS ? PS->GetPawn() : nullptr)
		{
			Pawn->TeleportTo(FVector(X, Y, Pawn->GetActorLocation().Z), Pawn->GetActorRotation(), false, true);
		}
	}

	int32 BellyCams(UWorld* World, bool* bShowing = nullptr)
	{
		int32 N = 0;
		for (TActorIterator<AGFGiantBellyCam> It(World); It; ++It)
		{
			++N;
			if (bShowing)
			{
				*bShowing |= It->IsShowingLocally();
			}
		}
		return N;
	}

	void SetPlay(UGFGiantEventComponent* D, const FString& Key, double Seconds)
	{
		FGFGiantSave& S = D->EditSaveForTests();
		const int32 i = S.Touch(Key, D->NowUtcTicks());
		S.PlayerSeconds[i] = Seconds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetGiantTest, "GFGame.Giant.Net", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetGiantTest::RunTest(const FString&)
{
	using namespace GFNetGiantTest;
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FRun> Run = MakeShared<FRun>();

	Do(Ctx, [this, Run]()
	{
		Run->C0 = Client(0)->PlayerKey;
		Run->C1 = Client(1)->PlayerKey;
		Run->Host = LocalPlayerState(ServerWorld())->PlayerKey;
		UGFGiantEventComponent* D = AGFGiantDirector::GetEvent(ServerWorld());
		if (!TestNotNull(TEXT("Giant director"), D))
		{
			return;
		}
		D->bAutoTrigger = false;
		D->ForcedSeed = 31;
		D->Tuning.CircleBaseRadius = 800.f;
		D->Tuning.CircleRadiusPerExtraPlayer = 200.f; // 3 players: 1200
		D->Tuning.EventSeconds = 600.f;
		D->Tuning.bEatIdleTarget = false;            // everyone stands still in this test; eating comes from the attack
		D->Tuning.AttackRange = 100000.f;
		D->Tuning.BellyCamSeconds = 3.f;
		Place(Run->Host, 0.f, 0.f);       // home
		Place(Run->C0, 2200.f, 0.f);      // outside, veteran-ish
		Place(Run->C1, 0.f, -2300.f);     // outside, the newbie
		Run->LedgerBefore = GameState(ServerWorld())->Ledger.AppliedOrder.Num();
	});
	WaitUntil(Ctx, TEXT("director on both clients"), 15.f, []() { return ClientDirector(0) && ClientDirector(1); });
	Wait(Ctx, 0.5f);

	Do(Ctx, [this, Run]()
	{
		UGFGiantEventComponent* D = ServerDirector();
		SetPlay(D, Run->Host, 9000.0);
		SetPlay(D, Run->C0, 5000.0);
		SetPlay(D, Run->C1, 10.0);
		TestTrue(TEXT("Event starts"), D->ServerStartEvent());
		TestEqual(TEXT("Goes for the newest player"), D->GetSim().TargetKey, Run->C1);
		// {Target} gets the target's real player name (the sim's view of them comes from their player state).
		const FString RealName = OnServer(Run->C1)->GetPlayerName();
		const FGFGiantPlayerView* View = nullptr;
		const TArray<FGFGiantPlayerView> Views = D->GatherPlayers();
		for (const FGFGiantPlayerView& V : Views)
		{
			View = V.Key == Run->C1 ? &V : View;
		}
		TestFalse(TEXT("Target has a player name"), RealName.IsEmpty());
		TestTrue(TEXT("Sim sees the target's player name"), View && View->Name == RealName);
		TestEqual(TEXT("{Target} filled with it"), FGFGiantRules::FillTarget(TEXT("Where you going, {Target}? Huh? Where you going?"), View ? View->Name : FString()),
			FString::Printf(TEXT("Where you going, %s? Huh? Where you going?"), *RealName));
		TestTrue(TEXT("Default safe region for 3 players"), D->SafeRegions.Num() == 1 && FMath::IsNearlyEqual(D->SafeRegions[0].Radius(), 1200.f));
		TestFalse(TEXT("Newest player is outside safety"), D->GetSafeZone().IsInsideSafe(OnServer(Run->C1)->GetPawn()->GetActorLocation()));
	});
	WaitUntil(Ctx, TEXT("both clients see Big Grumble and its first line"), 15.f, []()
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const UGFGiantEventComponent* D = ClientDirector(i);
			if (!D || !D->bEventActive || !D->Giant || D->Giant->DisplayName != TEXT("Big Grumble") || D->Giant->MeshScale != 3.5f || D->Chat.Num() == 0)
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [this, Run]()
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const UGFGiantEventComponent* D = ClientDirector(i);
			TestTrue(TEXT("Opens with a Spotted line"), D->Chat[0].Phase == EGFGiantLinePhase::Spotted);
			TestEqual(TEXT("Said by Big Grumble"), D->Chat[0].Speaker, FString(TEXT("Big Grumble")));
			TestEqual(TEXT("In all chat"), D->Chat[0].Channel, FName(TEXT("All")));
			TestEqual(TEXT("Clients see the target"), D->Giant->TargetKey, Run->C1);
			TestEqual(TEXT("Clients see the safe regions"), D->SafeRegions.Num(), 1);
		}
	});

	// Never inside the circle (server, and as the clients see it).
	Repeat(Ctx, 30, 0.1f, [this](int32)
	{
		const UGFGiantEventComponent* D = ServerDirector();
		if (!D || !D->Giant)
		{
			return;
		}
		const float Margin = D->Tuning.GiantRadius;
		if (!FGFSafeZone::IsClear(D->GetSafeZone(), D->Giant->GetActorLocation(), Margin))
		{
			AddError(FString::Printf(TEXT("Giant inside safety on the server (%.0f cm out)"), FGFSafeZone::DistanceOutside(D->GetSafeZone(), D->Giant->GetActorLocation())));
		}
		for (int32 i = 0; i < 2; ++i)
		{
			const UGFGiantEventComponent* CD = ClientDirector(i);
			if (CD && CD->Giant && CD->SafeRegions.Num() > 0
				&& (CD->GetSafeZone().IsInsideSafe(CD->Giant->GetActorLocation()) || FGFSafeZone::DistanceOutside(CD->GetSafeZone(), CD->Giant->GetActorLocation()) < Margin - 50.f))
			{
				AddError(TEXT("Giant inside safety on a client"));
			}
		}
	});

	// Client 0 attacks: eaten, the giant leaves at once.
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("No damage from the giant so far"), ServerDirector()->DamageFromGiant, 0);
		Run->HitId = Client(0)->RequestMode(FGFModeRequest::Make(UGFGiantEventComponent::ModeName, TEXT("Hit")));
	});
	WaitUntil(Ctx, TEXT("hit answered"), 10.f, [Run]() { return Client(0)->ModeResults.Contains(Run->HitId); });
	Do(Ctx, [this, Run]()
	{
		Run->EatenAt = FPlatformTime::Seconds();
		const FGFModeResult& R = Client(0)->ModeResults[Run->HitId];
		TestTrue(TEXT("Hit accepted"), R.bOk);
		TestTrue(TEXT("Attacker eaten"), R.Ints.Num() == 1 && R.Ints[0] == 1);
		const UGFGiantEventComponent* D = ServerDirector();
		TestTrue(TEXT("One player eaten: the attacker"), D->EatenKeys.Num() == 1 && D->EatenKeys[0] == Run->C0);
		TestFalse(TEXT("Event over at once"), D->bEventActive);
		TestNull(TEXT("Giant gone at once"), D->Giant.Get());
		TestEqual(TEXT("Belly cam spawned"), D->BellyCamsSpawned, 1);
		Run->LateHitId = Client(1)->RequestMode(FGFModeRequest::Make(UGFGiantEventComponent::ModeName, TEXT("Hit")));
	});
	WaitUntil(Ctx, TEXT("clients see it leave with a Snacked line"), 10.f, [Run]()
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const UGFGiantEventComponent* D = ClientDirector(i);
			if (!D || D->bEventActive || D->Giant || D->Chat.Num() == 0 || D->Chat.Last().Phase != EGFGiantLinePhase::Snacked || D->Chat.Last().TargetKey != Run->C0)
			{
				return false;
			}
		}
		return Client(1)->ModeResults.Contains(Run->LateHitId);
	});
	Do(Ctx, [this, Run]()
	{
		const FGFModeResult& R = Client(1)->ModeResults[Run->LateHitId];
		TestEqual(TEXT("Nobody else can be eaten"), R.Code, FName(TEXT("NoGiant")));
		TestEqual(TEXT("Still one eaten"), ServerDirector()->EatenKeys.Num(), 1);
	});
	WaitUntil(Ctx, TEXT("belly cam on the eaten player's machine"), 5.f, []()
	{
		bool bShowing = false;
		return BellyCams(ClientWorlds()[0], &bShowing) == 1 && bShowing;
	});
	Do(Ctx, [this]()
	{
		TestEqual(TEXT("Belly cam is owner-only (other client)"), BellyCams(ClientWorlds()[1]), 0);
		bool bHostShowing = false;
		BellyCams(ServerWorld(), &bHostShowing);
		TestFalse(TEXT("Host isn't looking through it"), bHostShowing);
	});
	WaitUntil(Ctx, TEXT("faint hook called after the belly cam"), 10.f, []()
	{
		const UGFGiantEventComponent* D = ServerDirector();
		return D && D->DefaultFaint && D->DefaultFaint->Calls.Num() > 0;
	});
	Do(Ctx, [this, Run]()
	{
		const UGFGiantEventComponent* D = ServerDirector();
		TestTrue(TEXT("Belly cam lasted ~3 s"), FPlatformTime::Seconds() - Run->EatenAt >= 2.5);
		TestEqual(TEXT("Faint hook called once"), D->DefaultFaint->Calls.Num(), 1);
		TestEqual(TEXT("For the eaten player"), D->DefaultFaint->Calls[0].Key, Run->C0);
		TestEqual(TEXT("Cause"), D->DefaultFaint->Calls[0].Cause, FName(TEXT("GiantSnack")));
		const APawn* Pawn = OnServer(Run->C0) ? OnServer(Run->C0)->GetPawn() : nullptr;
		if (TestNotNull(TEXT("Eaten player's pawn"), Pawn))
		{
			TestTrue(TEXT("Respawned inside safety"), D->GetSafeZone().IsInsideSafe(Pawn->GetActorLocation()));
			TestFalse(TEXT("Visible again"), Pawn->IsHidden());
		}
		TestEqual(TEXT("Ledger untouched"), GameState(ServerWorld())->Ledger.AppliedOrder.Num(), Run->LedgerBefore);
		TestEqual(TEXT("Never any damage"), D->DamageFromGiant, 0);
	});
	WaitUntil(Ctx, TEXT("belly cam gone on client 0"), 5.f, []() { return BellyCams(ClientWorlds()[0]) == 0; });
	Stop(Ctx);
	return true;
}

#endif
