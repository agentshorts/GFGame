#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "GFItemTypes.generated.h"

class UTexture2D;

/** What kind of thing an item is. Materials are the mini-game yields (PLAN.md section 2). */
UENUM(BlueprintType)
enum class EGFItemCategory : uint8
{
	Material,
	Furniture,
	Seed,
	Treat,
	Outfit,
	Block,
	/** Usable gadget (Calming Mist). Not tradeable: no tier, never wagered or rented. */
	Tool
};

/** Economy category (PLAN.md section 2): used for wager matching and the "what I need" journal. */
UENUM(BlueprintType)
enum class EGFMaterialCategory : uint8
{
	None,
	Building,
	Garden,
	Food,
	Fabric,
	Glow,
	Trophy,
	Rare
};

/** Materials that drop in one mode only (PLAN.md section 2 value tiers). */
UENUM(BlueprintType)
enum class EGFExclusive : uint8
{
	None,
	HallowNight,
	Lab
};

UENUM(BlueprintType)
enum class EGFRarity : uint8
{
	Common,
	Uncommon,
	Rare
};

/** Row for DT_Items. Row name = item id. */
USTRUCT(BlueprintType)
struct GFCORE_API FGFItemRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	EGFItemCategory Category = EGFItemCategory::Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "1"))
	int32 StackMax = 999;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	EGFRarity Rarity = EGFRarity::Common;

	/** Which mini-game or hub source mainly yields it (for UI hints). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FGameplayTag SourceTag;

	/** Value tier 1-4 (UI shows this many pips, never a number). 0 = not a tradeable material (furniture, tools). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Tier = 0;

	/** Tuning value per unit (1 / 3 / 8 / 20, exclusives 25 or 10). Never shown to the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	int32 Value = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	EGFMaterialCategory MaterialCategory = EGFMaterialCategory::None;

	/** Can be staked in arena wagers. Dinos, furniture, tools and key items never can. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	bool bWagerable = false;

	/** Can be chosen as plot rent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	bool bRentable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	EGFExclusive Exclusive = EGFExclusive::None;

	/** Stays on the account that received it. Gifts, mail, wagers and rent cannot move it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	bool bAccountBound = false;
};

/** An item id and a count. Used for inventory slots, recipe inputs/outputs and rewards. */
USTRUCT(BlueprintType)
struct GFCORE_API FGFItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Item")
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Item", meta = (ClampMin = "0"))
	int32 Count = 0;

	FGFItemStack() = default;
	FGFItemStack(FName InId, int32 InCount) : ItemId(InId), Count(InCount) {}

	bool IsValid() const { return !ItemId.IsNone() && Count > 0; }
};

/**
 * Pure inventory data (no UObject), so crafting, rewards and save code can be unit-tested
 * without a world. UGFInventoryComponent wraps one of these.
 */
USTRUCT(BlueprintType)
struct GFCORE_API FGFInventory
{
	GENERATED_BODY()

	/** One entry per item id; counts are unbounded here, StackMax only matters for UI. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "Inventory")
	TArray<FGFItemStack> Items;

	int32 GetCount(FName ItemId) const;
	bool Has(FName ItemId, int32 Count) const { return GetCount(ItemId) >= Count; }
	bool HasAll(const TArray<FGFItemStack>& Stacks) const;
	void Add(FName ItemId, int32 Count);
	/** Removes Count of ItemId. Returns false and changes nothing if there are not enough. */
	bool Remove(FName ItemId, int32 Count);
	/** Removes every stack or nothing. */
	bool RemoveAll(const TArray<FGFItemStack>& Stacks);
	/** Order-independent hash for save round-trip tests. */
	uint32 GetContentHash() const;

	bool operator==(const FGFInventory& Other) const { return GetContentHash() == Other.GetContentHash(); }
};
