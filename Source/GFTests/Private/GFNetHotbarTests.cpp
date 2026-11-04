// Hotbar over the network: the server fills a client's bag, the client's bar follows the replicated inventory,
// and the number keys and the wheel select slots there (highlight, held kind, counts).
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.Hotbar;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFCharacter.h"
#include "GFGameState.h"
#include "GFHotbarRules.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "Aim/GFAimSim.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetHotbarDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	AGFCharacter* ClientCharacter()
	{
		const TArray<UWorld*> Clients = ClientWorlds();
		AGFPlayerState* PS = Clients.Num() > 0 ? LocalPlayerState(Clients[0]) : nullptr;
		return PS ? Cast<AGFCharacter>(PS->GetPawn()) : nullptr;
	}

	AGFPlayerState* ClientOnServer()
	{
		const TArray<UWorld*> Clients = ClientWorlds();
		const AGFPlayerState* Mine = Clients.Num() > 0 ? LocalPlayerState(Clients[0]) : nullptr;
		return Mine ? PlayerStateByKey(ServerWorld(), Mine->PlayerKey) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetHotbarSelectTest, "GFGame.Net.Hotbar.Select", GFNetHotbarDetail::Flags)
bool FGFNetHotbarSelectTest::RunTest(const FString&)
{
	using namespace GFNetHotbarDetail;
	FCtxRef Ctx = Start(this, 2);
	WaitUntil(Ctx, TEXT("client pawn"), 20.f, []() { return ClientCharacter() != nullptr && ClientOnServer() != nullptr; });
	Do(Ctx, [Ctx]()
	{
		AGFGameState* GS = GameState(ServerWorld());
		AGFPlayerState* OnServer = ClientOnServer();
		if (!GS || !OnServer)
		{
			Ctx->Test->AddError(TEXT("No server game state or client player state"));
			Ctx->bFailed = true;
			return;
		}
		GS->bFreeBuild = false;
		OnServer->Inventory->SetInventory(FGFInventory());
		OnServer->Inventory->AddItem(TEXT("Dirt"), 7);
		OnServer->Inventory->AddItem(TEXT("Stone"), 1200);
	});
	WaitUntil(Ctx, TEXT("client bar shows the new blocks"), 10.f, []()
	{
		const AGFCharacter* C = ClientCharacter();
		return C && C->GetHotbarIds().Contains(FName(TEXT("Stone")));
	});
	Do(Ctx, [Ctx]()
	{
		FAutomationTestBase* Test = Ctx->Test;
		AGFCharacter* C = ClientCharacter();
		const AGFPlayerState* PS = C ? C->GetPlayerState<AGFPlayerState>() : nullptr;
		if (!C || !PS)
		{
			Test->AddError(TEXT("No client character"));
			Ctx->bFailed = true;
			return;
		}
		const TArray<FName> Ids = C->GetHotbarIds();
		Test->TestEqual(TEXT("Nine slots on the client"), Ids.Num(), FGFHotbarRules::SlotCount);
		Test->TestEqual(TEXT("Fists last"), Ids.Last(), FGFHotbarRules::FistsId());
		const int32 StoneSlot = Ids.IndexOfByKey(FName(TEXT("Stone")));
		const int32 DirtSlot = Ids.IndexOfByKey(FName(TEXT("Dirt")));
		Test->TestTrue(TEXT("Dirt and Stone both on the bar"), StoneSlot != INDEX_NONE && DirtSlot != INDEX_NONE);
		Test->TestEqual(TEXT("Dirt count from the replicated bag"), PS->GetHotbarCount(TEXT("Dirt")), int64(7));
		Test->TestEqual(TEXT("Stone shows the cap"), FGFHotbarRules::FormatCount(PS->GetHotbarCount(TEXT("Stone"))), FString(TEXT("999+")));
		Test->TestEqual(TEXT("Fists have no count"), PS->GetHotbarCount(FGFHotbarRules::FistsId()), int64(-1));

		// Number key for the stone slot.
		C->SelectHotbarSlot(StoneSlot);
		Test->TestEqual(TEXT("Number key highlights the slot"), C->GetSelectedHotbarSlot(), StoneSlot);
		Test->TestEqual(TEXT("Number key selects the block"), C->SelectedBlock, FName(TEXT("Stone")));
		Test->TestTrue(TEXT("Holding a block"), C->GetAimHeldKind() == EGFAimHeldKind::Block);

		// Slot 9 is bare hands.
		C->SelectHotbarSlot(8);
		Test->TestEqual(TEXT("Slot 9 is Fists"), C->SelectedBlock, GFAim::FistsId());
		Test->TestTrue(TEXT("Holding fists"), C->GetAimHeldKind() == EGFAimHeldKind::Fists);

		// Wheel: up from 9 wraps to 1, down from 1 wraps to 9.
		C->CycleBlock(1);
		Test->TestEqual(TEXT("Wheel up wraps to slot 1"), C->GetSelectedHotbarSlot(), 0);
		C->CycleBlock(-1);
		Test->TestEqual(TEXT("Wheel down wraps to slot 9"), C->GetSelectedHotbarSlot(), 8);

		// An empty slot is bare hands too.
		const int32 EmptySlot = Ids.IndexOfByKey(NAME_None);
		if (EmptySlot != INDEX_NONE)
		{
			C->SelectHotbarSlot(EmptySlot);
			Test->TestEqual(TEXT("Empty slot holds fists"), C->SelectedBlock, GFAim::FistsId());
		}
		C->SelectHotbarSlot(DirtSlot);
	});
	// The server spends all the dirt: the client's bar drops it and the selection follows the slot.
	Do(Ctx, []()
	{
		if (AGFPlayerState* OnServer = ClientOnServer())
		{
			OnServer->Inventory->RemoveItem(TEXT("Dirt"), 7);
		}
	});
	WaitUntil(Ctx, TEXT("client bar drops the spent dirt"), 10.f, []()
	{
		const AGFCharacter* C = ClientCharacter();
		return C && !C->GetHotbarIds().Contains(FName(TEXT("Dirt")));
	});
	Wait(Ctx, 0.2f);
	Do(Ctx, [Ctx]()
	{
		const AGFCharacter* C = ClientCharacter();
		if (!C)
		{
			return;
		}
		const TArray<FName> Ids = C->GetHotbarIds();
		const FName Now = Ids.IsValidIndex(C->GetSelectedHotbarSlot()) ? Ids[C->GetSelectedHotbarSlot()] : FName();
		Ctx->Test->TestEqual(TEXT("Selection follows what the slot holds now"), C->SelectedBlock, Now.IsNone() ? GFAim::FistsId() : Now);
	});
	Stop(Ctx);
	return true;
}

#endif
