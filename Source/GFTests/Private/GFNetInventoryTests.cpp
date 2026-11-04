// The bag over the network and on disk: a client arranges its hotbar through the validated server move (owned,
// unowned, unknown, off the bar, swap, clear), the bar replicates back and the slot 9 hand item works; the bag
// modal frees the mouse and a slot drag swaps through the HUD hit boxes; the arranged bar survives a character save.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.Hotbar;GFGame.Net.Inventory;GFGame.Save.Hotbar;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFCharacter.h"
#include "GFGameState.h"
#include "GFHotbarRules.h"
#include "GFHUD.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "GFSaveGame.h"
#include "Aim/GFAimSim.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetInventoryDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	AGFPlayerState* ClientPS()
	{
		const TArray<UWorld*> Clients = ClientWorlds();
		return Clients.Num() > 0 ? LocalPlayerState(Clients[0]) : nullptr;
	}

	AGFCharacter* ClientCharacter()
	{
		AGFPlayerState* PS = ClientPS();
		return PS ? Cast<AGFCharacter>(PS->GetPawn()) : nullptr;
	}

	AGFPlayerState* ClientOnServer()
	{
		const AGFPlayerState* Mine = ClientPS();
		return Mine ? PlayerStateByKey(ServerWorld(), Mine->PlayerKey) : nullptr;
	}

	APlayerController* LocalPC(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				return PC;
			}
		}
		return nullptr;
	}

	/** Request ids in send order, shared between the steps. */
	struct FMoves
	{
		int32 Owned = INDEX_NONE;
		int32 Unowned = INDEX_NONE;
		int32 Unknown = INDEX_NONE;
		int32 OffBar = INDEX_NONE;
		int32 Swap = INDEX_NONE;
		int32 Clear = INDEX_NONE;
		int32 Block = INDEX_NONE;
		TArray<int32> All() const { return { Owned, Unowned, Unknown, OffBar, Swap, Clear, Block }; }
	};

	EGFHotbarMoveResult ResultOf(const AGFPlayerState* PS, int32 Id)
	{
		const EGFHotbarMoveResult* Found = PS ? PS->HotbarResults.Find(Id) : nullptr;
		return Found ? *Found : EGFHotbarMoveResult::Pending;
	}

	TArray<FName> Expected()
	{
		TArray<FName> Out;
		Out.Init(NAME_None, FGFHotbarRules::SlotCount);
		Out[4] = TEXT("Dirt");
		Out[8] = TEXT("Amber");
		return Out;
	}

	/** Expected() once the amber is spent: its slot is empty. */
	TArray<FName> SpentExpected()
	{
		TArray<FName> Out = Expected();
		Out[8] = NAME_None;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetHotbarMovesTest, "GFGame.Net.Hotbar.Moves", GFNetInventoryDetail::Flags)
