#include "GFGardenPlot.h"
#include "GFGardenSubsystem.h"
#include "GFItemTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

AGFGardenPlot::AGFGardenPlot()
{
	PrimaryActorTick.bCanEverTick = false;
	SoilMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Soil"));
	RootComponent = SoilMesh;
	CropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Crop"));
	CropMesh->SetupAttachment(SoilMesh);
	CropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AGFGardenPlot::BeginPlay()
{
	Super::BeginPlay();
	if (UGFGardenSubsystem* Sub = GetWorld()->GetSubsystem<UGFGardenSubsystem>())
	{
		Sub->RegisterPlot(this);
	}
	UpdateStageVisual();
}

void AGFGardenPlot::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWorld* World = GetWorld())
	{
		if (UGFGardenSubsystem* Sub = World->GetSubsystem<UGFGardenSubsystem>())
		{
			Sub->UnregisterPlot(this);
		}
	}
	Super::EndPlay(Reason);
}

const FGFPlantRow* AGFGardenPlot::GetRow() const
{
	if (PlantId.IsNone())
	{
		return nullptr;
	}
	if (bHasRowOverride)
	{
		return &RowOverride;
	}
	return PlantTable ? PlantTable->FindRow<FGFPlantRow>(PlantId, TEXT("GFGardenPlot"), false) : nullptr;
}

bool AGFGardenPlot::Plant(FName InPlantId)
{
	if (!PlantId.IsNone())
	{
		return false;
	}
	PlantId = InPlantId;
	if (!GetRow())
	{
		PlantId = NAME_None;
		return false;
	}
	ElapsedSeconds = 0.f;
	Stage = 0;
	UpdateStageVisual();
	OnStageChanged.Broadcast(this, Stage);
	return true;
}

void AGFGardenPlot::AdvanceGrowth(float DeltaSeconds)
{
	const FGFPlantRow* Row = GetRow();
	if (!Row || GetState() != EGFPlotState::Growing)
	{
		return;
	}
	ElapsedSeconds = FMath::Min(ElapsedSeconds + DeltaSeconds, Row->GetTotalGrowSeconds());
	const int32 NewStage = Row->StageForElapsed(ElapsedSeconds);
	if (NewStage != Stage)
	{
		Stage = NewStage;
		UpdateStageVisual();
		OnStageChanged.Broadcast(this, Stage);
	}
}

EGFPlotState AGFGardenPlot::GetState() const
{
	const FGFPlantRow* Row = GetRow();
	if (!Row)
	{
		return EGFPlotState::Empty;
	}
	return Stage >= Row->GetNumStages() - 1 ? EGFPlotState::Harvestable : EGFPlotState::Growing;
}

bool AGFGardenPlot::Harvest(FGFInventory& Inventory, FRandomStream& Rng)
{
	const FGFPlantRow* Row = GetRow();
	if (!Row || GetState() != EGFPlotState::Harvestable)
	{
		return false;
	}
	Inventory.Add(Row->YieldItem, Rng.RandRange(Row->YieldMin, FMath::Max(Row->YieldMin, Row->YieldMax)));
	if (!Row->RareItem.IsNone() && Rng.FRand() < Row->RareChance)
	{
		Inventory.Add(Row->RareItem, 1);
	}
	PlantId = NAME_None;
	ElapsedSeconds = 0.f;
	Stage = 0;
	UpdateStageVisual();
	return true;
}

FGFPlotSaveData AGFGardenPlot::ToSaveData() const
{
	FGFPlotSaveData D;
	D.Transform = GetActorTransform();
	D.PlantId = PlantId;
	D.ElapsedSeconds = ElapsedSeconds;
	return D;
}

void AGFGardenPlot::FromSaveData(const FGFPlotSaveData& Data)
{
	SetActorTransform(Data.Transform);
	PlantId = Data.PlantId;
	ElapsedSeconds = Data.ElapsedSeconds;
	const FGFPlantRow* Row = GetRow();
	Stage = Row ? Row->StageForElapsed(ElapsedSeconds) : 0;
	UpdateStageVisual();
}

void AGFGardenPlot::UpdateStageVisual()
{
	const FGFPlantRow* Row = GetRow();
	UStaticMesh* Mesh = nullptr;
	if (Row && Row->StageMeshes.IsValidIndex(Stage))
	{
		Mesh = Row->StageMeshes[Stage].LoadSynchronous();
	}
	CropMesh->SetStaticMesh(Mesh);
	CropMesh->SetVisibility(Mesh != nullptr);
}
