#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFGardenTypes.h"
#include "GFGardenPlot.generated.h"

class UDataTable;
class UStaticMeshComponent;
struct FGFInventory;

UENUM(BlueprintType)
enum class EGFPlotState : uint8
{
	Empty,
	Growing,
	Harvestable
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGFOnPlotStageChanged, AGFGardenPlot*, Plot, int32, NewStage);

/**
 * One 1x1-block garden plot. Does not tick itself: UGFGardenSubsystem advances every plot once per frame.
 */
UCLASS()
class GFGARDEN_API AGFGardenPlot : public AActor
{
	GENERATED_BODY()

public:
	AGFGardenPlot();

	/** Rows: FGFPlantRow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Garden")
	TObjectPtr<UDataTable> PlantTable;

	UFUNCTION(BlueprintCallable, Category = "GF|Garden")
	bool Plant(FName PlantId);

	/** Advance growth by DeltaSeconds of in-game time. */
	UFUNCTION(BlueprintCallable, Category = "GF|Garden")
	void AdvanceGrowth(float DeltaSeconds);

	/** Adds the yield to Inventory and clears the plot. Returns false if not harvestable. */
	bool Harvest(FGFInventory& Inventory, FRandomStream& Rng);

	UFUNCTION(BlueprintPure, Category = "GF|Garden")
	EGFPlotState GetState() const;

	UFUNCTION(BlueprintPure, Category = "GF|Garden")
	int32 GetStage() const { return Stage; }

	UFUNCTION(BlueprintPure, Category = "GF|Garden")
	FName GetPlantId() const { return PlantId; }

	FGFPlotSaveData ToSaveData() const;
	void FromSaveData(const FGFPlotSaveData& Data);

	/** Test hook: lets tests inject a row without a DataTable asset. */
	void SetPlantRowOverride(const FGFPlantRow& Row) { RowOverride = Row; bHasRowOverride = true; }

	UPROPERTY(BlueprintAssignable, Category = "GF|Garden")
	FGFOnPlotStageChanged OnStageChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	const FGFPlantRow* GetRow() const;
	void UpdateStageVisual();

	UPROPERTY(VisibleAnywhere, Category = "GF|Garden")
	TObjectPtr<UStaticMeshComponent> SoilMesh;

	UPROPERTY(VisibleAnywhere, Category = "GF|Garden")
	TObjectPtr<UStaticMeshComponent> CropMesh;

	UPROPERTY(VisibleAnywhere, SaveGame, Category = "GF|Garden")
	FName PlantId;

	UPROPERTY(VisibleAnywhere, SaveGame, Category = "GF|Garden")
	float ElapsedSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "GF|Garden")
	int32 Stage = 0;

	FGFPlantRow RowOverride;
	bool bHasRowOverride = false;
};