bool FGFNetHotbarMovesTest::RunTest(const FString&)
{
	using namespace GFNetInventoryDetail;
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FMoves> Moves = MakeShared<FMoves>();
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
		OnServer->Inventory->AddItem(TEXT("Amber"), 2);
	});
	WaitUntil(Ctx, TEXT("client sees the bag"), 10.f, []()
	{
		const AGFPlayerState* PS = ClientPS();
		return PS && PS->Inventory && PS->Inventory->GetCount(TEXT("Amber")) == 2;
	});
	Do(Ctx, [Ctx, Moves]()
	{
		AGFPlayerState* PS = ClientPS();
		if (!PS)
		{
			Ctx->bFailed = true;
			return;
		}
		Ctx->Test->TestFalse(TEXT("Nothing arranged yet"), PS->Progress.Hotbar.bArranged);
		// Sent in order on one reliable channel, so each move sees the one before it.
		Moves->Owned = PS->RequestHotbarMove(FGFHotbarMove::Assign(0, TEXT("Amber")));
		Moves->Unowned = PS->RequestHotbarMove(FGFHotbarMove::Assign(1, TEXT("GlowDust")));
		Moves->Unknown = PS->RequestHotbarMove(FGFHotbarMove::Assign(2, TEXT("NotARealRow")));
		Moves->OffBar = PS->RequestHotbarMove(FGFHotbarMove::Assign(9, TEXT("Dirt")));
		Moves->Swap = PS->RequestHotbarMove(FGFHotbarMove::Swap(0, 8));
		Moves->Clear = PS->RequestHotbarMove(FGFHotbarMove::Clear(0));
		Moves->Block = PS->RequestHotbarMove(FGFHotbarMove::Assign(4, TEXT("Dirt")));
		Ctx->Test->TestTrue(TEXT("Client waits for the server"), ResultOf(PS, Moves->Owned) == EGFHotbarMoveResult::Pending);
	});
	WaitUntil(Ctx, TEXT("server answered every move"), 10.f, [Moves]()
	{
		const AGFPlayerState* PS = ClientPS();
		for (const int32 Id : Moves->All())
		{
			if (ResultOf(PS, Id) == EGFHotbarMoveResult::Pending)
			{
				return false;
			}
		}
		return PS != nullptr;
	});
	Do(Ctx, [Ctx, Moves]()
	{
		FAutomationTestBase* Test = Ctx->Test;
		const AGFPlayerState* PS = ClientPS();
		Test->TestTrue(TEXT("Owned item: Ok"), ResultOf(PS, Moves->Owned) == EGFHotbarMoveResult::Ok);
		Test->TestTrue(TEXT("Unowned item: NotOwned"), ResultOf(PS, Moves->Unowned) == EGFHotbarMoveResult::NotOwned);
		Test->TestTrue(TEXT("Unknown id: UnknownItem"), ResultOf(PS, Moves->Unknown) == EGFHotbarMoveResult::UnknownItem);
		Test->TestTrue(TEXT("Slot 10: BadSlot"), ResultOf(PS, Moves->OffBar) == EGFHotbarMoveResult::BadSlot);
		Test->TestTrue(TEXT("Swap: Ok"), ResultOf(PS, Moves->Swap) == EGFHotbarMoveResult::Ok);
		Test->TestTrue(TEXT("Clear: Ok"), ResultOf(PS, Moves->Clear) == EGFHotbarMoveResult::Ok);
		Test->TestTrue(TEXT("Block: Ok"), ResultOf(PS, Moves->Block) == EGFHotbarMoveResult::Ok);

		const AGFPlayerState* OnServer = ClientOnServer();
		Test->TestTrue(TEXT("Server bar is arranged"), OnServer && OnServer->Progress.Hotbar.bArranged);
		Test->TestTrue(TEXT("Server bar matches"), OnServer && OnServer->GetHotbarSlots() == Expected());
	});
	WaitUntil(Ctx, TEXT("client bar replicates"), 10.f, []()
	{
		const AGFCharacter* C = ClientCharacter();
		const AGFPlayerState* PS = ClientPS();
		return C && PS && PS->GetHotbarSlots() == Expected() && C->GetHotbarIds() == Expected();
	});
	Do(Ctx, [Ctx]()
	{
		FAutomationTestBase* Test = Ctx->Test;
		AGFCharacter* C = ClientCharacter();
		const AGFPlayerState* PS = ClientPS();
		if (!C || !PS)
		{
			Ctx->bFailed = true;
			return;
		}
		Test->TestEqual(TEXT("Item counts stay real on the bar"), PS->GetHotbarCount(TEXT("Amber")), int64(2));
		C->SelectHotbarSlot(4);
		Test->TestEqual(TEXT("Slot 5 places dirt"), C->SelectedBlock, FName(TEXT("Dirt")));
		Test->TestTrue(TEXT("Holding a block"), C->GetAimHeldKind() == EGFAimHeldKind::Block);
		Test->TestFalse(TEXT("Not a hand item"), C->IsHandItemSelected());
		// The wheel walks every slot, empty ones too. Slots 4 and 6 are empty, so they are bare hands.
		C->CycleBlock(1);
		Test->TestEqual(TEXT("Wheel up from dirt stops on the empty slot 6"), C->GetSelectedHotbarSlot(), 5);
		Test->TestEqual(TEXT("An empty slot is bare hands"), C->SelectedBlock, GFAim::FistsId());
		C->CycleBlock(-1);
		C->CycleBlock(-1);
		Test->TestEqual(TEXT("Wheel down past dirt stops on the empty slot 4"), C->GetSelectedHotbarSlot(), 3);
		Test->TestEqual(TEXT("Still bare hands"), C->SelectedBlock, GFAim::FistsId());
		C->SelectHotbarSlot(8);
		Test->TestTrue(TEXT("Slot 9 is in hand"), C->IsHandItemSelected());
		Test->TestTrue(TEXT("A hand item aims like fists"), C->GetAimHeldKind() == EGFAimHeldKind::Fists);
	});
	WaitUntil(Ctx, TEXT("server sees amber in hand"), 10.f, []()
	{
		const AGFPlayerState* OnServer = ClientOnServer();
		return OnServer && OnServer->HeldItem == TEXT("Amber");
	});
	// Spending the amber clears its slot. The rest of the arranged bar stays where the player put it.
	Do(Ctx, []()
	{
		if (AGFPlayerState* OnServer = ClientOnServer())
		{
			OnServer->Inventory->RemoveItem(TEXT("Amber"), 2);
		}
	});
	WaitUntil(Ctx, TEXT("client bag empties and the bar drops the amber"), 10.f, []()
	{
		const AGFPlayerState* PS = ClientPS();
		return PS && PS->Inventory && PS->Inventory->GetCount(TEXT("Amber")) == 0 && PS->GetHotbarSlots() == SpentExpected();
	});
	Do(Ctx, [Ctx]()
	{
		const AGFPlayerState* OnServer = ClientOnServer();
		Ctx->Test->TestTrue(TEXT("The server bar cleared the spent slot"), OnServer && OnServer->GetHotbarSlots() == SpentExpected());
		Ctx->Test->TestTrue(TEXT("Still arranged"), OnServer && OnServer->Progress.Hotbar.bArranged);
	});
	WaitUntil(Ctx, TEXT("slot 9 is bare hands again"), 10.f, []()
	{
		const AGFCharacter* C = ClientCharacter();
		return C && C->GetSelectedHotbarSlot() == 8 && !C->IsHandItemSelected() && C->SelectedBlock == GFAim::FistsId();
	});
	Stop(Ctx);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetInventoryBagTest, "GFGame.Net.Inventory.Bag", GFNetInventoryDetail::Flags)
