// Mining feel. Crack stages, cosmetic prediction, the tap window, and the one replicated cell.
#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFCrosshairPanel.h"
#include "GFGameState.h"
#include "GFMagicComponent.h"
#include "GFMagicPlayerState.h"
#include "GFMiningFeel.h"
#include "GFMiningFX.h"
#include "GFBlockIntegritySubsystem.h"
#include "GFVoxelWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags GFMiningFeelFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool Near(float A, float B, float Tol = 0.02f)
	{
		return FMath::IsNearlyEqual(A, B, Tol);
	}

	struct FFeelWorld
	{
		UWorld* World = nullptr;
		AGFVoxelWorld* Voxel = nullptr;
		UGFBlockIntegritySubsystem* Magic = nullptr;

		bool Open(FAutomationTestBase& Test, const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World || !GEngine)
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
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Voxel = World->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform::Identity, Params);
			if (Voxel)
			{
				Voxel->PlotSizeInBlocks = FIntVector(16, 16, 8);
				Voxel->StarterGroundLayers = 0;
				Voxel->FinishSpawning(FTransform::Identity);
			}
			Magic = World->GetSubsystem<UGFBlockIntegritySubsystem>();
			return Test.TestNotNull(TEXT("World"), World)
				&& Test.TestNotNull(TEXT("Voxel"), Voxel)
				&& Test.TestNotNull(TEXT("Integrity"), Magic);
		}

		~FFeelWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFeelCrackStageTest, "GFGame.MagicMining.Feel.CrackStages", GFMiningFeelFlags)
