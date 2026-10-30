#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFGardenSubsystem.generated.h"

class AGFGardenPlot;

/** One tick for all crops (PLAN.md section 4), instead of per-plot ticks. In-game time only (Q9 open). */
UCLASS()
class GFGARDEN_API UGFGardenSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterPlot(AGFGardenPlot* Plot);
	void UnregisterPlot(AGFGardenPlot* Plot);
	const TArray<TWeakObjectPtr<AGFGardenPlot>>& GetPlots() const { return Plots; }

	/** Multiplier for testing and debug (e.g. 60 = a minute per second). */
	UPROPERTY(BlueprintReadWrite, Category = "GF|Garden")
	float GrowthRate = 1.f;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TArray<TWeakObjectPtr<AGFGardenPlot>> Plots;
};
