// A server-validated interact opens the elder menu. Hand in moves the quest and takes the items once.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.ElderMenu.TurnIn;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFCharacter.h"
#include "GFElder.h"
#include "GFHUD.h"
#include "GFInventoryComponent.h"
#include "GFPlayerController.h"
#include "GFPlayerState.h"
#include "GFQuestSubsystem.h"
#include "GFTownsfolkRules.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetElderMenu
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	AGFCharacter* LocalBody(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				return Cast<AGFCharacter>(PC->GetPawn());
			}
		}
		return nullptr;
	}

	AGFPlayerController* LocalGFPC(UWorld* World)
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
				return Cast<AGFPlayerController>(PC);
			}
		}
		return nullptr;
	}

	AGFCharacter* GuestOnServer()
	{
		using namespace GFNetTest;
		if (ClientWorlds().Num() == 0)
		{
			return nullptr;
		}
		const AGFPlayerState* ClientPS = LocalPlayerState(ClientWorlds()[0]);
		AGFPlayerState* ServerPS = (ClientPS && ServerWorld()) ? PlayerStateByKey(ServerWorld(), ClientPS->PlayerKey) : nullptr;
		return ServerPS ? Cast<AGFCharacter>(ServerPS->GetPawn()) : nullptr;
	}

	AGFPlayerController* GuestController()
	{
		AGFCharacter* Body = GuestOnServer();
		return Body ? Cast<AGFPlayerController>(Body->GetController()) : nullptr;
	}

	void Plant(AGFCharacter* Body, const FVector& At)
	{
		if (!Body)
		{
			return;
		}
		Body->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Move = Body->GetCharacterMovement())
		{
			Move->GravityScale = 0.f;
			Move->StopMovementImmediately();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetElderMenuTurnInTest, "GFGame.Net.ElderMenu.TurnIn", GFNetElderMenu::Flags)
bool FGFNetElderMenuTurnInTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetElderMenu;

	FCtxRef Ctx = Start(this, 2);
	TSharedRef<TWeakObjectPtr<AGFElder>> ServerElder = MakeShared<TWeakObjectPtr<AGFElder>>();

	WaitUntil(Ctx, TEXT("client pawn"), 30.f, []()
	{
		return ClientWorlds().Num() > 0 && LocalBody(ClientWorlds()[0]) && GuestOnServer() && LocalGFPC(ClientWorlds()[0]);
	});
	Do(Ctx, [Ctx, ServerElder]()
	{
		UWorld* Server = ServerWorld();
		if (!Server)
		{
			Ctx->Test->AddError(TEXT("No server world"));
			Ctx->bFailed = true;
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector At(2600.f, 2600.f, 200.f);
		AGFElder* Elder = Server->SpawnActor<AGFElder>(AGFElder::StaticClass(), At, FRotator::ZeroRotator, Params);
		if (!Elder)
		{
			Ctx->Test->AddError(TEXT("Elder did not spawn"));
			Ctx->bFailed = true;
			return;
		}
		FGFTownsfolkSpawnParams SpawnParams;
		SpawnParams.TownId = TEXT("Rime");
		SpawnParams.Role = EGFTownsfolkRole::Elder;
		SpawnParams.TownCenter = At;
		SpawnParams.TownRadius = 1600.f;
		SpawnParams.Reach = FGFTownsfolkRules::InteractReachCm;
		SpawnParams.TalkSeconds = 30.f;
		Elder->InitTownsfolk(SpawnParams);
		*ServerElder = Elder;
	});
	WaitUntil(Ctx, TEXT("client sees the elder"), 10.f, []()
	{
		if (ClientWorlds().Num() == 0)
		{
			return false;
		}
		for (TActorIterator<AGFElder> It(ClientWorlds()[0]); It; ++It)
		{
			return true;
		}
		return false;
	});
	Do(Ctx, [Ctx, ServerElder]()
	{
		AGFElder* Elder = ServerElder->Get();
		AGFCharacter* ServerBody = GuestOnServer();
		UWorld* ClientWorld = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFCharacter* ClientBody = LocalBody(ClientWorld);
		AGFPlayerController* ClientPC = LocalGFPC(ClientWorld);
		AGFHUD* HUD = ClientPC ? Cast<AGFHUD>(ClientPC->GetHUD()) : nullptr;
		if (!Elder || !ServerBody || !ClientBody || !HUD)
		{
			Ctx->Test->AddError(TEXT("Missing elder, pawn, or HUD"));
			Ctx->bFailed = true;
			return;
		}
		Ctx->Test->TestTrue(TEXT("Menu starts closed"), HUD->GetHudModal() == EGFHudModal::Game);
		const FVector Away = Elder->GetActorLocation() + FVector(2000.f, 0.f, 0.f);
		Plant(ServerBody, Away);
		Plant(ClientBody, Away);
		ClientBody->RequestInteract();
	});
	Wait(Ctx, 1.f);
	Do(Ctx, [Ctx, ServerElder]()
	{
		AGFPlayerController* ServerPC = GuestController();
		AGFPlayerController* ClientPC = ClientWorlds().Num() > 0 ? LocalGFPC(ClientWorlds()[0]) : nullptr;
		AGFHUD* HUD = ClientPC ? Cast<AGFHUD>(ClientPC->GetHUD()) : nullptr;
		if (!ServerPC || !HUD)
		{
			Ctx->Test->AddError(TEXT("Missing controller after the far press"));
			Ctx->bFailed = true;
			return;
		}
		Ctx->Test->TestFalse(TEXT("Out of reach does not open a session"), ServerPC->IsElderMenuSessionOpen());
		Ctx->Test->TestTrue(TEXT("Out of reach leaves the menu closed"), HUD->GetHudModal() == EGFHudModal::Game);

		AGFElder* Elder = ServerElder->Get();
		AGFCharacter* ServerBody = GuestOnServer();
		AGFCharacter* ClientBody = LocalBody(ClientWorlds()[0]);
		if (!Elder || !ServerBody || !ClientBody)
		{
			Ctx->Test->AddError(TEXT("Missing bodies for the real press"));
			Ctx->bFailed = true;
			return;
		}
		const FVector Beside = Elder->GetActorLocation() + FVector(80.f, 0.f, 0.f);
		Plant(ServerBody, Beside);
		Plant(ClientBody, Beside);
		ClientBody->RequestInteract();
	});
	WaitUntil(Ctx, TEXT("client elder menu is open"), 10.f, []()
	{
		AGFPlayerController* ClientPC = ClientWorlds().Num() > 0 ? LocalGFPC(ClientWorlds()[0]) : nullptr;
		const AGFHUD* HUD = ClientPC ? Cast<AGFHUD>(ClientPC->GetHUD()) : nullptr;
		AGFPlayerController* ServerPC = GuestController();
		return HUD && HUD->GetHudModal() == EGFHudModal::Elder && ServerPC && ServerPC->IsElderMenuSessionOpen();
	});
	Do(Ctx, [Ctx]()
	{
		AGFPlayerController* ClientPC = LocalGFPC(ClientWorlds()[0]);
		AGFHUD* HUD = ClientPC ? Cast<AGFHUD>(ClientPC->GetHUD()) : nullptr;
		APawn* Pawn = ClientPC ? ClientPC->GetPawn() : nullptr;
		if (!HUD || !Pawn)
		{
			Ctx->Test->AddError(TEXT("Menu HUD disappeared"));
			Ctx->bFailed = true;
			return;
		}
		Ctx->Test->TestTrue(TEXT("Cursor is free"), ClientPC->bShowMouseCursor);
		Ctx->Test->TestTrue(TEXT("Clicks are on"), ClientPC->bEnableClickEvents);
		Ctx->Test->TestFalse(TEXT("Pawn input is held"), Pawn->InputEnabled());
		HUD->HandleEscape();
		Ctx->Test->TestTrue(TEXT("Escape closes the menu"), HUD->GetHudModal() == EGFHudModal::Game);
		Ctx->Test->TestFalse(TEXT("Cursor hides again"), ClientPC->bShowMouseCursor);
		Ctx->Test->TestTrue(TEXT("Pawn input returns"), Pawn->InputEnabled());
	});
	WaitUntil(Ctx, TEXT("server session closes with the menu"), 5.f, []()
	{
		AGFPlayerController* ServerPC = GuestController();
		return ServerPC && !ServerPC->IsElderMenuSessionOpen();
	});
	Do(Ctx, [ServerElder]()
	{
		AGFElder* Elder = ServerElder->Get();
		AGFCharacter* ServerBody = GuestOnServer();
		AGFCharacter* ClientBody = ClientWorlds().Num() > 0 ? LocalBody(ClientWorlds()[0]) : nullptr;
		if (!Elder || !ServerBody || !ClientBody)
		{
			return;
		}
		const FVector Beside = Elder->GetActorLocation() + FVector(80.f, 0.f, 0.f);
		Plant(ServerBody, Beside);
		Plant(ClientBody, Beside);
		ClientBody->RequestInteract();
	});
	WaitUntil(Ctx, TEXT("menu opens again"), 10.f, []()
	{
		AGFPlayerController* ServerPC = GuestController();
		return ServerPC && ServerPC->IsElderMenuSessionOpen();
	});
	Do(Ctx, []()
	{
		if (AGFPlayerController* ClientPC = LocalGFPC(ClientWorlds()[0]))
		{
			ClientPC->RequestElderAccept();
		}
	});
	WaitUntil(Ctx, TEXT("accept reaches the owning client"), 10.f, []()
	{
		const AGFPlayerState* Owner = ClientWorlds().Num() > 0 ? LocalPlayerState(ClientWorlds()[0]) : nullptr;
		return Owner && Owner->QuestStep == EGFQuestStep::HuntHide && Owner->FactionId == FName(TEXT("Rime"));
	});
	Do(Ctx, []()
	{
		AGFCharacter* ServerBody = GuestOnServer();
		AGFPlayerState* ServerPS = ServerBody ? ServerBody->GetPlayerState<AGFPlayerState>() : nullptr;
		if (ServerPS && ServerPS->Inventory)
		{
			ServerPS->Inventory->AddItem(TEXT("Hide"), 8);
		}
		if (AGFPlayerController* ClientPC = ClientWorlds().Num() > 0 ? LocalGFPC(ClientWorlds()[0]) : nullptr)
		{
			ClientPC->RequestElderHandIn();
		}
	});
	WaitUntil(Ctx, TEXT("hand in advances the step and keeps the spare hide"), 10.f, []()
	{
		const AGFPlayerState* Owner = ClientWorlds().Num() > 0 ? LocalPlayerState(ClientWorlds()[0]) : nullptr;
		AGFCharacter* ServerBody = GuestOnServer();
		AGFPlayerState* ServerPS = ServerBody ? ServerBody->GetPlayerState<AGFPlayerState>() : nullptr;
		return Owner && ServerPS && ServerPS->Inventory
			&& Owner->QuestStep == EGFQuestStep::HuntSkins
			&& ServerPS->Inventory->GetCount(TEXT("Hide")) == 2;
	});
	Do(Ctx, []()
	{
		if (AGFPlayerController* ClientPC = ClientWorlds().Num() > 0 ? LocalGFPC(ClientWorlds()[0]) : nullptr)
		{
			ClientPC->RequestElderHandIn();
		}
	});
	Wait(Ctx, 1.f);
	Do(Ctx, [Ctx]()
	{
		const AGFPlayerState* Owner = LocalPlayerState(ClientWorlds()[0]);
		AGFCharacter* ServerBody = GuestOnServer();
		AGFPlayerState* ServerPS = ServerBody ? ServerBody->GetPlayerState<AGFPlayerState>() : nullptr;
		UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(ServerWorld());
		if (!Owner || !ServerPS || !ServerPS->Inventory || !Quest)
		{
			Ctx->Test->AddError(TEXT("Missing state after the second hand in"));
			Ctx->bFailed = true;
			return;
		}
		Ctx->Test->TestTrue(TEXT("Step stays on skins"), Owner->QuestStep == EGFQuestStep::HuntSkins);
		Ctx->Test->TestEqual(TEXT("Spare hide is still there"), ServerPS->Inventory->GetCount(TEXT("Hide")), 2);
		const FGFQuestProgress* Line = Quest->FindProgress(ServerPS->PlayerKey);
		Ctx->Test->TestTrue(TEXT("Server line matches"), Line && Line->Step == EGFQuestStep::HuntSkins && Line->FactionId == FName(TEXT("Rime")));
	});
	Stop(Ctx);
	return true;
}

#endif
