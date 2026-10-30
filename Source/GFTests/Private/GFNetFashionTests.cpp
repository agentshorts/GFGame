// Fashion show over the network: the server owns the show, clients act through their own player link (RPCs), the
// state replicates, dyes are burned and rewards minted only through the ledger.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFRunwayStage.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace
{
	struct FFashionRun
	{
		FString HostKey;
		FString ClientKey;
		TWeakObjectPtr<AGFRunwayStage> ServerStage;
		int32 AppliedAtResults = 0;
	};

	FGFOutfit MakeOutfit(std::initializer_list<const TCHAR*> Ids)
	{
		FGFOutfit O;
		for (const TCHAR* Id : Ids)
		{
			O.Equip(Id);
		}
		return O;
	}

	FGFOutfit GardenLook() { return MakeOutfit({ TEXT("FernBraid"), TEXT("PetalBlouse"), TEXT("MeadowSkirt"), TEXT("BloomSandals"), TEXT("DaisyCrown") }); }
	FGFOutfit ExplorerLook() { return MakeOutfit({ TEXT("MossMop"), TEXT("ExplorerVest"), TEXT("CargoShorts"), TEXT("TrailBoots"), TEXT("FossilCompass") }); }

	FGFOutfit DyedGardenLook()
	{
		FGFOutfit O = GardenLook();
		O.Dye(EGFOutfitSlot::Top, FLinearColor(0.9f, 0.1f, 0.1f)); // RoseDye
		return O;
	}

	bool HasAnswer(const AGFRunwayPlayerLink* Link, EGFRunwayRequest Request, EGFRunwayRequestResult Result)
	{
		return Link && Link->GetAnswers().ContainsByPredicate([&](const FGFRunwayRequestAnswer& A) { return A.Request == Request && A.Result == Result; });
	}

	AGFRunwayStage* ClientStage() { return AGFRunwayStage::Find(ClientWorlds()[0]); }
	AGFRunwayPlayerLink* ClientLink() { return AGFRunwayPlayerLink::FindLocal(ClientWorlds()[0]); }
	AGFRunwayPlayerLink* HostLink() { return AGFRunwayPlayerLink::FindLocal(ServerWorld()); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetFashionTest, "GFGame.Net.Fashion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetFashionTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FFashionRun> Run = MakeShared<FFashionRun>();
	Do(Ctx, [this, Run]()
	{
		UWorld* Server = ServerWorld();
		Run->HostKey = LocalPlayerState(Server)->PlayerKey;
		Run->ClientKey = LocalPlayerState(ClientWorlds()[0])->PlayerKey;
		AGFRunwayStage* Stage = Server->SpawnActor<AGFRunwayStage>();
		Stage->DressingSeconds = 60.f;
		Stage->WalkSeconds = 0.5f;
		Stage->JudgingSeconds = 0.5f;
		Run->ServerStage = Stage;
		// The client owns exactly one RoseDye.
		const int32 Have = LocalPlayerState(ClientWorlds()[0])->Inventory->GetCount(TEXT("RoseDye"));
		AGFGameState* GS = GameState(Server);
		TestTrue(TEXT("Dye minted to the client"), GS->ServerMint(FGuid(0x46415348, 1, 2, 3), Run->ClientKey, { FGFItemStack(TEXT("RoseDye"), 1 - Have) }, TEXT("Test")) == EGFTxResult::Applied || Have == 1);
	});
	WaitUntil(Ctx, TEXT("stage and links replicated"), 20.f, []()
	{
		return ClientStage() && ClientLink() && HostLink()
			&& LocalPlayerState(ClientWorlds()[0])->Inventory->GetCount(TEXT("RoseDye")) == 1;
	});
	Do(Ctx, [this]()
	{
		// A client can't run the show directly: its copy of the stage has no authority.
		TestTrue(TEXT("Client StartShow refused"), ClientStage()->StartShow(TEXT("GardenParty")) == EGFRunwayRequestResult::NotAuthority);
		TestTrue(TEXT("Client HandleSetOutfit refused"), ClientStage()->HandleSetOutfit(TEXT("Anyone"), GardenLook()) == EGFRunwayRequestResult::NotAuthority);
		ClientLink()->RequestStart(TEXT("GardenParty")); // guests can't start a show
	});
	WaitUntil(Ctx, TEXT("guest start refused"), 20.f, []()
	{
		return HasAnswer(ClientLink(), EGFRunwayRequest::Start, EGFRunwayRequestResult::NotHost);
	});
	Do(Ctx, [this]()
	{
		TestTrue(TEXT("No show from a guest's request"), ClientStage()->GetPhase() == EGFRunwayPhase::Idle);
		HostLink()->RequestStart(TEXT("GardenParty"));
	});
	WaitUntil(Ctx, TEXT("client sees dressing"), 20.f, [Run]()
	{
		return ClientStage()->GetPhase() == EGFRunwayPhase::Dressing
			&& HasAnswer(HostLink(), EGFRunwayRequest::Start, EGFRunwayRequestResult::Ok)
			&& ClientStage()->FindContestant(Run->HostKey) && ClientStage()->FindContestant(Run->ClientKey);
	});
	Do(Ctx, []()
	{
		FGFOutfit Bad = GardenLook();
		Swap(Bad.Slots[0], Bad.Slots[1]);
		ClientLink()->RequestOutfit(Bad);
		ClientLink()->RequestOutfit(DyedGardenLook());
		ClientLink()->RequestOutfit(DyedGardenLook()); // resend: charged once
		HostLink()->RequestOutfit(ExplorerLook());
		HostLink()->RequestStart(TEXT("GardenParty")); // already running
	});
	WaitUntil(Ctx, TEXT("outfits applied and replicated"), 20.f, [Run]()
	{
		const FGFRunwayContestant* C = ClientStage()->FindContestant(Run->ClientKey);
		const FGFRunwayContestant* H = ClientStage()->FindContestant(Run->HostKey);
		return HasAnswer(ClientLink(), EGFRunwayRequest::Outfit, EGFRunwayRequestResult::InvalidOutfit)
			&& ClientLink()->GetAnswers().Num() >= 4
			&& HasAnswer(HostLink(), EGFRunwayRequest::Start, EGFRunwayRequestResult::WrongPhase)
			&& C && C->Outfit.Slots[static_cast<int32>(EGFOutfitSlot::Top)].bDyed
			&& H && !H->Outfit.IsEmpty()
			&& LocalPlayerState(ClientWorlds()[0])->Inventory->GetCount(TEXT("RoseDye")) == 0;
	});
	Do(Ctx, [this, Run]()
	{
		const AGFGameState* GS = GameState(ServerWorld());
		TestEqual(TEXT("One RoseDye burned"), GS->Ledger.GetBurned(TEXT("RoseDye")), static_cast<int64>(1));
		TestTrue(TEXT("Burn TxId is show + player + slot + colour"), GS->Ledger.WasApplied(Run->ServerStage->GetShow().DyeTxId(Run->ClientKey, EGFOutfitSlot::Top, DyedGardenLook().Slots[static_cast<int32>(EGFOutfitSlot::Top)].DyeColor)));
		int32 OkOutfits = 0;
		for (const FGFRunwayRequestAnswer& A : ClientLink()->GetAnswers())
		{
			OkOutfits += A.Request == EGFRunwayRequest::Outfit && A.Result == EGFRunwayRequestResult::Ok ? 1 : 0;
		}
		TestEqual(TEXT("Both dyed sends accepted"), OkOutfits, 2);
		ClientLink()->RequestReady(true);
		HostLink()->RequestReady(true);
	});
	WaitUntil(Ctx, TEXT("client sees results"), 20.f, []()
	{
		return ClientStage()->GetPhase() == EGFRunwayPhase::Results;
	});
	WaitUntil(Ctx, TEXT("rewards replicated to the client's inventory"), 20.f, [Run]()
	{
		const FGFRunwayContestant* C = ClientStage()->FindContestant(Run->ClientKey);
		if (!C || C->Rewards.Num() == 0)
		{
			return false;
		}
		const UGFInventoryComponent* Inv = LocalPlayerState(ClientWorlds()[0])->Inventory;
		for (const FGFItemStack& S : C->Rewards)
		{
			if (Inv->GetCount(S.ItemId) < S.Count)
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [this, Run]()
	{
		const AGFGameState* GS = GameState(ServerWorld());
		const AGFRunwayStage* Stage = Run->ServerStage.Get();
		TestTrue(TEXT("Host reward minted"), GS->Ledger.WasApplied(Stage->GetShow().RewardTxId(Run->HostKey)));
		TestTrue(TEXT("Client reward minted"), GS->Ledger.WasApplied(Stage->GetShow().RewardTxId(Run->ClientKey)));
		TestEqual(TEXT("Nothing pending"), Stage->GetPendingRewards().Num(), 0);
		Run->AppliedAtResults = GS->Ledger.AppliedOrder.Num();
	});
	Wait(Ctx, 1.f);
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Rewards paid once"), GameState(ServerWorld())->Ledger.AppliedOrder.Num(), Run->AppliedAtResults);
	});
	Stop(Ctx);
	return true;
}

#endif