bool FGFNetInventoryBagTest::RunTest(const FString&)
{
	using namespace GFNetInventoryDetail;
	FCtxRef Ctx = StartOnMap(this, TEXT("/Game/Maps/L_World_Factions_A"), 1, 120.f);
	WaitUntil(Ctx, TEXT("local controller with a pawn and the HUD"), 20.f, []()
	{
		const APlayerController* PC = LocalPC(ServerWorld());
		return PC && PC->GetPawn() && Cast<AGFHUD>(PC->GetHUD()) && PC->GetPlayerState<AGFPlayerState>();
	});
	Wait(Ctx, 0.5f);
	Do(Ctx, [Ctx]()
	{
		FAutomationTestBase* Test = Ctx->Test;
		APlayerController* PC = LocalPC(ServerWorld());
		AGFHUD* HUD = PC ? Cast<AGFHUD>(PC->GetHUD()) : nullptr;
		AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
		AGFGameState* GS = GameState(ServerWorld());
		if (!HUD || !PS || !GS)
		{
			Test->AddError(TEXT("No HUD, player state or game state"));
			Ctx->bFailed = true;
			return;
		}
		GS->bFreeBuild = false;
		PS->Inventory->SetInventory(FGFInventory());
		PS->Inventory->AddItem(TEXT("Amber"), 1);
		PS->Inventory->AddItem(TEXT("Dirt"), 3);

		HUD->ToggleInventory();
		Test->TestTrue(TEXT("Tab opens the bag"), HUD->IsInventoryOpen());
		Test->TestTrue(TEXT("Bag: cursor shown"), PC->bShowMouseCursor);
		Test->TestTrue(TEXT("Bag: click events on"), PC->bEnableClickEvents);
		if (const APawn* Pawn = PC->GetPawn())
		{
			Test->TestFalse(TEXT("Bag: pawn input held"), Pawn->InputEnabled());
		}

		const TArray<FGFInventoryEntry> Entries = HUD->GetInventoryEntries();
		bool bAmber = false;
		for (const FGFInventoryEntry& Entry : Entries)
		{
			bAmber |= Entry.Id == TEXT("Amber") && Entry.Tab == EGFInventoryTab::Crystals && Entry.Count == 1;
		}
		Test->TestTrue(TEXT("The bag lists the amber under Crystals"), bAmber);

		HUD->NotifyHitBoxClick(TEXT("GFInv_Tab_2"));
		Test->TestTrue(TEXT("Tab click switches the tab"), HUD->GetInventoryTab() == EGFInventoryTab::Crystals);

		// Drag slot 9 (Fists in the default bar) onto slot 1.
		const TArray<FName> Before = PS->GetHotbarSlots();
		HUD->NotifyHitBoxClick(TEXT("GFInv_Slot_8"));
		Test->TestTrue(TEXT("Picked up slot 9"), HUD->GetInventoryCursor().IsHolding());
		HUD->NotifyHitBoxRelease(TEXT("GFInv_Slot_0"));
		const TArray<FName> After = PS->GetHotbarSlots();
		Test->TestTrue(TEXT("Host move applies at once"), ResultOf(PS, HUD->GetLastHotbarRequest()) == EGFHotbarMoveResult::Ok);
		Test->TestTrue(TEXT("Drag swapped slots 1 and 9"), After.Num() == FGFHotbarRules::SlotCount && Before.Num() == FGFHotbarRules::SlotCount
			&& After[0] == Before[8] && After[8] == Before[0]);

		HUD->NotifyHitBoxClick(TEXT("GFInv_Close"));
		Test->TestFalse(TEXT("Close button closes the bag"), HUD->IsInventoryOpen());
		Test->TestFalse(TEXT("Closed: cursor hidden"), PC->bShowMouseCursor);
		if (const APawn* Pawn = PC->GetPawn())
		{
			Test->TestTrue(TEXT("Closed: pawn input back"), Pawn->InputEnabled());
		}

		HUD->OpenInventory();
		HUD->SetHudModal(EGFHudModalEvent::Escape);
		Test->TestFalse(TEXT("Escape closes the bag"), HUD->IsInventoryOpen());
		Test->TestFalse(TEXT("Escape from the bag does not pause"), HUD->IsPauseMenuOpen());
	});
	Stop(Ctx);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHotbarSaveRoundTripTest, "GFGame.Save.Hotbar.RoundTrip", GFNetInventoryDetail::Flags)
