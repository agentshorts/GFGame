#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFFashionTypes.h"
#include "GFFashionGateSubsystem.generated.h"

/** Gate C: fashion show (M3). Wardrobe and theme lists for the UI; runs live on AGFRunwayStage. */
UCLASS()
class GFFASHION_API UGFFashionGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Fashion"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Runway"); }
	virtual FText GetDisplayName() const override { return FText::FromString(TEXT("Runway")); }

	UFUNCTION(BlueprintPure, Category = "GF|Fashion")
	TArray<FGFFashionTheme> GetThemes() const { return FGFFashionCatalog::Themes(); }

	UFUNCTION(BlueprintPure, Category = "GF|Fashion")
	TArray<FGFFashionPiece> GetWardrobe() const { return FGFFashionCatalog::Pieces(); }
};
