// Hallow Night over the network (PLAN.md M7 Net.Depths): depths unlock in order, the server runs the 2.5D
// mover from client input and every machine sees the bodies, Z/X/C platforms are shared (anyone works them,
// one cooldown, same position on every machine from server time), a downed friend can be revived, fainting
// loses half the unbanked loot, checkpoints and Lift Home bank into the player's bag, finishing a depth opens the next,
// and a drop mid-run keeps the faint share.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFDepthsDirector.h"
#include "GFModeTypes.h"
#include "GFRewards.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetDepthsTest
{
	struct FDepthsRun
	{
		FString C1, C2, ZBy;
		TMap<FName, int32> Want;
	};

	static const FName Depths(TEXT("Depths"));

	AGFPlayerState* Client(int32 i) { return LocalPlayerState(ClientWorlds()[i]); }
	AGFPlayerState* OnServer(const FString& Key) { return PlayerStateByKey(ServerWorld(), Key); }
	AGFDepthsDirector* ServerDirector() { return AGFDepthsDirector::Get(ServerWorld(), false); }
	AGFDepthsDirector* ClientDirector(int32 i) { return AGFDepthsDirector::Get(ClientWorlds()[i], false); }

	void Send(FDepthsRun& Run, int32 ClientIndex, const TCHAR* Label, FName Verb, TFunction<void(FGFModeRequest&)> Fill = nullptr)
	{
		FGFModeRequest R = FGFModeRequest::Make(Depths, Verb);
		if (Fill)
		{
			Fill(R);
		}
		const int32 Id = Client(ClientIndex)->RequestMode(R);
		Run.Want.Add(FName(Label), Id + ClientIndex * 100000);
	}

	const FGFModeResult* Result(const FDepthsRun& Run, const TCHAR* Label)
	{
		const int32* Packed = Run.Want.Find(FName(Label));
		const AGFPlayerState* PS = Packed ? Client(*Packed / 100000) : nullptr;
		return PS ? PS->ModeResults.Find(*Packed % 100000) : nullptr;
	}

	bool HaveAll(const FDepthsRun& Run, std::initializer_list<const TCHAR*> Labels)
	{
		for (const TCHAR* L : Labels)
		{
			if (!Result(Run, L))
			{
				return false;
			}
		}
		return true;
	}

	FString Code(const FDepthsRun& Run, const TCHAR* Label)
	{
		const FGFModeResult* R = Result(Run, Label);
		return !R ? FString(TEXT("<none>")) : (R->bOk ? FString(TEXT("Ok")) : R->Code.ToString());
	}

	const FGFDepthsPlayerView* ClientView(int32 ClientIndex, const FString& Key)
	{
		const AGFDepthsDirector* D = ClientDirector(ClientIndex);
		return D ? D->FindPlayer(Key) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetDepthsTest, "GFGame.Net.Depths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetDepthsTest::RunTest(const FString&)
{
	using namespace GFNetDepthsTest;
	AddExpectedMessage(TEXT("Your connection to the host has been lost"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0, false);
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FDepthsRun> Run = MakeShared<FDepthsRun>();

	Do(Ctx, [this, Run]()
	{
		Run->C1 = Client(0)->PlayerKey;
		Run->C2 = Client(1)->PlayerKey;
		AGFGameState* GS = GameState(ServerWorld());
		for (const FString& K : { Run->C1, Run->C2 })
		{
			AGFPlayerState* P = OnServer(K);
			if (!TestNotNull(TEXT("Client player on the server"), P))
			{
				return;
			}
			const TArray<FGFItemStack> All = P->Inventory->GetInventory().Items;
			if (All.Num() > 0)
			{
				GS->ServerBurn(FGuid::NewGuid(), K, All, TEXT("Test")); // exact counts below
			}
		}
		AGFDepthsDirector* D = AGFDepthsDirector::Get(ServerWorld());
		if (TestNotNull(TEXT("Depths director"), D))
		{
			D->ForcedSeed = 777;
		}
	});
	WaitUntil(Ctx, TEXT("empty bags and the director on clients"), 15.f, []()
	{
		return Client(0) && Client(1) && Client(0)->Inventory->GetInventory().Items.Num() == 0 && Client(1)->Inventory->GetInventory().Items.Num() == 0 &&
			ClientDirector(0) && ClientDirector(1);
	});

	// ---- entering ----
	Do(Ctx, [Run]()
	{
		Send(*Run, 0, TEXT("Deep"), TEXT("Enter"), [](FGFModeRequest& R) { R.Ints = { 2 }; });
		Send(*Run, 0, TEXT("Enter1"), TEXT("Enter"), [](FGFModeRequest& R) { R.Ints = { 1 }; });
		Send(*Run, 0, TEXT("Twice"), TEXT("Enter"), [](FGFModeRequest& R) { R.Ints = { 1 }; });
		Send(*Run, 1, TEXT("NotIn"), TEXT("Heal"));
		Send(*Run, 1, TEXT("Enter2"), TEXT("Enter"), [](FGFModeRequest& R) { R.Ints = { 1 }; });
	});
	WaitUntil(Ctx, TEXT("enter answers"), 15.f, [Run]() { return HaveAll(*Run, { TEXT("Deep"), TEXT("Enter1"), TEXT("Twice"), TEXT("NotIn"), TEXT("Enter2") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Depth 2 is locked until depth 1 is done"), Code(*Run, TEXT("Deep")), FString(TEXT("Locked")));
		TestEqual(TEXT("Enter depth 1"), Code(*Run, TEXT("Enter1")), FString(TEXT("Ok")));
		TestEqual(TEXT("Already in"), Code(*Run, TEXT("Twice")), FString(TEXT("AlreadyIn")));
		TestEqual(TEXT("Not in a run yet"), Code(*Run, TEXT("NotIn")), FString(TEXT("NotInRun")));
		TestEqual(TEXT("Friend joins"), Code(*Run, TEXT("Enter2")), FString(TEXT("Ok")));
		// Client 1 runs right and jumps; the server moves them and everyone sees it.
		Send(*Run, 0, TEXT("Run"), TEXT("Input"), [](FGFModeRequest& R) { R.Floats = { 1.f }; R.Ints = { 0, 0, 0 }; });
	});
	WaitUntil(Ctx, TEXT("both clients see the player run"), 10.f, [Run]()
	{
		const FGFDepthsPlayerView* A = ClientView(0, Run->C1);
		const FGFDepthsPlayerView* B = ClientView(1, Run->C1);
		return A && B && A->Body.Pos.X > 300.f && B->Body.Pos.X > 300.f;
	});
	Do(Ctx, [this, Run]()
	{
		const AGFDepthsDirector* D = ServerDirector();
		TestEqual(TEXT("A run at depth 1 on the server"), D ? D->Depth : -1, 1);
		TestTrue(TEXT("Clients see two players in the run"), ClientDirector(0)->Players.Num() == 2 && ClientDirector(1)->Players.Num() == 2);
		TestTrue(TEXT("Shades are tougher with a friend"), D && D->Enemies.Num() > 0 && D->Enemies[0].MaxHP == FGFDepthsRules::EnemyHP(2, 1, 2));
		Send(*Run, 0, TEXT("Hop"), TEXT("Input"), [](FGFModeRequest& R) { R.Floats = { 0.f }; R.Ints = { 1, 1, 0 }; });
	});
	WaitUntil(Ctx, TEXT("both clients see the player jump"), 10.f, [Run]()
	{
		const FGFDepthsPlayerView* A = ClientView(0, Run->C1);
		const FGFDepthsPlayerView* B = ClientView(1, Run->C1);
		return A && B && A->Body.Pos.Y > 120.f && B->Body.Pos.Y > 120.f;
	});

	// ---- shared platforms ----
	Do(Ctx, [Run]()
	{
		Send(*Run, 0, TEXT("Release"), TEXT("Input"), [](FGFModeRequest& R) { R.Floats = { 0.f }; R.Ints = { 0, 0, 0 }; });
		Send(*Run, 1, TEXT("Z"), TEXT("Switch"), [](FGFModeRequest& R) { R.Names = { TEXT("Z") }; });
		Send(*Run, 0, TEXT("ZAgain"), TEXT("Switch"), [](FGFModeRequest& R) { R.Names = { TEXT("Z") }; });
		Send(*Run, 0, TEXT("BadKey"), TEXT("Switch"), [](FGFModeRequest& R) { R.Names = { TEXT("Q") }; });
	});
	WaitUntil(Ctx, TEXT("switch answers"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Release"), TEXT("Z"), TEXT("ZAgain"), TEXT("BadKey") }); });
	Do(Ctx, [this, Run]()
	{
		// Both press Z in the same instant: whoever reaches the server first works it, the other hits the shared cooldown.
		const FString A = Code(*Run, TEXT("Z")), B = Code(*Run, TEXT("ZAgain"));
		TestTrue(TEXT("Either of them can work Z; one shared cooldown"), (A == TEXT("Ok") && B == TEXT("Cooldown")) || (A == TEXT("Cooldown") && B == TEXT("Ok")));
		Run->ZBy = A == TEXT("Ok") ? Run->C2 : Run->C1;
		TestEqual(TEXT("Only Z/X/C"), Code(*Run, TEXT("BadKey")), FString(TEXT("BadKey")));
	});
	WaitUntil(Ctx, TEXT("the Z platform reaches B on every machine"), 10.f, []()
	{
		const FGFDepthsPlatform& Z = FGFDepthsLevel::Get(1).Platforms[0];
		for (int32 i = 0; i < 2; ++i)
		{
			const AGFDepthsDirector* D = ClientDirector(i);
			if (!D || !D->PlatformPosition(0).Equals(Z.B, 1.f))
			{
				return false;
			}
		}
		return ServerDirector() && ServerDirector()->PlatformPosition(0).Equals(Z.B, 1.f);
	});
	Do(Ctx, [this, Run]()
	{
		const FGFDepthsSwitch* S = FGFDepthsPlatforms::Find(ClientDirector(0)->Switches, TEXT("Z"));
		TestTrue(TEXT("Client 1 sees who worked it"), S && S->bAtB && S->LastBy == Run->ZBy);
		// ---- downed, revive, heals ----
		AGFDepthsDirector* D = ServerDirector();
		D->TeleportForTests(Run->C1, FVector2D(100.f, 60.f));
		D->TeleportForTests(Run->C2, FVector2D(0.f, 60.f));
		D->EditPlayerForTests(Run->C1)->Run.Unbanked = { FGFItemStack(TEXT("Moonstone"), 5), FGFItemStack(TEXT("Stone"), 3) };
		D->HitForTests(Run->C1, 5);
		TestTrue(TEXT("Down (a friend is up)"), D->FindPlayer(Run->C1)->Run.bDowned);
		Send(*Run, 0, TEXT("DownHeal"), TEXT("Heal"));
	});
	WaitUntil(Ctx, TEXT("no heal while down"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("DownHeal") }); });
	Do(Ctx, [Run]() { Send(*Run, 1, TEXT("Revive"), TEXT("Revive")); });
	WaitUntil(Ctx, TEXT("revive"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Revive") }); });
	Do(Ctx, [Run]()
	{
		Send(*Run, 0, TEXT("Heal1"), TEXT("Heal"));
		Send(*Run, 0, TEXT("Heal2"), TEXT("Heal"));
		Send(*Run, 0, TEXT("Full"), TEXT("Heal"));
	});
	WaitUntil(Ctx, TEXT("heals"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Heal1"), TEXT("Heal2"), TEXT("Full") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("No heals while down"), Code(*Run, TEXT("DownHeal")), FString(TEXT("CantHeal")));
		TestEqual(TEXT("Friend revives the player"), Code(*Run, TEXT("Revive")), FString(TEXT("Ok")));
		TestEqual(TEXT("Heal 1"), Code(*Run, TEXT("Heal1")), FString(TEXT("Ok")));
		TestEqual(TEXT("Heal 2"), Code(*Run, TEXT("Heal2")), FString(TEXT("Ok")));
		TestEqual(TEXT("Full HP"), Code(*Run, TEXT("Full")), FString(TEXT("CantHeal")));
		AGFDepthsDirector* D = ServerDirector();
		const FGFDepthsPlayerView* P = D->FindPlayer(Run->C1);
		TestTrue(TEXT("Revived at 2, healed to 5, one heal left"), P && P->Run.HP == 5 && P->Run.Heals == 1);
		// Down again with nobody coming: the timer runs out and the player faints.
		D->TeleportForTests(Run->C1, FVector2D(100.f, 60.f));
		D->HitForTests(Run->C1, 5);
		D->EditPlayerForTests(Run->C1)->Run.DownedLeft = 0.05f;
	});
	WaitUntil(Ctx, TEXT("the player faints"), 10.f, [Run]()
	{
		const FGFDepthsPlayerView* P = ClientView(0, Run->C1);
		return P && P->Run.Deaths == 1 && !P->Run.bDowned;
	});
	Do(Ctx, [this, Run]()
	{
		const FGFDepthsPlayerView* P = ClientView(0, Run->C1);
		TestEqual(TEXT("Fainting loses half: Moonstone 5 -> 3"), FGFRewards::CountOf(P->Run.Unbanked, TEXT("Moonstone")), 3);
		TestEqual(TEXT("Stone 3 -> 2"), FGFRewards::CountOf(P->Run.Unbanked, TEXT("Stone")), 2);
		TestTrue(TEXT("Full HP and heals again"), P->Run.HP == 5 && P->Run.Heals == 3);
		TestEqual(TEXT("Nothing reached the player's bag yet"), Client(0)->Inventory->GetInventory().Items.Num(), 0);
		ServerDirector()->TeleportForTests(Run->C1, FGFDepthsLevel::Get(1).Checkpoints[0]);
	});
	WaitUntil(Ctx, TEXT("checkpoint banks into the player's bag"), 10.f, [Run]()
	{
		return Client(0)->Inventory->GetCount(TEXT("Moonstone")) == 3 && Client(0)->Inventory->GetCount(TEXT("Stone")) == 2;
	});
	Do(Ctx, [this, Run]()
	{
		AGFDepthsDirector* D = ServerDirector();
		TestEqual(TEXT("Checkpoint 1 reached"), D->FindPlayer(Run->C1)->Run.Checkpoint, 0);
		D->EditPlayerForTests(Run->C1)->Run.Unbanked = { FGFItemStack(TEXT("Starglass"), 4) };
		D->TeleportForTests(Run->C1, FVector2D(2200.f, 60.f));
		Send(*Run, 0, TEXT("NoLift"), TEXT("LiftHome"));
	});
	WaitUntil(Ctx, TEXT("no lift here"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("NoLift") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Lift Home only at a lift"), Code(*Run, TEXT("NoLift")), FString(TEXT("NotAtLift")));
		ServerDirector()->TeleportForTests(Run->C1, FGFDepthsLevel::Get(1).Checkpoints[0]);
		Send(*Run, 0, TEXT("Lift"), TEXT("LiftHome"));
		// The player's friend finishes the depth.
		ServerDirector()->TeleportForTests(Run->C2, FGFDepthsLevel::Get(1).Exit);
	});
	WaitUntil(Ctx, TEXT("lift home and the friend at the exit"), 10.f, [Run]()
	{
		const AGFDepthsDirector* D = ServerDirector();
		const FGFDepthsPlayerView* P2 = D ? D->FindPlayer(Run->C2) : nullptr;
		return HaveAll(*Run, { TEXT("Lift") }) && P2 && P2->Run.bReachedExit;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Lift Home"), Code(*Run, TEXT("Lift")), FString(TEXT("Ok")));
		const FGFModeResult* R = Result(*Run, TEXT("Lift"));
		TestTrue(TEXT("Lift Home banks everything"), R && FGFRewards::CountOf(R->Stacks, TEXT("Starglass")) == 4);
		TestEqual(TEXT("Left without the exit: depth 1 not finished"), OnServer(Run->C1)->Progress.DepthsReached, 0);
		TestEqual(TEXT("Friend still in the run"), ServerDirector()->Players.Num(), 1);
		Run->Want.Remove(TEXT("Enter2"));
		Send(*Run, 1, TEXT("Exit"), TEXT("LiftHome"));
	});
	WaitUntil(Ctx, TEXT("friend lifts home from the exit"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Exit") }) && Client(0)->Inventory->GetCount(TEXT("Starglass")) == 4; });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Exit lift"), Code(*Run, TEXT("Exit")), FString(TEXT("Ok")));
		const FGFModeResult* R = Result(*Run, TEXT("Exit"));
		TestTrue(TEXT("The exit drops tier-4 minerals"), R && FGFRewards::CountOf(R->Stacks, TEXT("Moonstone")) > 0);
		TestEqual(TEXT("Depth 1 finished"), OnServer(Run->C2)->Progress.DepthsReached, 1);
		TestEqual(TEXT("Run over"), ServerDirector()->Depth, 0);
		Send(*Run, 1, TEXT("Deeper"), TEXT("Enter"), [](FGFModeRequest& R) { R.Ints = { 2 }; });
	});
	WaitUntil(Ctx, TEXT("depth 2 opens"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Deeper") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Depth 2 opens after depth 1"), Code(*Run, TEXT("Deeper")), FString(TEXT("Ok")));
		TestEqual(TEXT("Depth 2 run"), ServerDirector()->Depth, 2);
		// Client 2 drops mid-run with 3 Gloomsilk unbanked: a faint's share (2) comes home.
		ServerDirector()->EditPlayerForTests(Run->C2)->Run.Unbanked = { FGFItemStack(TEXT("Gloomsilk"), 3) };
		AGFPlayerState* P2 = OnServer(Run->C2);
		APlayerController* PC = P2 ? P2->GetPlayerController() : nullptr;
		AGameModeBase* GM = ServerWorld()->GetAuthGameMode();
		if (TestNotNull(TEXT("Client 2 controller"), PC) && GM && GM->GameSession)
		{
			GM->GameSession->KickPlayer(PC, FText::FromString(TEXT("GF test: drop mid-run")));
		}
	});
	WaitUntil(Ctx, TEXT("dropped client is in the guest records"), 15.f, [Run]()
	{
		const AGFGameState* GS = GameState(ServerWorld());
		const FGFGuestRecord* Rec = GS ? GS->GuestRecords.Find(Run->C2) : nullptr;
		return Rec && Rec->bHasCharacter && !PlayerStateByKey(ServerWorld(), Run->C2);
	});
	Do(Ctx, [this, Run]()
	{
		const FGFGuestRecord* Rec = GameState(ServerWorld())->GuestRecords.Find(Run->C2);
		TestEqual(TEXT("Dropped mid-run: keeps the faint share"), Rec ? Rec->LastCharacter.Inventory.GetCount(TEXT("Gloomsilk")) : -1, 2);
		TestEqual(TEXT("Run closed"), ServerDirector()->Depth, 0);
	});
	Stop(Ctx);
	return true;
}

#endif
