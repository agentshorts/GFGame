// Mapless mini-game directors used to spawn at the world origin, on top of the Rime hub.
// Get() now parks each one on its own pad.

#include "Misc/AutomationTest.h"
#include "GFArenaDirector.h"
#include "GFDepthsDirector.h"
#include "GFLabDirector.h"
#include "GFMineDirector.h"
#include "GFMiniGameCatalog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSimPadSpawnTest, "GFGame.Gates.SimPadSpawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSimPadSpawnTest::RunTest(const FString&)
{
	if (!TestNotNull(TEXT("GEngine"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFSimPadWorld"));
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	struct FCase
	{
		const TCHAR* Name;
		AActor* Actor;
		FName Mode;
	};
	const FCase Cases[] = {
		{ TEXT("Arena"), AGFArenaDirector::Get(World, true), AGFArenaDirector::ModeName },
		{ TEXT("Depths"), AGFDepthsDirector::Get(World, true), AGFDepthsDirector::ModeName },
		{ TEXT("Mine"), AGFMineDirector::Get(World, true), AGFMineDirector::ModeName },
		{ TEXT("Lab"), AGFLabDirector::Get(World, true), AGFLabDirector::ModeName },
	};
	for (const FCase& Case : Cases)
	{
		const FVector Want = FGFMiniGameCatalog::DefaultSpawn(Case.Mode);
		TestFalse(*FString::Printf(TEXT("%s pad is not the origin"), Case.Name), Want.IsNearlyZero(1.f));
		if (TestNotNull(*FString::Printf(TEXT("%s director"), Case.Name), Case.Actor))
		{
			TestTrue(*FString::Printf(TEXT("%s spawned on its pad"), Case.Name), Case.Actor->GetActorLocation().Equals(Want, 1.f));
		}
	}
	TestTrue(TEXT("Get returns the director already on the pad"), AGFArenaDirector::Get(World, true) == Cases[0].Actor);

	for (int32 i = 0; i < 4; ++i)
	{
		for (int32 j = i + 1; j < 4; ++j)
		{
			if (Cases[i].Actor && Cases[j].Actor)
			{
				TestTrue(
					*FString::Printf(TEXT("%s and %s are not stacked"), Cases[i].Name, Cases[j].Name),
					FVector::Dist2D(Cases[i].Actor->GetActorLocation(), Cases[j].Actor->GetActorLocation()) > 1000.f);
			}
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
