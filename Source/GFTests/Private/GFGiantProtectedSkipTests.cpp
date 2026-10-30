// Big Grumble must not see or shove a pawn that carries GF.FactionProtected.
// A rider in flight gets that tag. GFGiant does not include the ferry module.

#include "GFGiantDirector.h"
#include "GFGiantEventComponent.h"
#include "GFCharacter.h"
#include "GFFerryRiderComponent.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFProtectedActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFGiantSkipTest
{
	struct FWorldScope
	{
		UWorld* World = nullptr;
		explicit FWorldScope(UWorld* InWorld) : World(InWorld) {}
		~FWorldScope()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static void Link(APlayerController* PC, AGFPlayerState* PS, APawn* Pawn)
	{
		PS->SetOwner(PC);
		PC->PlayerState = PS;
		PC->SetPawn(Pawn);
		Pawn->SetPlayerState(PS);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantProtectedSkipTest, "GFGame.Giant.ProtectedSkip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantProtectedSkipTest::RunTest(const FString&)
{
	using namespace GFGiantSkipTest;
	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFGiantProtectedSkip"));
	FWorldScope Scope(World);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay() && World->GetWorldSettings())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}
	AGFGameState* GS = World->SpawnActor<AGFGameState>();
	if (GS)
	{
		World->SetGameState(GS);
	}
	if (!TestNotNull(TEXT("Game state"), GS))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UGFGiantEventComponent* Event = AGFGiantDirector::GetEvent(World, true);
	if (!TestNotNull(TEXT("Giant event"), Event))
	{
		return false;
	}

	auto Add = [&](const TCHAR* Key, int32 Id, const FVector& At, bool bTag, bool bFly) -> APawn*
	{
		AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
		APlayerController* PC = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity, Params);
		APawn* Pawn = nullptr;
		if (bFly)
		{
			AGFCharacter* Character = World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), At, FRotator::ZeroRotator, Params);
			Pawn = Character;
			if (Character && Character->FerryRider)
			{
				Character->FerryRider->BeginFlightProtection();
			}
		}
		else
		{
			Pawn = World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), At, FRotator::ZeroRotator, Params);
			if (Pawn && bTag)
			{
				Pawn->Tags.Add(FGFProtectedActor::Tag);
			}
		}
		if (!PS || !PC || !Pawn)
		{
			return nullptr;
		}
		PS->PlayerKey = Key;
		PS->SetPlayerId(Id);
		if (GS->PlayerArray.Find(PS) == INDEX_NONE)
		{
			GS->AddPlayerState(PS);
		}
		Link(PC, PS, Pawn);
		return Pawn;
	};

	APawn* Open = Add(TEXT("Open"), 81, FVector(100.f, 0.f, 0.f), false, false);
	APawn* Tagged = Add(TEXT("Tagged"), 82, FVector(200.f, 0.f, 0.f), true, false);
	APawn* Rider = Add(TEXT("Rider"), 83, FVector(300.f, 0.f, 0.f), false, true);
	if (!TestNotNull(TEXT("Open pawn"), Open) || !TestNotNull(TEXT("Tagged pawn"), Tagged) || !TestNotNull(TEXT("Rider"), Rider))
	{
		return false;
	}
	TestTrue(TEXT("Tagged carries the protection tag"), FGFProtectedActor::Is(Tagged));
	TestTrue(TEXT("Rider in flight carries the protection tag"), FGFProtectedActor::Is(Rider));

	const TArray<FGFGiantPlayerView> Views = Event->GatherPlayers();
	const auto Has = [&Views](const TCHAR* Key)
	{
		return Views.ContainsByPredicate([Key](const FGFGiantPlayerView& View) { return View.Key == Key; });
	};
	TestTrue(TEXT("Open pawn is visible"), Has(TEXT("Open")));
	TestFalse(TEXT("Tagged pawn is skipped"), Has(TEXT("Tagged")));
	TestFalse(TEXT("Rider in flight is skipped"), Has(TEXT("Rider")));

	const FVector OpenAt = Open->GetActorLocation();
	const FVector TaggedAt = Tagged->GetActorLocation();
	const FVector RiderAt = Rider->GetActorLocation();
	const FVector Push(200.f, 0.f, 0.f);
	Event->ApplyShove(Open, Push);
	TestEqual(TEXT("Open shove counted"), Event->ShovesApplied, 1);
	TestTrue(TEXT("Open pawn moved"), Open->GetActorLocation().Equals(OpenAt + Push, 1.f));

	Event->ApplyShove(Tagged, Push);
	Event->ApplyShove(Rider, Push);
	TestEqual(TEXT("Protected shoves do not count"), Event->ShovesApplied, 1);
	TestTrue(TEXT("Tagged pawn stayed"), Tagged->GetActorLocation().Equals(TaggedAt, 1.f));
	TestTrue(TEXT("Rider stayed"), Rider->GetActorLocation().Equals(RiderAt, 1.f));
	return true;
}

#endif
