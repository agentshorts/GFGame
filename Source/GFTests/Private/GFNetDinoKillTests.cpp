// A wild kill pays the killer on the server, and the owning client sees that stack arrive.
// A second hit on the same body does not pay again.

#include "GFNetTestUtils.h"
#include "GFDino.h"
#include "GFDinoKill.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "GFRewards.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace
{
	struct FKillDropRun
	{
		FString KillerKey;
		int32 Scales = 0;
		int32 Skin = 0;
		int32 Hide = 0;
		TWeakObjectPtr<AGFDino> Dino;
	};

	void ClearWildDinos(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		UClass* DirectorClass = FindObject<UClass>(nullptr, TEXT("/Script/GFGame.GFWildDinoDirector"));
		TArray<AActor*> Doomed;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if ((DirectorClass && It->IsA(DirectorClass)) || It->IsA<AGFDino>())
			{
				Doomed.Add(*It);
			}
		}
		for (AActor* Actor : Doomed)
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}
	}

	int32 BareSeed()
	{
		for (int32 Seed = 1; Seed < 64; ++Seed)
		{
			if (FGFRewards::CountOf(FGFDinoKill::Roll(TEXT("Raptor"), Seed), TEXT("Skin_Raptor")) == 0)
			{
				return Seed;
			}
		}
		return 1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetDinoKillDropTest, "GFGame.Net.Dino.KillDrop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetDinoKillDropTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FKillDropRun> Run = MakeShared<FKillDropRun>();
	Do(Ctx, [this, Run]()
	{
		UWorld* Server = ServerWorld();
		const TArray<UWorld*> Clients = ClientWorlds();
		const AGFPlayerState* Client = Clients.Num() > 0 ? LocalPlayerState(Clients[0]) : nullptr;
		if (!TestNotNull(TEXT("Client player"), Client))
		{
			return;
		}
		AGFPlayerState* Killer = PlayerStateByKey(Server, Client->PlayerKey);
		if (!TestNotNull(TEXT("Killer on the server"), Killer) || !TestNotNull(TEXT("Inventory"), Killer->Inventory.Get()))
		{
			return;
		}
		Run->KillerKey = Killer->PlayerKey;
		ClearWildDinos(Server);
		const APawn* Pawn = Killer->GetPawn();
		const FVector At = Pawn ? Pawn->GetActorLocation() + FVector(180.f, 0.f, 0.f) : FVector(400.f, 0.f, 200.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFDino* Dino = Server->SpawnActor<AGFDino>(AGFDino::StaticClass(), At, FRotator::ZeroRotator, Params);
		if (!TestNotNull(TEXT("Raptor"), Dino) || !TestNotNull(TEXT("Taming"), Dino->Taming.Get()))
		{
			return;
		}
		Dino->SetSpecies(TEXT("Raptor"));
		Dino->SetDropSeed(BareSeed());
		if (UCharacterMovementComponent* Move = Dino->GetCharacterMovement())
		{
			Move->DisableMovement();
			Move->GravityScale = 0.f;
		}
		Run->Dino = Dino;
		const int32 ScalesBefore = Killer->Inventory->GetCount(TEXT("Scales"));
		const int32 HideBefore = Killer->Inventory->GetCount(TEXT("Hide"));
		AController* By = Killer->GetPlayerController();
		AActor* Causer = Pawn ? static_cast<AActor*>(Killer->GetPawn()) : Killer;
		UGameplayStatics::ApplyDamage(Dino, 1000.f, By, Causer, nullptr);
		UGameplayStatics::ApplyDamage(Dino, 1000.f, By, Causer, nullptr);
		Run->Scales = Killer->Inventory->GetCount(TEXT("Scales"));
		Run->Skin = Killer->Inventory->GetCount(TEXT("Skin_Raptor"));
		Run->Hide = Killer->Inventory->GetCount(TEXT("Hide"));
		TestEqual(TEXT("Server paid two Scales"), Run->Scales, ScalesBefore + 2);
		TestEqual(TEXT("Server paid no skin for this seed"), Run->Skin, 0);
		TestEqual(TEXT("Server paid no Hide"), Run->Hide, HideBefore);
		TestEqual(TEXT("The body is downed, not paid twice"), static_cast<int32>(Dino->GetLife()), static_cast<int32>(EGFDinoLife::Downed));
	});
	WaitUntil(Ctx, TEXT("client sees the Scales"), 10.f, [Run]()
	{
		const TArray<UWorld*> Clients = ClientWorlds();
		if (Clients.Num() == 0)
		{
			return false;
		}
		const AGFPlayerState* Client = LocalPlayerState(Clients[0]);
		return Client && Client->Inventory
			&& Client->Inventory->GetCount(TEXT("Scales")) == Run->Scales
			&& Client->Inventory->GetCount(TEXT("Skin_Raptor")) == Run->Skin
			&& Client->Inventory->GetCount(TEXT("Hide")) == Run->Hide;
	});
	Stop(Ctx);
	return true;
}

#endif
