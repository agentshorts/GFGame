// Stairwell Call keeps a player-sized slice of Big Grumble's dig, never more than one claw can spend.
#include "GFMagicStairwell.h"
#include "GFMagicTypes.h"
#include "GFVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicStairwellTest, "GFGame.MagicMining.Stairwell", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMagicStairwellTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFMagicStair"));
	if (!TestNotNull(TEXT("World"), World) || !GEngine)
	{
		return false;
	}
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	FActorSpawnParameters Params;
	Params.bDeferConstruction = true;
	AGFVoxelWorld* Voxel = World->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform::Identity, Params);
	if (Voxel)
	{
		Voxel->PlotSizeInBlocks = FIntVector(16, 16, 8);
		Voxel->StarterGroundLayers = 0;
		Voxel->FinishSpawning(FTransform::Identity);
	}
	APawn* Pawn = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform(FVector(0.f, 0.f, 400.f)));
	const bool bVoxel = TestNotNull(TEXT("Voxel"), Voxel);
	const bool bPawn = TestNotNull(TEXT("Pawn"), Pawn);
	if (bVoxel && bPawn)
	{
		const FVector Aim(600.f, 0.f, 40.f);
		const FGFGrumbleDigResult Planned = FGFGrumbleDig::Plan(FGFMagicStairwell::MoveFor(Pawn, Aim), FGFMagicStairwell::Context(), FGFMagicStairwell::Tuning());
		const TArray<FIntVector> Kept = FGFMagicStairwell::Cells(Pawn, Aim, Voxel);
		TestTrue(TEXT("The dig plan carved something"), Planned.bDug && Planned.Cells.Num() > 0);
		TestTrue(TEXT("Stairwell Call keeps cells"), Kept.Num() > 0);
		TestTrue(TEXT("And no more than one claw's budget"), Kept.Num() <= GFMagic::CellsPerTick);
		for (const FIntVector& Cell : Kept)
		{
			TestTrue(TEXT("Every kept cell came from the plan"), Planned.Cells.Contains(Cell));
		}
	}
	if (GEngine)
	{
		GEngine->DestroyWorldContext(World);
	}
	World->DestroyWorld(false);
	return true;
}

#endif