bool FGFMiningFeelCrackStageTest::RunTest(const FString&)
{
	const FGFCrackParams Intact = FGFCrackParams::FromFraction(1.f);
	TestEqual(TEXT("Full block has no crack"), Intact.Stage, 0);
	TestTrue(TEXT("Full block params are off"), Intact.Crack75 == 0.f && Intact.Crack50 == 0.f && Intact.Crack25 == 0.f);

	const FGFCrackParams Above = FGFCrackParams::FromFraction(0.76f);
	TestEqual(TEXT("Just above 75 percent is still intact"), Above.Stage, 0);

	const FGFCrackParams At75 = FGFCrackParams::FromFraction(0.75f);
	TestEqual(TEXT("75 percent is stage 1"), At75.Stage, 1);
	TestEqual(TEXT("Stage 1 sets Crack75"), At75.Crack75, 1.f);
	TestEqual(TEXT("Stage 1 leaves Crack50 off"), At75.Crack50, 0.f);
	TestEqual(TEXT("Stage 1 leaves Crack25 off"), At75.Crack25, 0.f);

	const FGFCrackParams At50 = FGFCrackParams::FromFraction(0.50f);
	TestEqual(TEXT("50 percent is stage 2"), At50.Stage, 2);
	TestEqual(TEXT("Stage 2 sets Crack50"), At50.Crack50, 1.f);
	TestEqual(TEXT("Stage 2 keeps Crack75"), At50.Crack75, 1.f);
	TestEqual(TEXT("Stage 2 leaves Crack25 off"), At50.Crack25, 0.f);

	const FGFCrackParams At25 = FGFCrackParams::FromFraction(0.25f);
	TestEqual(TEXT("25 percent is stage 3"), At25.Stage, 3);
	TestEqual(TEXT("Stage 3 sets Crack25"), At25.Crack25, 1.f);
	TestEqual(TEXT("Stage 3 keeps the earlier cracks"), At25.Crack75 + At25.Crack50, 2.f);

	const FGFCrackParams Spent = FGFCrackParams::FromFraction(0.f);
	TestEqual(TEXT("A spent block is not a crack stage"), Spent.Stage, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFeelCrackRollbackTest, "GFGame.MagicMining.Feel.CrackRollback", GFMiningFeelFlags)
bool FGFMiningFeelCrackRollbackTest::RunTest(const FString&)
{
	FGFCrackClient Crack;
	const FIntVector Cell(4, 5, 6);
	Crack.BeginTarget(false, Cell, FString());
	Crack.PredictStep(0.25f);
	Crack.PredictStep(0.25f);
	TestTrue(TEXT("Two steps lead the cracks"), Near(Crack.Shown(), 0.5f));

	FGFTargetedCellFraction Quiet;
	Quiet.Serial = 0;
	Quiet.bActive = false;
	Crack.Reconcile(Quiet, 0.2f);
	TestTrue(TEXT("No server word leaves the guess"), Near(Crack.Shown(), 0.5f));
	TestFalse(TEXT("Silence is not a rollback"), Crack.RolledBack());

	FGFTargetedCellFraction Reject;
	Reject.bActive = true;
	Reject.bTown = false;
	Reject.Cell = Cell;
	Reject.Fraction = 1.f;
	Reject.Serial = 1;
	Crack.Reconcile(Reject, 0.2f);
	TestTrue(TEXT("A rejected hit rolls the crack back"), Crack.RolledBack());
	TestTrue(TEXT("The shown fraction matches the server"), Near(Crack.Shown(), 1.f));

	Crack.BeginTarget(false, Cell, FString());
	Crack.PredictStep(0.1f);
	FGFTargetedCellFraction Close;
	Close.bActive = true;
	Close.Cell = Cell;
	Close.Fraction = 1.f;
	Close.Serial = 2;
	Crack.Reconcile(Close, 0.2f);
	TestFalse(TEXT("A small lead stays"), Crack.RolledBack());
	TestTrue(TEXT("The guess is still ahead"), Near(Crack.Shown(), 0.9f));

	FGFTargetedCellFraction Ahead;
	Ahead.bActive = true;
	Ahead.Cell = Cell;
	Ahead.Fraction = 0.7f;
	Ahead.Serial = 3;
	Crack.Reconcile(Ahead, 0.2f);
	TestTrue(TEXT("A server that got further is followed"), Near(Crack.Shown(), 0.7f));
	TestFalse(TEXT("Catching up is not a rollback"), Crack.RolledBack());

	for (int32 Step = 0; Step < 40; ++Step)
	{
		Crack.PredictStep(0.5f);
	}
	TestTrue(TEXT("Prediction stops short of a break"), Crack.Shown() >= FGFMiningFeelRules::PredictFloor - 0.001f);
	TestTrue(TEXT("The floor is the last crack, not zero"), Crack.Shown() <= FGFMiningFeelRules::PredictFloor + 0.001f);

	FGFTargetedCellFraction Clear;
	Clear.bActive = false;
	Clear.Serial = 4;
	Crack.Reconcile(Clear, 0.2f);
	TestTrue(TEXT("A server clear drops the guess"), Crack.RolledBack());
	TestTrue(TEXT("The clear does not leave a local break"), Near(Crack.Shown(), 1.f));

	Crack.BeginTarget(false, Cell, FString());
	Crack.PredictStep(0.4f);
	Crack.BeginTarget(true, FIntVector::ZeroValue, TEXT("TreeA"));
	TestTrue(TEXT("A new target starts intact"), Near(Crack.Shown(), 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFeelFieldRiseTest, "GFGame.MagicMining.Feel.FieldRise", GFMiningFeelFlags)
bool FGFMiningFeelFieldRiseTest::RunTest(const FString&)
{
	TestEqual(TEXT("The rise window is 150 ms"), FGFMiningFeelRules::DefaultFieldRiseSeconds, 0.15f);
	TestEqual(TEXT("TapSeconds matches the rise window"), GFMagic::TapSeconds, 0.15f);
	TestFalse(TEXT("149 ms stays a tap"), FGFMiningFeelRules::FieldRises(0.149f, 0.15f));
	TestTrue(TEXT("150 ms raises the field"), FGFMiningFeelRules::FieldRises(0.15f, 0.15f));
	TestFalse(TEXT("A zero threshold does not raise"), FGFMiningFeelRules::FieldRises(1.f, 0.f));
	TestEqual(TEXT("Hit-stop stays inside 40-60 ms"), FGFMiningFeelRules::ClampHitStop(0.05f), 0.05f);
	TestEqual(TEXT("A short hit-stop is raised to 40 ms"), FGFMiningFeelRules::ClampHitStop(0.01f), 0.04f);
	TestEqual(TEXT("A long hit-stop is cut to 60 ms"), FGFMiningFeelRules::ClampHitStop(1.f), 0.06f);

	const AGFCharacter* CDO = GetDefault<AGFCharacter>();
	if (!TestNotNull(TEXT("Character defaults"), CDO))
	{
		return false;
	}
	TestEqual(TEXT("Character rise default is 150 ms"), CDO->MiningFieldRiseSeconds, 0.15f);
	TestEqual(TEXT("Character hit-stop default is 50 ms"), CDO->MiningBreakHitStopSeconds, 0.05f);
	TestEqual(TEXT("Character prediction tolerance"), CDO->MiningPredictTolerance, 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFeelHitResponseTest, "GFGame.MagicMining.Feel.HitResponse", GFMiningFeelFlags)
bool FGFMiningFeelHitResponseTest::RunTest(const FString&)
{
	const FGFMiningFeelResponse Hit = FGFMiningFeelRules::ResponseFor(EGFMiningFXKind::Hit);
	TestTrue(TEXT("A hit shakes"), Hit.bShake);
	TestTrue(TEXT("A hit ticks the crosshair"), Hit.bCrosshair);
	TestFalse(TEXT("A hit does not stop"), Hit.bHitStop);

	const FGFMiningFeelResponse Crack = FGFMiningFeelRules::ResponseFor(EGFMiningFXKind::Crack);
	TestTrue(TEXT("A crack shakes"), Crack.bShake);
	TestTrue(TEXT("A crack ticks the crosshair"), Crack.bCrosshair);
	TestFalse(TEXT("A crack does not stop"), Crack.bHitStop);

	const FGFMiningFeelResponse Break = FGFMiningFeelRules::ResponseFor(EGFMiningFXKind::Break);
	TestTrue(TEXT("A break shakes"), Break.bShake);
	TestTrue(TEXT("A break ticks the crosshair"), Break.bCrosshair);
	TestTrue(TEXT("A break hit-stops"), Break.bHitStop);

	const FGFMiningFeelResponse Beam = FGFMiningFeelRules::ResponseFor(EGFMiningFXKind::Beam);
	TestFalse(TEXT("A beam does not shake"), Beam.bShake);
	TestFalse(TEXT("A beam does not tick"), Beam.bCrosshair);
	TestFalse(TEXT("A beam does not stop"), Beam.bHitStop);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFeelCrosshairTickTest, "GFGame.MagicMining.Feel.CrosshairTick", GFMiningFeelFlags)
bool FGFMiningFeelCrosshairTickTest::RunTest(const FString&)
{
	FGFCrosshairTick::Reset();
	TestTrue(TEXT("A quiet crosshair has no tick"), FGFCrosshairTick::Alpha(10.0) == 0.f);
	FGFCrosshairSettings Idle;
	const float Thickness = Idle.Thickness;
	const float Size = Idle.Size;
	GFApplyCrosshairTick(Idle, 0.f);
	TestEqual(TEXT("Alpha 0 keeps the thickness"), Idle.Thickness, Thickness);
	TestEqual(TEXT("Alpha 0 keeps the size"), Idle.Size, Size);

	FGFCrosshairTick::Pulse(10.0, 0.14f);
	TestTrue(TEXT("The tick starts full"), FGFCrosshairTick::Alpha(10.0) > 0.99f);
	TestTrue(TEXT("The tick is gone after its window"), FGFCrosshairTick::Alpha(10.2) == 0.f);
	FGFCrosshairSettings Live;
	GFApplyCrosshairTick(Live, 1.f);
	TestTrue(TEXT("A tick thickens the crosshair"), Live.Thickness > Thickness);
	TestTrue(TEXT("A tick grows the crosshair"), Live.Size > Size);
	TestTrue(TEXT("A tick turns the center dot on"), Live.bCenterDot);
	FGFCrosshairTick::Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFeelFractionLossTest, "GFGame.MagicMining.Feel.FractionLoss", GFMiningFeelFlags)
bool FGFMiningFeelFractionLossTest::RunTest(const FString&)
{
	const float Dirt = FGFMiningFeelRules::FractionLoss(TEXT("Dirt"), EGFMagicElement::Spark, 100.f, 1.f, 0.1f);
	TestTrue(TEXT("Dirt loses 10 of 50 in one step"), Near(Dirt, 0.2f, 0.001f));
	const float Log = FGFMiningFeelRules::FractionLoss(TEXT("Log"), EGFMagicElement::Spark, 100.f, 1.f, 0.1f);
	TestTrue(TEXT("Log spark uses the affinity multiplier"), Near(Log, 0.075f, 0.001f));
	TestEqual(TEXT("An empty id removes nothing"), FGFMiningFeelRules::FractionLoss(NAME_None, EGFMagicElement::Spark, 100.f, 1.f, 0.1f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFeelTargetedFractionTest, "GFGame.MagicMining.Feel.TargetedFraction", GFMiningFeelFlags)
bool FGFMiningFeelTargetedFractionTest::RunTest(const FString&)
{
	FFeelWorld W;
	if (!W.Open(*this, TEXT("GFMiningFeelFraction")))
	{
		return false;
	}
	const FIntVector Dirt(2, 2, 2);
	const FIntVector Neighbour(3, 2, 2);
	const FIntVector Stone(6, 2, 2);
	if (!TestTrue(TEXT("Dirt placed"), W.Voxel->ServerApplyEdit(Dirt, TEXT("Dirt")))
		|| !TestTrue(TEXT("Neighbour placed"), W.Voxel->ServerApplyEdit(Neighbour, TEXT("Dirt")))
		|| !TestTrue(TEXT("Stone placed"), W.Voxel->ServerApplyEdit(Stone, TEXT("Stone"))))
	{
		return false;
	}
	W.Magic->NotePlaced(Stone);

	AGFMagicPlayerState* PS = W.World->SpawnActor<AGFMagicPlayerState>();
	if (!TestNotNull(TEXT("Digger"), PS) || !TestNotNull(TEXT("Magic"), PS->Magic.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Digger");
	const FVector DirtAt = W.Voxel->CellToWorld(Dirt);
	PS->Magic->SetManualOrigin(DirtAt);
	PS->Magic->SetAim(DirtAt, DirtAt);
	PS->Magic->SetFieldHeld(true);
	W.Magic->Advance(0.1f);

	const FGFTargetedCellFraction& Rep = PS->Magic->GetTargetedFraction();
	TestTrue(TEXT("The aimed cell is active"), Rep.bActive);
	TestFalse(TEXT("A voxel is not a town node"), Rep.bTown);
	TestEqual(TEXT("Only the aimed cell is replicated"), Rep.Cell, Dirt);
	TestTrue(TEXT("The neighbour is not the replicated cell"), Rep.Cell != Neighbour);
	TestTrue(TEXT("One dirt step leaves about 80 percent"), Near(Rep.Fraction, 0.8f, 0.05f));
	TestTrue(TEXT("The publish has a serial"), Rep.Serial > 0);

	const FVector StoneAt = W.Voxel->CellToWorld(Stone);
	PS->Magic->SetManualOrigin(StoneAt);
	PS->Magic->SetAim(StoneAt, StoneAt);
	W.Magic->Advance(0.3f);
	const FGFTargetedCellFraction& Rejected = PS->Magic->GetTargetedFraction();
	TestEqual(TEXT("The spell-proof aim is the stone cell"), Rejected.Cell, Stone);
	TestTrue(TEXT("The stone is still the only cell"), Rejected.Cell != Dirt);
	TestTrue(TEXT("A placed stone does not lose integrity"), Near(Rejected.Fraction, 1.f, 0.02f));
	TestTrue(TEXT("The stone cell is still there"), W.Voxel->GetBlock(Stone) == FName(TEXT("Stone")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFeelCrackParamsTest, "GFGame.MagicMining.Feel.CrackParams", GFMiningFeelFlags)
bool FGFMiningFeelCrackParamsTest::RunTest(const FString&)
{
	FFeelWorld W;
	if (!W.Open(*this, TEXT("GFMiningFeelCrackParams")))
	{
		return false;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Cube"), Cube))
	{
		return false;
	}
	AStaticMeshActor* Actor = W.World->SpawnActor<AStaticMeshActor>(FVector(400.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Mesh actor"), Actor))
	{
		return false;
	}
	UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(Cube);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Base)
	{
		Mesh->SetMaterial(0, UMaterialInstanceDynamic::Create(Base, Mesh));
	}
	FGFMiningCrack::Apply(Mesh, 0.4f);
	FGFCrackParams Read;
	TestTrue(TEXT("Crack params can be read back"), FGFMiningCrack::Read(Mesh, Read));
	TestEqual(TEXT("40 percent is stage 2 on the mesh"), Read.Stage, 2);
	TestEqual(TEXT("Mesh Crack75"), Read.Crack75, 1.f);
	TestEqual(TEXT("Mesh Crack50"), Read.Crack50, 1.f);
	TestEqual(TEXT("Mesh Crack25 stays off"), Read.Crack25, 0.f);

	FGFMiningCrack::Apply(Mesh, 0.2f);
	TestTrue(TEXT("25 percent range reads back"), FGFMiningCrack::Read(Mesh, Read));
	TestEqual(TEXT("20 percent is stage 3"), Read.Stage, 3);
	TestEqual(TEXT("Stage 3 Crack25 is on"), Read.Crack25, 1.f);

	FGFMiningCrack::Clear(Mesh);
	TestTrue(TEXT("Clear reads back"), FGFMiningCrack::Read(Mesh, Read));
	TestEqual(TEXT("Clear drops the stage"), Read.Stage, 0);

	AGFMiningCrackMarker* Marker = W.World->SpawnActor<AGFMiningCrackMarker>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Crack marker"), Marker))
	{
		return false;
	}
	Marker->Show(FVector(10.f, 0.f, 20.f), FVector::UpVector, 0.25f);
	UStaticMeshComponent* Plate = Marker->GetPlate();
	if (!TestNotNull(TEXT("Crack plate"), Plate))
	{
		return false;
	}
	TestTrue(TEXT("The plate does not block traces"), Plate->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("The marker plate carries stage 3"), FGFMiningCrack::Read(Plate, Read) && Read.Stage == 3);
	Marker->Hide();
	TestTrue(TEXT("Hide clears the plate"), FGFMiningCrack::Read(Plate, Read) && Read.Stage == 0);
	return true;
}

#endif
