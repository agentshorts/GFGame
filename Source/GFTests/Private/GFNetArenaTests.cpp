// Dino Arena over the network (PLAN.md M6 Net.Arena): two clients place dinos from the balcony, wagers go
// through the safeguards (pinned recipe, wagers off, escrow), Slow Time slows the arena world on every machine
// and its commands and possession are owner-only, a disconnect mid-fight refunds the stake, and the payout
// matches rewards +- the wager exactly.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFArenaDirector.h"
#include "GFModeTypes.h"
#include "GFEconomy.h"
#include "GFRewards.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetArenaTest
{
	struct FArenaRun
	{
		FString C1, C2;
		TArray<FGuid> C1Dinos;
		TMap<FName, int32> Want; // request name -> request id (client side)
		FGFInventory Before; // client 1's items before the fight
		FGuid C2Escrow;
		int32 C2Amber = 0;
		int32 RexUnit = INDEX_NONE;
		int32 RaptorUnit = INDEX_NONE;
		int32 NipperUnit = INDEX_NONE;
	};

	static const FName Arena(TEXT("Arena"));
	static const FName PlayerMode(TEXT("Player"));

	AGFPlayerState* Client(int32 i) { return LocalPlayerState(ClientWorlds()[i]); }
	AGFPlayerState* OnServer(const FString& Key) { return PlayerStateByKey(ServerWorld(), Key); }
	AGFArenaDirector* ServerDirector() { return AGFArenaDirector::Get(ServerWorld(), false); }

	int32 Send(FArenaRun& Run, int32 ClientIndex, const TCHAR* Label, FName Mode, FName Verb, TFunction<void(FGFModeRequest&)> Fill = nullptr)
	{
		FGFModeRequest R = FGFModeRequest::Make(Mode, Verb);
		if (Fill)
		{
			Fill(R);
		}
		const int32 Id = Client(ClientIndex)->RequestMode(R);
		Run.Want.Add(FName(Label), Id + ClientIndex * 100000);
		return Id;
	}

	const FGFModeResult* Result(const FArenaRun& Run, const TCHAR* Label)
	{
		const int32* Packed = Run.Want.Find(FName(Label));
		if (!Packed)
		{
			return nullptr;
		}
		const int32 ClientIndex = *Packed / 100000;
		const AGFPlayerState* PS = Client(ClientIndex);
		return PS ? PS->ModeResults.Find(*Packed % 100000) : nullptr;
	}

	bool HaveAll(const FArenaRun& Run, std::initializer_list<const TCHAR*> Labels)
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

	FString Code(const FArenaRun& Run, const TCHAR* Label)
	{
		const FGFModeResult* R = Result(Run, Label);
		return !R ? FString(TEXT("<none>")) : (R->bOk ? FString(TEXT("Ok")) : R->Code.ToString());
	}

	float Dilation(UWorld* W)
	{
		const AWorldSettings* WS = W ? W->GetWorldSettings() : nullptr;
		return WS ? WS->TimeDilation : -1.f;
	}

	int32 UnitOf(const FGFArenaSim& Sim, const FString& Owner, FName Species)
	{
		for (const FGFArenaUnit& U : Sim.Units)
		{
			if (U.OwnerKey == Owner && U.Species == Species)
			{
				return U.Id;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetArenaTest, "GFGame.Net.Arena", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetArenaTest::RunTest(const FString&)
{
	using namespace GFNetArenaTest;
	// Client 2 gets kicked mid-fight; its own "connection lost" error is expected.
	AddExpectedMessage(TEXT("Your connection to the host has been lost"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0, false);
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FArenaRun> Run = MakeShared<FArenaRun>();

	Do(Ctx, [this, Run]()
	{
		UWorld* Server = ServerWorld();
		Run->C1 = Client(0)->PlayerKey;
		Run->C2 = Client(1)->PlayerKey;
		AGFPlayerState* P1 = OnServer(Run->C1);
		AGFPlayerState* P2 = OnServer(Run->C2);
		if (!TestNotNull(TEXT("Client players on the server"), P1) || !P2)
		{
			return;
		}
		Run->C1Dinos.Add(P1->ServerAddCompanion(TEXT("Rex")));
		Run->C1Dinos.Add(P1->ServerAddCompanion(TEXT("Raptor")));
		Run->C1Dinos.Add(P1->ServerAddCompanion(TEXT("Nipper")));
		P2->ServerAddCompanion(TEXT("Sailback"));
		AGFGameState* GS = GameState(Server);
		// Start from empty bags so the 25 % cap and the pinned-recipe numbers below are exact.
		for (AGFPlayerState* P : { P1, P2 })
		{
			const TArray<FGFItemStack> All = P->Inventory->GetInventory().Items;
			if (All.Num() > 0)
			{
				GS->ServerBurn(FGuid::NewGuid(), P->PlayerKey, All, TEXT("Test"));
			}
		}
		GS->ServerMint(FGuid::NewGuid(), Run->C1, { FGFItemStack(TEXT("Amber"), 10), FGFItemStack(TEXT("Wood"), 20) }, TEXT("Test"));
		GS->ServerMint(FGuid::NewGuid(), Run->C2, { FGFItemStack(TEXT("Amber"), 10), FGFItemStack(TEXT("WoodPlank"), 4) }, TEXT("Test"));
		AGFArenaDirector* D = AGFArenaDirector::Get(Server);
		if (TestNotNull(TEXT("Arena director"), D))
		{
			D->ForcedSeed = 4242;
			D->bApplyWorldDilation = true; // this test world stands in for the Colosseum map
			D->SimSpeed = 0.5f;
		}
	});
	WaitUntil(Ctx, TEXT("clients see their dinos"), 15.f, []()
	{
		const AGFPlayerState* A = Client(0);
		const AGFPlayerState* B = Client(1);
		return A && B && A->Companions.Num() == 3 && B->Companions.Num() == 1;
	});
	WaitUntil(Ctx, TEXT("clients see their items"), 15.f, []()
	{
		const AGFPlayerState* A = Client(0);
		const AGFPlayerState* B = Client(1);
		return A && B && A->Inventory->GetCount(TEXT("Amber")) == 10 && B->Inventory->GetCount(TEXT("WoodPlank")) == 4 && A->Inventory->GetInventory().Items.Num() == 2;
	});
	WaitUntil(Ctx, TEXT("clients see the arena director"), 15.f, []()
	{
		return AGFArenaDirector::Get(ClientWorlds()[0], false) && AGFArenaDirector::Get(ClientWorlds()[1], false);
	});

	// ---- the balcony: placement, pinning, wagers ----
	Do(Ctx, [Run]()
	{
		Send(*Run, 0, TEXT("Place"), Arena, TEXT("Place"), [Run](FGFModeRequest& R) { R.Ids = Run->C1Dinos; R.Ints = { 0, 3, 4 }; });
		Send(*Run, 0, TEXT("PlaceFour"), Arena, TEXT("Place"), [Run](FGFModeRequest& R) { R.Ids = Run->C1Dinos; R.Ids.Add(FGuid::NewGuid()); R.Ints = { 0, 1, 2, 5 }; });
		Send(*Run, 1, TEXT("Auto"), Arena, TEXT("AutoPlace"));
		Send(*Run, 1, TEXT("Pin"), PlayerMode, TEXT("PinRecipe"), [](FGFModeRequest& R) { R.Names = { TEXT("Chair") }; });
		Send(*Run, 1, TEXT("Pinned"), Arena, TEXT("Wager"), [](FGFModeRequest& R) { R.Stacks = { FGFItemStack(TEXT("WoodPlank"), 1) }; });
		Send(*Run, 1, TEXT("Off"), PlayerMode, TEXT("WagersOff"), [](FGFModeRequest& R) { R.Ints = { 1 }; });
		Send(*Run, 1, TEXT("WhileOff"), Arena, TEXT("Wager"), [](FGFModeRequest& R) { R.Stacks = { FGFItemStack(TEXT("Amber"), 1) }; });
		Send(*Run, 1, TEXT("On"), PlayerMode, TEXT("WagersOff"), [](FGFModeRequest& R) { R.Ints = { 0 }; });
		Send(*Run, 1, TEXT("C2Wager"), Arena, TEXT("Wager"), [](FGFModeRequest& R) { R.Stacks = { FGFItemStack(TEXT("Amber"), 2) }; });
		Send(*Run, 0, TEXT("OverCap"), Arena, TEXT("Wager"), [](FGFModeRequest& R) { R.Stacks = { FGFItemStack(TEXT("Amber"), 4) }; });
		Send(*Run, 0, TEXT("Dino"), Arena, TEXT("Wager"), [](FGFModeRequest& R) { R.Stacks = { FGFItemStack(TEXT("Raptor"), 1) }; });
		Send(*Run, 0, TEXT("Suggest"), Arena, TEXT("Suggest"));
		Send(*Run, 0, TEXT("C1Wager"), Arena, TEXT("Wager"), [](FGFModeRequest& R) { R.Stacks = { FGFItemStack(TEXT("Amber"), 3) }; });
		Send(*Run, 1, TEXT("NotHost"), Arena, TEXT("HostWagersOff"), [](FGFModeRequest& R) { R.Ints = { 1 }; });
		Send(*Run, 0, TEXT("Early"), Arena, TEXT("SlowTime"));
	});
	WaitUntil(Ctx, TEXT("balcony answers"), 15.f, [Run]()
	{
		return HaveAll(*Run, { TEXT("Place"), TEXT("PlaceFour"), TEXT("Auto"), TEXT("Pin"), TEXT("Pinned"), TEXT("Off"), TEXT("WhileOff"), TEXT("On"), TEXT("C2Wager"),
			TEXT("OverCap"), TEXT("Dino"), TEXT("Suggest"), TEXT("C1Wager"), TEXT("NotHost"), TEXT("Early") });
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Place"), Code(*Run, TEXT("Place")), FString(TEXT("Ok")));
		TestEqual(TEXT("At most 3 dinos"), Code(*Run, TEXT("PlaceFour")), FString(TEXT("BadPlacement")));
		TestEqual(TEXT("AutoPlace"), Code(*Run, TEXT("Auto")), FString(TEXT("Ok")));
		TestEqual(TEXT("Pin"), Code(*Run, TEXT("Pin")), FString(TEXT("Ok")));
		TestEqual(TEXT("Pinned recipe materials protected"), Code(*Run, TEXT("Pinned")), FString(TEXT("PinnedProtected")));
		TestEqual(TEXT("Wagers off"), Code(*Run, TEXT("WhileOff")), FString(TEXT("WagersOff")));
		TestEqual(TEXT("Client 2 stakes"), Code(*Run, TEXT("C2Wager")), FString(TEXT("Ok")));
		TestEqual(TEXT("25 % cap"), Code(*Run, TEXT("OverCap")), FString(TEXT("OverCap")));
		TestEqual(TEXT("Never dinos"), Code(*Run, TEXT("Dino")), FString(TEXT("NotWagerable")));
		TestEqual(TEXT("Suggest"), Code(*Run, TEXT("Suggest")), FString(TEXT("Ok")));
		TestEqual(TEXT("Client 1 stakes"), Code(*Run, TEXT("C1Wager")), FString(TEXT("Ok")));
		TestEqual(TEXT("Only the host switches wagers off for everyone"), Code(*Run, TEXT("NotHost")), FString(TEXT("NotHost")));
		TestEqual(TEXT("No Slow Time before the fight"), Code(*Run, TEXT("Early")), FString(TEXT("NotFighting")));
		const FGFModeResult* W = Result(*Run, TEXT("C1Wager"));
		TestTrue(TEXT("The AI matches the player's stake"), W && W->Stacks.Num() > 0);
		const AGFPlayerState* P1 = OnServer(Run->C1);
		TestTrue(TEXT("Formation remembered"), P1 && P1->Progress.ArenaFormationSpots.Num() == 3);
		Run->Before = P1 ? P1->Inventory->GetInventory() : FGFInventory();
		const AGFPlayerState* P2 = OnServer(Run->C2);
		Run->C2Amber = P2 ? P2->Inventory->GetCount(TEXT("Amber")) : -1;
		if (AGFArenaDirector* D = ServerDirector())
		{
			D->SimSpeed = 0.f; // frozen while the test gives orders; sped up at the end
		}
		Send(*Run, 0, TEXT("Fight"), Arena, TEXT("Fight"));
	});
	WaitUntil(Ctx, TEXT("the fight starts"), 15.f, [Run]() { return Result(*Run, TEXT("Fight")) != nullptr; });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Fight"), Code(*Run, TEXT("Fight")), FString(TEXT("Ok")));
		AGFArenaDirector* D = ServerDirector();
		if (!TestNotNull(TEXT("Director"), D))
		{
			return;
		}
		TestTrue(TEXT("Fighting"), D->Phase == EGFArenaPhase::Fighting);
		const FGFArenaEntry* E1 = D->FindEntry(Run->C1);
		const FGFArenaEntry* E2 = D->FindEntry(Run->C2);
		TestTrue(TEXT("Stakes held in escrow"), E1 && E1->bEscrowOpen && E2 && E2->bEscrowOpen);
		if (E2)
		{
			Run->C2Escrow = E2->EscrowId;
		}
		TestEqual(TEXT("Player's stake left their bag"), OnServer(Run->C1)->Inventory->GetCount(TEXT("Amber")), Run->Before.GetCount(TEXT("Amber")) - 3);
		TestEqual(TEXT("Team: player's 3 + friend's 1"), D->GetSim().AliveCount(0), 4);
		TestTrue(TEXT("At least 3 opponents"), D->GetSim().AliveCount(1) >= 3);
		Run->RexUnit = UnitOf(D->GetSim(), Run->C1, TEXT("Rex"));
		Run->RaptorUnit = UnitOf(D->GetSim(), Run->C1, TEXT("Raptor"));
		Run->NipperUnit = UnitOf(D->GetSim(), Run->C1, TEXT("Nipper"));
		// Fresh cooldowns so the commands below are allowed (the sim test covers cooldown refusals).
		for (FGFArenaUnit& U : D->EditSimForTests().Units)
		{
			for (float& C : U.Cooldowns)
			{
				C = 0.f;
			}
		}
		Send(*Run, 0, TEXT("Slow"), Arena, TEXT("SlowTime"));
	});
	WaitUntil(Ctx, TEXT("Slow Time on every machine"), 10.f, [Run]()
	{
		return Result(*Run, TEXT("Slow")) && FMath::IsNearlyEqual(Dilation(ServerWorld()), 0.2f) &&
			FMath::IsNearlyEqual(Dilation(ClientWorlds()[0]), 0.2f) && FMath::IsNearlyEqual(Dilation(ClientWorlds()[1]), 0.2f);
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Slow Time"), Code(*Run, TEXT("Slow")), FString(TEXT("Ok")));
		Send(*Run, 1, TEXT("StealCmd"), Arena, TEXT("Command"), [Run](FGFModeRequest& R) { R.Ints = { Run->RexUnit, 1, INDEX_NONE }; });
		Send(*Run, 1, TEXT("StealPossess"), Arena, TEXT("Possess"), [Run](FGFModeRequest& R) { R.Ints = { Run->RaptorUnit }; });
		Send(*Run, 0, TEXT("Cmd"), Arena, TEXT("Command"), [Run](FGFModeRequest& R) { R.Ints = { Run->RexUnit, 1, INDEX_NONE }; });
		Send(*Run, 0, TEXT("Cmd2"), Arena, TEXT("Command"), [Run](FGFModeRequest& R) { R.Ints = { Run->RexUnit, 0, INDEX_NONE }; });
		Send(*Run, 0, TEXT("Locked"), Arena, TEXT("Command"), [Run](FGFModeRequest& R) { R.Ints = { Run->NipperUnit, 2, INDEX_NONE }; });
	});
	WaitUntil(Ctx, TEXT("command answers"), 10.f, [Run]()
	{
		return HaveAll(*Run, { TEXT("StealCmd"), TEXT("StealPossess"), TEXT("Cmd"), TEXT("Cmd2"), TEXT("Locked") });
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Friend can't command the player's dino"), Code(*Run, TEXT("StealCmd")), FString(TEXT("NotOwner")));
		TestEqual(TEXT("Friend can't possess the player's dino"), Code(*Run, TEXT("StealPossess")), FString(TEXT("NotOwner")));
		TestEqual(TEXT("Command"), Code(*Run, TEXT("Cmd")), FString(TEXT("Ok")));
		TestEqual(TEXT("One order per dino"), Code(*Run, TEXT("Cmd2")), FString(TEXT("AlreadyCommanded")));
		TestEqual(TEXT("Third ability locked at level 1"), Code(*Run, TEXT("Locked")), FString(TEXT("Locked")));
		AGFArenaDirector* D = ServerDirector();
		TestTrue(TEXT("Still slowed"), D && D->bSlowTimeActive);
		Send(*Run, 0, TEXT("EndSlow"), Arena, TEXT("SlowTime"));
	});
	WaitUntil(Ctx, TEXT("Slow Time ended on every machine"), 10.f, [Run]()
	{
		return Result(*Run, TEXT("EndSlow")) && FMath::IsNearlyEqual(Dilation(ServerWorld()), 1.f) &&
			FMath::IsNearlyEqual(Dilation(ClientWorlds()[0]), 1.f) && FMath::IsNearlyEqual(Dilation(ClientWorlds()[1]), 1.f);
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Second press ends it"), Code(*Run, TEXT("EndSlow")), FString(TEXT("Ok")));
		AGFArenaDirector* D = ServerDirector();
		TestTrue(TEXT("Cooldown running"), D && D->SlowTimeCooldown > 25.f);
		const FGFArenaUnit* Rex = D ? D->GetSim().Find(Run->RexUnit) : nullptr;
		TestTrue(TEXT("The order went off or is walking there"), Rex && (Rex->Cooldowns[1] > 0.f || Rex->CommandSlot == 1));
		Send(*Run, 0, TEXT("Again"), Arena, TEXT("SlowTime"));
		Send(*Run, 0, TEXT("Possess"), Arena, TEXT("Possess"), [Run](FGFModeRequest& R) { R.Ints = { Run->RexUnit }; });
		Send(*Run, 0, TEXT("Steer"), Arena, TEXT("Input"), [](FGFModeRequest& R) { R.Floats = { 1.f, 0.f }; R.Ints = { 1, -1 }; });
		Send(*Run, 0, TEXT("Cycle"), Arena, TEXT("Cycle"));
	});
	WaitUntil(Ctx, TEXT("possession answers"), 10.f, [Run]()
	{
		return HaveAll(*Run, { TEXT("Again"), TEXT("Possess"), TEXT("Steer"), TEXT("Cycle") });
	});
	WaitUntil(Ctx, TEXT("client 1 sees itself possessing a dino"), 10.f, [Run]()
	{
		const AGFArenaDirector* D = AGFArenaDirector::Get(ClientWorlds()[0], false);
		return D && D->Units.ContainsByPredicate([Run](const FGFArenaUnitView& V) { return V.PossessedBy == Run->C1; });
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("One charge: on cooldown"), Code(*Run, TEXT("Again")), FString(TEXT("Cooldown")));
		TestEqual(TEXT("Possess"), Code(*Run, TEXT("Possess")), FString(TEXT("Ok")));
		TestEqual(TEXT("Steer"), Code(*Run, TEXT("Steer")), FString(TEXT("Ok")));
		const FGFModeResult* C = Result(*Run, TEXT("Cycle"));
		TestTrue(TEXT("Cycled to another of the player's dinos"), C && C->Ints.Num() == 1 && C->Ints[0] != Run->RexUnit && C->Ints[0] != INDEX_NONE);
		Send(*Run, 0, TEXT("Release"), Arena, TEXT("Release"));
		Send(*Run, 0, TEXT("NoSteer"), Arena, TEXT("Input"), [](FGFModeRequest& R) { R.Floats = { 1.f, 0.f }; });
	});
	WaitUntil(Ctx, TEXT("release answers"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Release"), TEXT("NoSteer") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Release"), Code(*Run, TEXT("Release")), FString(TEXT("Ok")));
		TestEqual(TEXT("Back on the balcony"), Code(*Run, TEXT("NoSteer")), FString(TEXT("NotPossessing")));
		// Client 2 drops mid-fight (kicked by the host): its stake goes home.
		UWorld* Server = ServerWorld();
		AGFPlayerState* P2 = OnServer(Run->C2);
		APlayerController* PC = P2 ? P2->GetPlayerController() : nullptr;
		AGameModeBase* GM = Server->GetAuthGameMode();
		if (TestNotNull(TEXT("Client 2 controller"), PC) && GM && GM->GameSession)
		{
			GM->GameSession->KickPlayer(PC, FText::FromString(TEXT("GF test: drop mid-fight")));
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
		AGFGameState* GS = GameState(ServerWorld());
		const FGFGuestRecord* Rec = GS->GuestRecords.Find(Run->C2);
		TestEqual(TEXT("Dropped player's stake refunded"), Rec ? Rec->LastCharacter.Inventory.GetCount(TEXT("Amber")) : -1, Run->C2Amber);
		const FGFEscrow* Esc = GS->Ledger.FindEscrow(Run->C2Escrow);
		TestTrue(TEXT("Their escrow is closed"), Esc && !Esc->bOpen);
		AGFArenaDirector* D = ServerDirector();
		if (D)
		{
			TestTrue(TEXT("The fight goes on for the player"), D->Phase == EGFArenaPhase::Fighting);
			D->SimSpeed = 25.f; // finish quickly
		}
	});
	WaitUntil(Ctx, TEXT("the fight ends"), 40.f, []()
	{
		const AGFArenaDirector* D = ServerDirector();
		return D && D->Phase == EGFArenaPhase::Finished;
	});
	WaitUntil(Ctx, TEXT("client 1 sees its match count"), 10.f, []()
	{
		const AGFPlayerState* P = Client(0);
		return P && P->Progress.ArenaMatches == 1;
	});
	Do(Ctx, [this, Run]()
	{
		const AGFArenaDirector* D = ServerDirector();
		const FGFArenaEntry* E = D ? D->FindEntry(Run->C1) : nullptr;
		const AGFPlayerState* P1 = OnServer(Run->C1);
		if (!TestNotNull(TEXT("Player's entry"), E) || !P1)
		{
			return;
		}
		AddInfo(FString::Printf(TEXT("Arena outcome %d, %d star(s), wager %s, %d reward stacks"), int32(D->Outcome), E->Stars, *E->WagerResult.ToString(), E->Rewards.Num()));
		TestTrue(TEXT("A free reward roll"), E->Rewards.Num() > 0);
		TestFalse(TEXT("Escrow settled"), E->bEscrowOpen);
		TestTrue(TEXT("Wager settled"), !E->WagerResult.IsNone());
		const bool bWon = E->WagerResult == FName(TEXT("Won"));
		const bool bLost = E->WagerResult == FName(TEXT("Lost"));
		TSet<FName> Ids;
		for (const FGFItemStack& S : Run->Before.Items) { Ids.Add(S.ItemId); }
		for (const FGFItemStack& S : E->Rewards) { Ids.Add(S.ItemId); }
		for (const FGFItemStack& S : E->LastCounter) { Ids.Add(S.ItemId); }
		for (const FGFItemStack& S : P1->Inventory->GetInventory().Items) { Ids.Add(S.ItemId); }
		for (const FName& Id : Ids)
		{
			const int32 Expected = Run->Before.GetCount(Id) + FGFRewards::CountOf(E->Rewards, Id) + (bWon ? FGFRewards::CountOf(E->LastCounter, Id) : 0) - (bLost ? FGFRewards::CountOf(E->LastStake, Id) : 0);
			TestEqual(*FString::Printf(TEXT("%s after the fight"), *Id.ToString()), P1->Inventory->GetCount(Id), Expected);
		}
		TestTrue(TEXT("Dinos earned XP"), P1->Companions.ContainsByPredicate([](const FGFCompanionEntry& C) { return C.XP > 0 || C.Level > 1; }));
		TestTrue(TEXT("World speed back to normal"), FMath::IsNearlyEqual(Dilation(ServerWorld()), 1.f));
	});
	Stop(Ctx);
	return true;
}

#endif
