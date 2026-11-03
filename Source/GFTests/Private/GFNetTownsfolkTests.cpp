// A client press talks to a townsfolk on the server, and a press out of reach does not.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.Townsfolk.Interact;Quit" -unattended -nullrhi

#include "GFNetTestUtils.h"
#include "GFCharacter.h"
#include "GFElder.h"
#include "GFPlayerState.h"
#include "GFTownsfolkRules.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetTownsfolk
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

	/** The guest's pawn as the server sees it. That is the body the server reach check uses. */
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

	AGFElder* FindElder(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AGFElder> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetTownsfolkInteractTest, "GFGame.Net.Townsfolk.Interact", GFNetTownsfolk::Flags)
bool FGFNetTownsfolkInteractTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetTownsfolk;

	FCtxRef Ctx = Start(this, 2);
	TSharedRef<int32> Hits = MakeShared<int32>(0);
	TSharedRef<TWeakObjectPtr<AGFElder>> ServerElder = MakeShared<TWeakObjectPtr<AGFElder>>();

	WaitUntil(Ctx, TEXT("client pawn"), 30.f, []()
	{
		return GFNetTest::ClientWorlds().Num() > 0 && LocalBody(GFNetTest::ClientWorlds()[0]) && GuestOnServer();
	});
	Do(Ctx, [Ctx, Hits, ServerElder]()
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
		const FVector At(2400.f, 2400.f, 200.f);
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
		SpawnParams.TalkSeconds = 4.f;
		Elder->InitTownsfolk(SpawnParams);
		*ServerElder = Elder;
		Elder->OnInteract.AddLambda([Hits](AActor*, APawn*)
		{
			++*Hits;
		});
	});
	WaitUntil(Ctx, TEXT("client sees the elder"), 10.f, []()
	{
		return ClientWorlds().Num() > 0 && FindElder(ClientWorlds()[0]) != nullptr;
	});
	Do(Ctx, [Ctx, ServerElder]()
	{
		AGFElder* Elder = ServerElder->Get();
		AGFCharacter* ServerBody = GuestOnServer();
		UWorld* ClientWorld = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFCharacter* ClientBody = LocalBody(ClientWorld);
		if (!Elder || !ServerBody || !ClientBody)
		{
			Ctx->Test->AddError(TEXT("Missing elder or pawn"));
			Ctx->bFailed = true;
			return;
		}
		const FVector Beside = Elder->GetActorLocation() + FVector(80.f, 0.f, 0.f);
		Plant(ServerBody, Beside);
		Plant(ClientBody, Beside);
		ClientBody->RequestInteract();
	});
	WaitUntil(Ctx, TEXT("server hears the first press"), 5.f, [Hits]()
	{
		return *Hits >= 1;
	});
	Do(Ctx, [Ctx, ServerElder]()
	{
		AGFElder* Elder = ServerElder->Get();
		UWorld* ClientWorld = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFCharacter* ClientBody = LocalBody(ClientWorld);
		AGFCharacter* ServerBody = GuestOnServer();
		if (!Elder || !ClientBody || !ServerBody)
		{
			Ctx->Test->AddError(TEXT("Client pawn left"));
			Ctx->bFailed = true;
			return;
		}
		const FVector Beside = Elder->GetActorLocation() + FVector(80.f, 0.f, 0.f);
		Plant(ServerBody, Beside);
		Plant(ClientBody, Beside);
		ClientBody->RequestInteract();
	});
	WaitUntil(Ctx, TEXT("server hears the second press"), 5.f, [Hits]()
	{
		return *Hits >= 2;
	});
	Do(Ctx, [Ctx, ServerElder]()
	{
		AGFElder* Elder = ServerElder->Get();
		AGFCharacter* ServerBody = GuestOnServer();
		UWorld* ClientWorld = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFCharacter* ClientBody = LocalBody(ClientWorld);
		AGFElder* ClientElder = FindElder(ClientWorld);
		if (!Elder || !ServerBody || !ClientBody || !ClientElder)
		{
			Ctx->Test->AddError(TEXT("Missing actors for the out of reach press"));
			Ctx->bFailed = true;
			return;
		}
		const FVector Away = Elder->GetActorLocation() + FVector(2000.f, 0.f, 0.f);
		Plant(ServerBody, Away);
		Plant(ClientBody, Away);
		ClientBody->RequestInteract();
		ClientBody->RequestInteractWith(ClientElder);
	});
	Wait(Ctx, 1.f);
	Do(Ctx, [Ctx, Hits]()
	{
		Ctx->Test->TestEqual(TEXT("Out of reach does nothing"), *Hits, 2);
	});
	Stop(Ctx);
	return true;
}

#endif
