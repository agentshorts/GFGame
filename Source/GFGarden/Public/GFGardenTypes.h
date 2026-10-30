#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFGardenTypes.generated.h"

class UStaticMesh;

/** Row for DT_Plants. Row name = plant id. */
USTRUCT(BlueprintType)
struct GFGARDEN_API FGFPlantRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant")
	FName SeedItem;

	/** One mesh per growth stage; the last stage is harvestable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant")
	TArray<TSoftObjectPtr<UStaticMesh>> StageMeshes;

	/** Seconds spent in each stage before the next (one entry per non-final stage). In-game time only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant")
	TArray<float> StageSeconds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant")
	FName YieldItem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant", meta = (ClampMin = "0"))
	int32 YieldMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant", meta = (ClampMin = "0"))
	int32 YieldMax = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant")
	FName RareItem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plant", meta = (ClampMin = "0", ClampMax = "1"))
	float RareChance = 0.1f;

	/** Number of stages = StageSeconds.Num() + 1. */
	int32 GetNumStages() const { return StageSeconds.Num() + 1; }
	float GetTotalGrowSeconds() const;
	/** Pure: stage index for elapsed growing time. */
	int32 StageForElapsed(float ElapsedSeconds) const;
};

/** Saved state of one plot. */
USTRUCT(BlueprintType)
struct GFGARDEN_API FGFPlotSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FTransform Transform;
	UPROPERTY(SaveGame) FName PlantId;
	UPROPERTY(SaveGame) float ElapsedSeconds = 0.f;
};
