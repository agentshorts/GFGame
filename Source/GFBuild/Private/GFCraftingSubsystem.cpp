#include "GFCraftingSubsystem.h"
#include "GFInventoryComponent.h"
#include "Engine/DataTable.h"

const FGFRecipeRow* UGFCraftingSubsystem::FindRecipe(FName RecipeId) const
{
	return RecipeTable ? RecipeTable->FindRow<FGFRecipeRow>(RecipeId, TEXT("GFCrafting"), false) : nullptr;
}

EGFCraftResult UGFCraftingSubsystem::Check(const FGFRecipeRow& Recipe, FName AtStation, const FGameplayTagContainer& Unlocked, const FGFInventory& Inventory)
{
	if (Recipe.UnlockTag.IsValid() && !Unlocked.HasTag(Recipe.UnlockTag))
	{
		return EGFCraftResult::Locked;
	}
	if (!Recipe.Station.IsNone() && Recipe.Station != AtStation)
	{
		return EGFCraftResult::WrongStation;
	}
	if (!Recipe.Output.IsValid())
	{
		return EGFCraftResult::UnknownRecipe;
	}
	return Inventory.HasAll(Recipe.Inputs) ? EGFCraftResult::Crafted : EGFCraftResult::MissingInputs;
}

EGFCraftResult UGFCraftingSubsystem::TryCraft(const FGFRecipeRow& Recipe, FName AtStation, const FGameplayTagContainer& Unlocked, FGFInventory& Inventory)
{
	const EGFCraftResult R = Check(Recipe, AtStation, Unlocked, Inventory);
	if (R == EGFCraftResult::Crafted)
	{
		verify(Inventory.RemoveAll(Recipe.Inputs));
		Inventory.Add(Recipe.Output.ItemId, Recipe.Output.Count);
	}
	return R;
}

bool UGFCraftingSubsystem::CanCraft(FName RecipeId, FName AtStation, const UGFInventoryComponent* Inventory) const
{
	const FGFRecipeRow* Recipe = FindRecipe(RecipeId);
	return Recipe && Inventory && Check(*Recipe, AtStation, UnlockedTags, Inventory->GetInventory()) == EGFCraftResult::Crafted;
}

EGFCraftResult UGFCraftingSubsystem::Craft(FName RecipeId, FName AtStation, UGFInventoryComponent* Inventory)
{
	const FGFRecipeRow* Recipe = FindRecipe(RecipeId);
	if (!Recipe || !Inventory)
	{
		return EGFCraftResult::UnknownRecipe;
	}
	if (!Inventory->CanMutate())
	{
		return EGFCraftResult::NotAuthority; // server-authoritative: clients craft through a server RPC
	}
	FGFInventory Working = Inventory->GetInventory();
	const EGFCraftResult R = TryCraft(*Recipe, AtStation, UnlockedTags, Working);
	if (R == EGFCraftResult::Crafted)
	{
		Inventory->SetInventory(Working);
	}
	return R;
}
