#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "GFItemTypes.h"
#include "GFRecipeTypes.generated.h"

/** Row for DT_Recipes. Row name = recipe id. Executed by UGFCraftingSubsystem (GFBuild). */
USTRUCT(BlueprintType)
struct GFCORE_API FGFRecipeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	TArray<FGFItemStack> Inputs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FGFItemStack Output;

	/** Crafting station id (e.g. Bench, Loom, Kiln). None = craft anywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FName Station;

	/** Empty = unlocked from the start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FGameplayTag UnlockTag;
};
