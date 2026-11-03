// A witnessed crystal break hires a wild dino onto the thief, and the client sees that target.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.Theft.HostileOnThief;Quit" -unattended -nullrhi

#include "GFNetTestUtils.h"
#include "GFDino.h"
#include "GFElderBook.h"
#include "GFPlayerState.h"
#include "GFTamingComponent.h"
#include "GFTheftDirector.h"
#include "GFTownNodes.h"
#include "GFTownsfolkRules.h"
#include "GFVillager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetTheft
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	void Plant(AActor* Body, const FVector& At)
	{
		if (!Body)
		{
			return;
		}
		Body->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Move = Body->FindComponentByClass<UCharacterMovementComponent>())
		{
			Move->GravityScale = 0.f;
			Move->StopMovementImmediately();
		}
	}

	AGFVillager* SpawnFolk(UWorld* World, const FVector& At, FName TownId, EGFTownsfolkRole Role)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFVillager* Folk = World->SpawnActor<AGFVillager>(AGFVillager::StaticClass(), At, FRotator::ZeroRotator, Params);
		if (!Folk)
		{
			return nullptr;
		}
		Plant(Folk, At);
		FGFTownsfolkSpawnParams Spawn;
		Spawn.TownId = TownId;
		Spawn.Role = Role;
		Spawn.TownCenter = FVector::ZeroVector;
		Spawn.TownRadius = 5000.f;
		Spawn.WalkSpeed = 400.f;
		Spawn.StopDistance = 80.f;
		Spawn.IdleMin = 30.f;
		Spawn.IdleMax = 30.f;
		Folk->InitTownsfolk(Spawn);
		Plant(Folk, At);
		return Folk;
	}

	bool AnyHostileAt(UWorld* World, const FString& Key)
	{
		if (!World)
		{
			return false;
		}
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			if (It->Taming && It->Taming->GetTameState() == EGFTameState::Hostile && It->Taming->GetTargetKey() == Key)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetTheftHostileOnThiefTest, "GFGame.Net.Theft.HostileOnThief", GFNetTheft::Flags)
bool FGFNetTheftHostileOnThiefTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetTheft;

	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> ThiefKey = MakeShared<FString>();

	Do(Ctx, [this, ThiefKey]()
	{
		UWorld* World = ServerWorld();
		const AGFPlayerState* Host = World ? LocalPlayerState(World) : nullptr;
		if (!TestNotNull(TEXT("Server"), World) || !TestNotNull(TEXT("Host"), Host))
		{
			return;
		}
		*ThiefKey = Host->PlayerKey;

		bool bHasDirector = false;
		for (TActorIterator<AGFTheftDirector> It(World); It; ++It)
		{
			bHasDirector = true;
		}
		if (!bHasDirector)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<AGFTheftDirector>(AGFTheftDirector::StaticClass(), FTransform::Identity, Params);
		}

		const FVector Crystal(0.f, 0.f, 500.f);
		AGFVillager* Villager = SpawnFolk(World, FVector(200.f, 0.f, 500.f), TEXT("Rime"), EGFTownsfolkRole::Villager);
		AGFVillager* Elder = SpawnFolk(World, FVector(260.f, 0.f, 500.f), TEXT("Rime"), EGFTownsfolkRole::Elder);
		if (!TestNotNull(TEXT("Villager"), Villager) || !TestNotNull(TEXT("Elder"), Elder))
		{
			return;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), FVector(900.f, 0.f, 500.f), FRotator::ZeroRotator, Params);
		if (!TestNotNull(TEXT("Dino"), Dino))
		{
			return;
		}
		Dino->bAlwaysRelevant = true;
		Dino->FinishSpawning(FTransform(FRotator::ZeroRotator, FVector(900.f, 0.f, 500.f)));
		Dino->SetSpecies(TEXT("Nipper"));
		Plant(Dino, FVector(900.f, 0.f, 500.f));

		TArray<FString> Keys;
		Keys.Add(*ThiefKey);
		FGFTownNodeEvents::OnCrystalTaken().Broadcast(World, TEXT("RimeCrystal"), Crystal, Keys);
	});

	// A wild dino can turn on the host by itself, so also wait for the elder's hire post.
	WaitUntil(Ctx, TEXT("the elder hired and a dino is hostile at the thief on the server and the client"), 20.f, [ThiefKey]()
	{
		bool bHire = false;
		if (const UGFElderBookSubsystem* Book = UGFElderBookSubsystem::Get(ServerWorld()))
		{
			for (const FGFElderPost& Post : Book->GetBook().ChannelPosts(EGFElderChannel::RimeBook))
			{
				bHire |= Post.Kind == EGFElderKind::Hire;
			}
		}
		return bHire
			&& AnyHostileAt(ServerWorld(), *ThiefKey)
			&& ClientWorlds().Num() > 0
			&& AnyHostileAt(ClientWorlds()[0], *ThiefKey);
	});

	Do(Ctx, [this, ThiefKey]()
	{
		TestTrue(TEXT("Server hire is on the thief"), AnyHostileAt(ServerWorld(), *ThiefKey));
		if (ClientWorlds().Num() > 0)
		{
			TestTrue(TEXT("Client sees the hire"), AnyHostileAt(ClientWorlds()[0], *ThiefKey));
		}
		bool bTheft = false;
		bool bHire = false;
		if (const UGFElderBookSubsystem* Book = UGFElderBookSubsystem::Get(ServerWorld()))
		{
			for (const FGFElderPost& Post : Book->GetBook().ChannelPosts(EGFElderChannel::RimeBook))
			{
				bTheft |= Post.Kind == EGFElderKind::Theft;
				bHire |= Post.Kind == EGFElderKind::Hire;
			}
		}
		TestTrue(TEXT("RimeBook has the theft"), bTheft);
		TestTrue(TEXT("RimeBook has the hire"), bHire);
	});

	Stop(Ctx);
	return true;
}

#endif
