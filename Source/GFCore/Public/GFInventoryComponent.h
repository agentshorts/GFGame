#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFItemTypes.h"
#include "GFInventoryComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGFOnInventoryChanged, FName, ItemId, int32, NewCount);

/**
 * The one cross-mode inventory (PLAN.md section 4). Lives on AGFPlayerState (section 4.1) so it survives
 * respawn and travel. Server-authoritative: only the server (or a standalone game) can change it; the
 * contents replicate to the owning client only (COND_OwnerOnly). Clients ask for changes through
 * server RPCs on the player state; transfers between players go through the economy ledger.
 */
UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFCORE_API UGFInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "GF|Inventory")
	int32 GetCount(FName ItemId) const { return Inventory.GetCount(ItemId); }

	/** Server only. Returns false (and changes nothing) when called on a client. */
	UFUNCTION(BlueprintCallable, Category = "GF|Inventory")
	bool AddItem(FName ItemId, int32 Count);

	/** Server only. False if not enough, or when called on a client. */
	UFUNCTION(BlueprintCallable, Category = "GF|Inventory")
	bool RemoveItem(FName ItemId, int32 Count);

	/** Server only. All or nothing. */
	bool RemoveAll(const TArray<FGFItemStack>& Stacks);
	/** Server only. */
	bool AddAll(const TArray<FGFItemStack>& Stacks);

	const FGFInventory& GetInventory() const { return Inventory; }
	/** Replaces the whole inventory (join upload, save load). Server only. */
	bool SetInventory(const FGFInventory& NewInventory);

	/** True on the server / standalone (the only place the inventory may change). */
	bool CanMutate() const;

	/** Bumped on every server-side change; replicated so tests and UI can tell when the owner caught up. */
	UFUNCTION(BlueprintPure, Category = "GF|Inventory")
	int32 GetRevision() const { return Revision; }

	UPROPERTY(BlueprintAssignable, Category = "GF|Inventory")
	FGFOnInventoryChanged OnInventoryChanged;

protected:
	void MarkChanged(FName ItemId);

	UFUNCTION()
	void OnRep_Inventory();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, ReplicatedUsing = OnRep_Inventory, Category = "GF|Inventory")
	FGFInventory Inventory;

	UPROPERTY(Replicated)
	int32 Revision = 0;
};
