// Block paint on the hub's voxel blocks (AGFPaintVolume + AGFVoxelWorld's paint overlay) in a bare standalone world:
// painted cells are drawn with their own tinted mesh, repaints move them, and the overlay follows the paint session.
// Removal: a removed or replaced (FillLayers) block, or a grid reset by ClearAll/LoadData, loses its paint, and unused
// finishes are freed.
// Save: paint is part of the world save (version 6); older saves load with no paint.
#include "Misc/AutomationTest.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GFPaintVolume.h"
#include "GFSaveGame.h"
#include "GFSaveSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "GFVoxelWorld.h"
#include "VoxelFinish.h"
#include "VoxelFinishSession.h"
#include "VoxelFinishTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFPaintDisplayTest
{
	static const FLinearColor Red(0.9f, 0.1f, 0.1f);
	static const FLinearColor Blue(0.1f, 0.3f, 0.9f);
	static const FName Grass(TEXT("Grass"));

	static FFinish Solid(const FLinearColor& C)
	{
		FFinish F;
		F.PatternId = VoxelFinish::SolidPatternId;
		F.ColorA = C;
		F.ColorB = FLinearColor::White;
		return F;
	}

	/** A bare game world with a 16x16x8 voxel world (one Grass layer) and a paint volume that paints for free. */
	struct FWorld
	{
		UWorld* World = nullptr;
		AGFVoxelWorld* Voxel = nullptr;
		AGFPaintVolume* Paint = nullptr;
		APlayerState* Payer = nullptr;

		bool Create(FAutomationTestBase& Test)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFPaintDisplayWorld"));
			if (!World)
			{
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			FActorSpawnParameters Params;
			Params.bDeferConstruction = true;
			Voxel = World->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform::Identity, Params);
			if (Voxel)
			{
				Voxel->PlotSizeInBlocks = FIntVector(16, 16, 8);
				Voxel->StarterGroundLayers = 1;
				Voxel->GroundBlock = Grass;
				Voxel->FinishSpawning(FTransform::Identity);
			}
			Paint = World->SpawnActor<AGFPaintVolume>();
			Payer = World->SpawnActor<APlayerState>();
			if (Paint)
			{
				Paint->SetCanAffordAndSpend([](const FFinish&, APlayerState*) { return true; });
			}
			return Test.TestNotNull(TEXT("Voxel world"), Voxel) && Test.TestNotNull(TEXT("Paint volume"), Paint) && Test.TestNotNull(TEXT("Payer"), Payer);
		}

		/** Pushes paint to the overlay and rebuilds meshes, as the next ticks would. */
		void Flush() const
		{
			Paint->FlushPaintDisplay();
			Voxel->FlushMeshes();
		}

		int32 Drawn(const FIntVector& Cell, bool bPainted, const FLinearColor& Color = FLinearColor::Black) const
		{
			return Voxel->CountDrawnInstances(FGFVoxelGrid::CellToChunk(Cell), Voxel->GetBlock(Cell), bPainted, Color);
		}

		void Destroy()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPaintDisplayTest, "GFGame.Paint.Display", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFPaintDisplayTest::RunTest(const FString&)
{
	using namespace GFPaintDisplayTest;
	FWorld W;
	if (!W.Create(*this))
	{
		W.Destroy();
		return false;
	}
	const FIntVector A(3, 3, 0);
	const FIntVector B(4, 3, 0);
	const FIntVector Chunk = FGFVoxelGrid::CellToChunk(A);
	auto Res = [](EVoxelFinishPaintResult R) { return static_cast<int32>(R); };
	const int32 Applied = Res(EVoxelFinishPaintResult::Applied);
	TestTrue(TEXT("The volume finds the voxel world without a game state"), W.Paint->FindVoxelWorld() == W.Voxel);
	FVector Center;
	TestTrue(TEXT("A ground block is a paint target"), W.Paint->ResolveTarget(A, Center));
	W.Flush();
	TestEqual(TEXT("Nothing painted yet"), W.Voxel->NumPaintedCells(), 0);
	TestEqual(TEXT("The whole layer is drawn unpainted"), W.Voxel->CountDrawnInstances(Chunk, Grass, false), 16 * 16);

	// Paint one block red.
	TestEqual(TEXT("Paint applied"), Res(W.Paint->AuthorityPaint(A, Solid(Red), W.Payer)), Applied);
	W.Flush();
	FLinearColor Shown;
	TestTrue(TEXT("The painted cell is in the overlay"), W.Voxel->GetCellPaint(A, Shown));
	TestTrue(TEXT("...in the finish's colour"), Shown.Equals(Red, 0.01f));
	TestFalse(TEXT("Its neighbour is not"), W.Voxel->GetCellPaint(B, Shown));
	TestEqual(TEXT("One block drawn red"), W.Drawn(A, true, Red), 1);
	TestEqual(TEXT("The rest stay unpainted"), W.Voxel->CountDrawnInstances(Chunk, Grass, false), 16 * 16 - 1);

	UMaterialInterface* Plain = W.Voxel->GetDrawnMaterial(Chunk, Grass, false);
	UMaterialInstanceDynamic* Painted = Cast<UMaterialInstanceDynamic>(W.Voxel->GetDrawnMaterial(Chunk, Grass, true, Red));
	TestNotNull(TEXT("The painted mesh has its own material instance"), Painted);
	TestTrue(TEXT("...not the block's own material"), Painted != Plain);
	if (Painted)
	{
		FLinearColor Param = FLinearColor::Black;
		TestTrue(TEXT("The paint colour parameter is set"), Painted->GetVectorParameterValue(FHashedMaterialParameterInfo(AGFVoxelWorld::PaintColorParameter), Param));
		TestTrue(TEXT("...to the paint colour"), Param.Equals(Red, 0.01f));
	}

	// Two blocks in the same colour share one mesh; a repaint moves the block to the new colour's mesh.
	TestEqual(TEXT("Second block painted red"), Res(W.Paint->AuthorityPaint(B, Solid(Red), W.Payer)), Applied);
	W.Flush();
	TestEqual(TEXT("Both red blocks in one mesh"), W.Voxel->CountDrawnInstances(Chunk, Grass, true, Red), 2);
	TestEqual(TEXT("Repaint applied"), Res(W.Paint->AuthorityPaint(A, Solid(Blue), W.Payer)), Applied);
	W.Flush();
	TestEqual(TEXT("One red block left"), W.Voxel->CountDrawnInstances(Chunk, Grass, true, Red), 1);
	TestEqual(TEXT("The repainted block is drawn blue"), W.Drawn(A, true, Blue), 1);
	TestEqual(TEXT("Unpainted count unchanged"), W.Voxel->CountDrawnInstances(Chunk, Grass, false), 16 * 16 - 2);

	// A pattern shows its first colour times the tint.
	FFinish Striped = Solid(Red);
	Striped.PatternId = 3;
	Striped.ColorB = Blue;
	Striped.Tint = FLinearColor(0.5f, 1.f, 1.f);
	TestEqual(TEXT("Pattern applied"), Res(W.Paint->AuthorityPaint(FIntVector(5, 3, 0), Striped, W.Payer)), Applied);
	W.Flush();
	TestTrue(TEXT("Pattern drawn as ColorA x Tint"), W.Voxel->GetCellPaint(FIntVector(5, 3, 0), Shown) && Shown.Equals(FLinearColor(0.45f, 0.1f, 0.1f), 0.01f));

	// The overlay is display only: the grid and its hash do not change.
	TestEqual(TEXT("Grid still one full layer"), W.Voxel->GetGrid().CountSolid(), 16 * 16);

	TestEqual(TEXT("Three painted blocks in the session"), W.Paint->GetSession().GetGrid().NumPainted(), 3);
	TestEqual(TEXT("...and three in the overlay"), W.Voxel->NumPaintedCells(), 3);

	W.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPaintRemovalTest, "GFGame.Paint.Removal", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFPaintRemovalTest::RunTest(const FString&)
{
	using namespace GFPaintDisplayTest;
	FWorld W;
	if (!W.Create(*this))
	{
		W.Destroy();
		return false;
	}
	const FIntVector A(3, 3, 0);
	const FIntVector B(4, 3, 0);
	const FIntVector C(5, 3, 0);
	auto Res = [](EVoxelFinishPaintResult R) { return static_cast<int32>(R); };
	const int32 Applied = Res(EVoxelFinishPaintResult::Applied);
	const FVoxelFinishSession& Session = W.Paint->GetSession();
	W.Flush(); // the volume finds the voxel world and listens to it

	TestEqual(TEXT("A red"), Res(W.Paint->AuthorityPaint(A, Solid(Red), W.Payer)), Applied);
	TestEqual(TEXT("B red"), Res(W.Paint->AuthorityPaint(B, Solid(Red), W.Payer)), Applied);
	TestEqual(TEXT("C blue"), Res(W.Paint->AuthorityPaint(C, Solid(Blue), W.Payer)), Applied);
	W.Flush();
	TestEqual(TEXT("Three painted"), W.Voxel->NumPaintedCells(), 3);
	TestEqual(TEXT("Two finishes in the palette"), Session.GetPalette().NumOccupied(), 2);

	// Removing a block unpaints its cell; the finish stays while another block uses it.
	TestEqual(TEXT("A removed"), W.Voxel->RemoveBlock(A), Grass);
	TestEqual(TEXT("A's cell unpainted in the session"), Session.GetGrid().GetFinishId(A), static_cast<uint16>(0));
	W.Flush();
	FLinearColor Shown;
	TestFalse(TEXT("...and in the overlay"), W.Voxel->GetCellPaint(A, Shown));
	TestEqual(TEXT("B still painted"), Session.GetGrid().GetFinishId(B) != 0, true);
	TestTrue(TEXT("Red is still in the palette (B uses it)"), Session.GetPalette().FindExisting(Solid(Red)) != 0);

	// The last red block goes: red's palette slot is freed.
	TestEqual(TEXT("B removed"), W.Voxel->RemoveBlock(B), Grass);
	W.Flush();
	TestEqual(TEXT("Red's slot is freed"), Session.GetPalette().FindExisting(Solid(Red)), static_cast<uint16>(0));
	TestEqual(TEXT("One finish left"), Session.GetPalette().NumOccupied(), 1);
	TestEqual(TEXT("Only C painted"), Session.GetGrid().NumPainted(), 1);
	TestEqual(TEXT("...in the overlay too"), W.Voxel->NumPaintedCells(), 1);

	// A block placed on a cleared cell is unpainted.
	TestTrue(TEXT("A placed again"), W.Voxel->PlaceBlock(A, Grass));
	W.Flush();
	TestEqual(TEXT("New block A is unpainted"), Session.GetGrid().GetFinishId(A), static_cast<uint16>(0));
	TestFalse(TEXT("...and not drawn painted"), W.Voxel->GetCellPaint(A, Shown));
	TestEqual(TEXT("...drawn with the plain mesh"), W.Drawn(A, false), 16 * 16 - 2);

	// LoadData: cells that are no longer solid lose their paint, the rest keep it.
	TestEqual(TEXT("A red again"), Res(W.Paint->AuthorityPaint(A, Solid(Red), W.Payer)), Applied);
	FGFVoxelGrid Edited = W.Voxel->GetGrid();
	Edited.Set(C, NAME_None);
	TestTrue(TEXT("Grid loaded"), W.Voxel->LoadData(Edited.ToSaveData()));
	W.Flush();
	TestEqual(TEXT("LoadData: C (now air) unpainted"), Session.GetGrid().GetFinishId(C), static_cast<uint16>(0));
	TestTrue(TEXT("LoadData: A (still solid) keeps its paint"), Session.GetGrid().GetFinishId(A) != 0);
	TestEqual(TEXT("LoadData: the overlay follows"), W.Voxel->NumPaintedCells(), 1);
	TestEqual(TEXT("LoadData: blue's slot is freed"), Session.GetPalette().FindExisting(Solid(Blue)), static_cast<uint16>(0));

	// FillLayers: replaced blocks lose their paint; blocks above the filled layers keep theirs.
	const FIntVector High(6, 6, 2);
	TestTrue(TEXT("A block above the ground"), W.Voxel->PlaceBlock(High, Grass));
	TestEqual(TEXT("High painted blue"), Res(W.Paint->AuthorityPaint(High, Solid(Blue), W.Payer)), Applied);
	W.Flush();
	TestTrue(TEXT("A is painted before the fill"), Session.GetGrid().GetFinishId(A) != 0);
	W.Voxel->FillLayers(2, Grass);
	W.Flush();
	TestEqual(TEXT("FillLayers: the replaced block A is unpainted"), Session.GetGrid().GetFinishId(A), static_cast<uint16>(0));
	TestFalse(TEXT("FillLayers: ...and not drawn painted"), W.Voxel->GetCellPaint(A, Shown));
	TestTrue(TEXT("FillLayers: the block above keeps its paint"), Session.GetGrid().GetFinishId(High) != 0);
	TestEqual(TEXT("FillLayers: only that block is painted"), Session.GetGrid().NumPainted(), 1);
	TestEqual(TEXT("FillLayers: red's slot is freed"), Session.GetPalette().FindExisting(Solid(Red)), static_cast<uint16>(0));
	TestEqual(TEXT("FillLayers: two full layers plus the high block"), W.Voxel->GetGrid().CountSolid(), 2 * 16 * 16 + 1);

	// ClearAll: everything is unpainted and the palette is empty.
	W.Voxel->ClearAll();
	W.Flush();
	TestEqual(TEXT("ClearAll: nothing painted"), Session.GetGrid().NumPainted(), 0);
	TestEqual(TEXT("ClearAll: empty palette"), Session.GetPalette().NumOccupied(), 0);
	TestEqual(TEXT("ClearAll: empty overlay"), W.Voxel->NumPaintedCells(), 0);

	W.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPaintSaveTest, "GFGame.Paint.Save", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFPaintSaveTest::RunTest(const FString&)
{
	using namespace GFPaintDisplayTest;
	FWorld W;
	if (!W.Create(*this))
	{
		W.Destroy();
		return false;
	}
	const FIntVector A(3, 3, 0);
	const FIntVector B(4, 3, 0);
	auto Res = [](EVoxelFinishPaintResult R) { return static_cast<int32>(R); };
	const int32 Applied = Res(EVoxelFinishPaintResult::Applied);
	const FVoxelFinishSession& Session = W.Paint->GetSession();
	FFinish Striped = Solid(Blue);
	Striped.PatternId = 3;
	Striped.ColorB = Red;
	auto FinishAt = [&Session](const FIntVector& Cell) { return Session.GetPalette().Find(Session.GetGrid().GetFinishId(Cell)); };
	W.Flush();
	TestEqual(TEXT("A red"), Res(W.Paint->AuthorityPaint(A, Solid(Red), W.Payer)), Applied);
	TestEqual(TEXT("B striped"), Res(W.Paint->AuthorityPaint(B, Striped, W.Payer)), Applied);

	// Capture and write through the real serializer (SaveGameToMemory only writes SaveGame properties).
	UGFSaveGame* Save = UGFSaveSubsystem::CaptureWorld(W.World);
	TestEqual(TEXT("New saves are version 6"), Save->SaveVersion, static_cast<int32>(EGFSaveVersion::BlockPaint));
	TestEqual(TEXT("BlockPaint is the latest version"), static_cast<int32>(EGFSaveVersion::Latest), static_cast<int32>(EGFSaveVersion::BlockPaint));
	TestEqual(TEXT("Two painted cells captured"), Save->Paint.Cells.Num(), 2);
	TArray<uint8> Bytes;
	TestTrue(TEXT("SaveGameToMemory"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	UGFSaveGame* Back = Cast<UGFSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Loaded save"), Back))
	{
		W.Destroy();
		return false;
	}
	TestEqual(TEXT("Painted cells survive the serializer"), Back->Paint.Cells.Num(), 2);
	TestEqual(TEXT("...with their ids"), Back->Paint.CellFinishIds.Num(), 2);
	TestEqual(TEXT("Both finishes survive"), Back->Paint.SlotFinishes.Num(), 2);
	TestEqual(TEXT("Paint save version survives"), Back->Paint.Version, FVoxelFinishSaveData::SparseVersion);
	TestTrue(TEXT("Migrate v6"), UGFSaveSubsystem::Migrate(Back));

	// Wipe the world, then load: blocks and paint come back.
	W.Voxel->ClearAll();
	W.Flush();
	TestEqual(TEXT("Wiped"), Session.GetGrid().NumPainted(), 0);
	TestTrue(TEXT("Apply v6 save"), UGFSaveSubsystem::ApplyToWorld(W.World, Back));
	W.Flush();
	TestEqual(TEXT("Blocks back"), W.Voxel->GetGrid().CountSolid(), 16 * 16);
	TestEqual(TEXT("Two painted after load"), Session.GetGrid().NumPainted(), 2);
	const FFinish* AtA = FinishAt(A);
	const FFinish* AtB = FinishAt(B);
	TestTrue(TEXT("A is red again"), AtA && AtA->CanonicallyEquals(Solid(Red)));
	TestTrue(TEXT("B has the striped finish again"), AtB && AtB->CanonicallyEquals(Striped));
	FLinearColor Shown;
	TestTrue(TEXT("A drawn red after load"), W.Voxel->GetCellPaint(A, Shown) && Shown.Equals(Red, 0.01f));
	TestEqual(TEXT("Overlay has both"), W.Voxel->NumPaintedCells(), 2);
	TestEqual(TEXT("A drawn with the red mesh"), W.Drawn(A, true, Red), 1);

	// A save whose blocks no longer include A: A's paint is dropped, B's stays.
	FGFVoxelGrid WithoutA = W.Voxel->GetGrid();
	WithoutA.Set(A, NAME_None);
	UGFSaveGame* Edited = UGFSaveSubsystem::CaptureWorld(W.World);
	Edited->Voxels = WithoutA.ToSaveData();
	TestTrue(TEXT("Apply save without A's block"), UGFSaveSubsystem::ApplyToWorld(W.World, Edited));
	W.Flush();
	TestEqual(TEXT("A (air) loads unpainted"), Session.GetGrid().GetFinishId(A), static_cast<uint16>(0));
	TestTrue(TEXT("B keeps its paint"), FinishAt(B) != nullptr);
	TestEqual(TEXT("Red's slot is not kept for nothing"), Session.GetPalette().FindExisting(Solid(Red)), static_cast<uint16>(0));

	// A version 5 save (no paint field): it loads, and the world has no paint afterwards.
	UGFSaveGame* V5 = NewObject<UGFSaveGame>();
	V5->SaveVersion = static_cast<int32>(EGFSaveVersion::ModeStates);
	V5->Voxels = Back->Voxels;
	TArray<uint8> V5Bytes;
	TestTrue(TEXT("v5 SaveGameToMemory"), UGameplayStatics::SaveGameToMemory(V5, V5Bytes));
	UGFSaveGame* V5Back = Cast<UGFSaveGame>(UGameplayStatics::LoadGameFromMemory(V5Bytes));
	if (TestNotNull(TEXT("Loaded v5 save"), V5Back))
	{
		TestTrue(TEXT("Migrate v5"), UGFSaveSubsystem::Migrate(V5Back));
		TestEqual(TEXT("v5 migrated to latest"), V5Back->SaveVersion, static_cast<int32>(EGFSaveVersion::Latest));
		TestTrue(TEXT("Apply v5 save"), UGFSaveSubsystem::ApplyToWorld(W.World, V5Back));
		W.Flush();
		TestEqual(TEXT("v5: blocks load"), W.Voxel->GetGrid().CountSolid(), 16 * 16);
		TestEqual(TEXT("v5: no paint"), Session.GetGrid().NumPainted(), 0);
		TestEqual(TEXT("v5: empty palette"), Session.GetPalette().NumOccupied(), 0);
		TestEqual(TEXT("v5: empty overlay"), W.Voxel->NumPaintedCells(), 0);
		TestEqual(TEXT("v5: every block drawn plain"), W.Voxel->CountDrawnInstances(FGFVoxelGrid::CellToChunk(A), Grass, false), 16 * 16);
	}

	W.Destroy();
	return true;
}

#endif
