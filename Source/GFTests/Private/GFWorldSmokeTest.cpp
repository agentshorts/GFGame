// World-level smoke test: a transient game world can host the hub voxel grid and a garden plot.
#include "Misc/AutomationTest.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GFVoxelWorld.h"
#include "GFGardenPlot.h"
#include "GFGardenTypes.h"
#include "GFItemTypes.h"
#include "GFSaveGame.h"
#include "GFSaveSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWorldSmokeTest, "GFGame.Smoke.HubWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFWorldSmokeTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFSmokeWorld"));
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	// No game mode in this bare world, so flag begun-play ourselves; actors spawned below then get BeginPlay.
	if (!World->HasBegunPlay() && World->GetWorldSettings())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	AGFVoxelWorld* Voxel = World->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform::Identity, Params);
	if (TestNotNull(TEXT("Voxel world spawned"), Voxel))
	{
		Voxel->PlotSizeInBlocks = FIntVector(16, 16, 8);
		Voxel->StarterGroundLayers = 1;
		Voxel->FinishSpawning(FTransform::Identity);
		TestEqual(TEXT("Starter ground layer"), Voxel->GetGrid().CountSolid(), 16 * 16);
		TestTrue(TEXT("Place on top"), Voxel->PlaceBlock(FIntVector(3, 3, 1), TEXT("Grass")));
		TestFalse(TEXT("Can't place into a filled cell"), Voxel->PlaceBlock(FIntVector(3, 3, 1), TEXT("Grass")));
		TestTrue(TEXT("Remove returns id"), Voxel->RemoveBlock(FIntVector(3, 3, 1)) == FName(TEXT("Grass")));
		TestTrue(TEXT("Cell world pos (bottom centre)"), Voxel->CellToWorld(FIntVector(1, 2, 3)).Equals(FVector(150.f, 250.f, 300.f)));
		TestTrue(TEXT("World to cell"), Voxel->WorldToCell(FVector(150.f, 250.f, 301.f)) == FIntVector(1, 2, 3));
		FIntVector HitCell, Adjacent;
		Voxel->HitToCells(FVector(350.f, 350.f, 100.f), FVector::UpVector, HitCell, Adjacent);
		TestTrue(TEXT("Hit top face -> hit cell"), HitCell == FIntVector(3, 3, 0));
		TestTrue(TEXT("Hit top face -> place above"), Adjacent == FIntVector(3, 3, 1));
	}

	AGFGardenPlot* Plot = World->SpawnActor<AGFGardenPlot>();
	if (TestNotNull(TEXT("Plot spawned"), Plot))
	{
		FGFPlantRow Row;
		Row.StageSeconds = { 10.f, 10.f };
		Row.YieldItem = TEXT("Fruit");
		Row.YieldMin = 2;
		Row.YieldMax = 2;
		Row.RareChance = 0.f;
		Plot->SetPlantRowOverride(Row);
		TestTrue(TEXT("Plant"), Plot->Plant(TEXT("Berry")));
		Plot->AdvanceGrowth(15.f);
		TestEqual(TEXT("Mid stage"), Plot->GetStage(), 1);
		Plot->AdvanceGrowth(10.f);
		TestTrue(TEXT("Harvestable"), Plot->GetState() == EGFPlotState::Harvestable);
		FGFInventory Inv;
		FRandomStream Rng(7);
		TestTrue(TEXT("Harvest"), Plot->Harvest(Inv, Rng));
		TestEqual(TEXT("Yield"), Inv.GetCount(TEXT("Fruit")), 2);
		TestTrue(TEXT("Empty after harvest"), Plot->GetState() == EGFPlotState::Empty);
	}

	// Save subsystem capture/apply on this world (no game instance: voxels + plots only).
	if (Voxel)
	{
		Voxel->PlaceBlock(FIntVector(5, 5, 1), TEXT("Stone"));
		UGFSaveGame* Save = UGFSaveSubsystem::CaptureWorld(World);
		const uint32 Before = Voxel->GetGrid().GetContentHash();
		Voxel->ClearAll();
		TestEqual(TEXT("Cleared"), Voxel->GetGrid().CountSolid(), 0);
		TestTrue(TEXT("Apply save"), UGFSaveSubsystem::ApplyToWorld(World, Save));
		TestEqual(TEXT("Voxel hash restored"), static_cast<int64>(Voxel->GetGrid().GetContentHash()), static_cast<int64>(Before));
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