bool FGFHotbarSaveRoundTripTest::RunTest(const FString&)
{
	FGFCharacterData Data;
	Data.bInitialized = true;
	Data.PlayerKey = TEXT("P");
	Data.Progress.Hotbar.bArranged = true;
	Data.Progress.Hotbar.Slots = GFNetInventoryDetail::Expected();

	UGFCharacterSave* Save = NewObject<UGFCharacterSave>();
	Save->Character = Data;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Save"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	const UGFCharacterSave* Back = Cast<UGFCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Load"), Back))
	{
		return false;
	}
	TestTrue(TEXT("Arranged survives"), Back->Character.Progress.Hotbar.bArranged);
	TestTrue(TEXT("Slots survive in order, empties included"), Back->Character.Progress.Hotbar.Slots == GFNetInventoryDetail::Expected());

	UGFCharacterSave* Old = NewObject<UGFCharacterSave>();
	Old->Character.bInitialized = true;
	TArray<uint8> OldBytes;
	TestTrue(TEXT("Save without a bar"), UGameplayStatics::SaveGameToMemory(Old, OldBytes));
	const UGFCharacterSave* OldBack = Cast<UGFCharacterSave>(UGameplayStatics::LoadGameFromMemory(OldBytes));
	if (TestNotNull(TEXT("Load without a bar"), OldBack))
	{
		TestFalse(TEXT("No bar loads unarranged, so the automatic fill shows"), OldBack->Character.Progress.Hotbar.bArranged);
		const TArray<FName> Default = { TEXT("Dirt") };
		TestTrue(TEXT("And resolves to the default"), FGFHotbarRules::Resolve(OldBack->Character.Progress.Hotbar, Default)[0] == TEXT("Dirt"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetHotbarSaveTest, "GFGame.Net.Inventory.SaveRoundTrip", GFNetInventoryDetail::Flags)
bool FGFNetHotbarSaveTest::RunTest(const FString&)
{
	using namespace GFNetInventoryDetail;
	FCtxRef Ctx = Start(this, 1);
	WaitUntil(Ctx, TEXT("host player state"), 20.f, []() { return LocalPlayerState(ServerWorld()) != nullptr; });
	Do(Ctx, [Ctx]()
	{
		FAutomationTestBase* Test = Ctx->Test;
		AGFPlayerState* PS = LocalPlayerState(ServerWorld());
		AGFGameState* GS = GameState(ServerWorld());
		if (!PS || !GS)
		{
			Ctx->bFailed = true;
			return;
		}
		GS->bFreeBuild = false;
		PS->Inventory->SetInventory(FGFInventory());
		PS->Inventory->AddItem(TEXT("Dirt"), 3);
		PS->Inventory->AddItem(TEXT("Amber"), 1);
		PS->Progress.Hotbar = FGFHotbarLayout();
		Test->TestTrue(TEXT("Assign dirt"), PS->ServerApplyHotbarMove(FGFHotbarMove::Assign(4, TEXT("Dirt"))) == EGFHotbarMoveResult::Ok);
		Test->TestTrue(TEXT("Assign amber"), PS->ServerApplyHotbarMove(FGFHotbarMove::Assign(8, TEXT("Amber"))) == EGFHotbarMoveResult::Ok);
		const TArray<FName> Arranged = PS->GetHotbarSlots();

		UGFCharacterSave* Save = NewObject<UGFCharacterSave>();
		Save->Character = PS->BuildCharacterData();
		TArray<uint8> Bytes;
		Test->TestTrue(TEXT("Save the character"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
		const UGFCharacterSave* Back = Cast<UGFCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
		if (!Back)
		{
			Test->AddError(TEXT("Load failed"));
			return;
		}
		PS->Progress.Hotbar = FGFHotbarLayout();
		Test->TestFalse(TEXT("Wiped"), PS->Progress.Hotbar.bArranged);
		PS->ApplyCharacterData(Back->Character);
		Test->TestTrue(TEXT("Loaded bar is arranged"), PS->Progress.Hotbar.bArranged);
		Test->TestTrue(TEXT("Loaded bar matches what was saved"), PS->GetHotbarSlots() == Arranged);
		Test->TestEqual(TEXT("Amber still in slot 9"), PS->GetHotbarSlots()[8], FName(TEXT("Amber")));
	});
	Stop(Ctx);
	return true;
}

#endif
