#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "GFRecipeTypes.h"
#include "GFCraftingSubsystem.generated.h"

class UDataTable;
class UGFInventoryComponent;

UENUM(BlueprintType)
enum class EGFCraftResult : uint8
{
	Crafted,
	UnknownRecipe,
	Locked,
	WrongStation,
	MissingInputs,
	/** Called on a client: the inventory only changes on the server. */
	NotAuthority
};

/** Runs DT_Recipes against an inventory. Pure static core so GFGame.Craft.Recipe needs no world. */
UCLASS()
class GFBUILD_API UGFCraftingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Set by the game mode (or loaded from project settings later). Rows: FGFRecipeRow. */
	UPROPERTY(BlueprintReadWrite, Category = "GF|Crafting")
	TObjectPtr<UDataTable> RecipeTable;

	/** Unlocked recipe tags (saved). Recipes with an empty UnlockTag are always available. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "GF|Crafting")
	FGameplayTagContainer UnlockedTags;

	UFUNCTION(BlueprintCallable, Category = "GF|Crafting")
	EGFCraftResult Craft(FName RecipeId, FName AtStation, UGFInventoryComponent* Inventory);

	UFUNCTION(BlueprintPure, Category = "GF|Crafting")
	bool CanCraft(FName RecipeId, FName AtStation, const UGFInventoryComponent* Inventory) const;

	/** Pure: checks and applies one recipe. Inventory is unchanged unless the result is Crafted. */
	static EGFCraftResult TryCraft(const FGFRecipeRow& Recipe, FName AtStation, const FGameplayTagContainer& Unlocked, FGFInventory& Inventory);
	static EGFCraftResult Check(const FGFRecipeRow& Recipe, FName AtStation, const FGameplayTagContainer& Unlocked, const FGFInventory& Inventory);

private:
	const FGFRecipeRow* FindRecipe(FName RecipeId) const;
};
