#include "GFInventoryComponent.h"
#include "GFLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UGFInventoryComponent::UGFInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UGFInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UGFInventoryComponent, Inventory, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFInventoryComponent, Revision, COND_OwnerOnly);
}

bool UGFInventoryComponent::CanMutate() const
{
	const AActor* Owner = GetOwner();
	return !Owner || Owner->HasAuthority();
}

void UGFInventoryComponent::MarkChanged(FName ItemId)
{
	++Revision;
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	OnInventoryChanged.Broadcast(ItemId, ItemId.IsNone() ? 0 : Inventory.GetCount(ItemId));
}

bool UGFInventoryComponent::AddItem(FName ItemId, int32 Count)
{
	if (!CanMutate())
	{
		UE_LOG(LogGF, Warning, TEXT("Inventory: client tried to add %s x%d; ignored (server-authoritative)"), *ItemId.ToString(), Count);
		return false;
	}
	if (ItemId.IsNone() || Count <= 0)
	{
		return false;
	}
	Inventory.Add(ItemId, Count);
	MarkChanged(ItemId);
	return true;
}

bool UGFInventoryComponent::RemoveItem(FName ItemId, int32 Count)
{
	if (!CanMutate() || !Inventory.Remove(ItemId, Count))
	{
		return false;
	}
	MarkChanged(ItemId);
	return true;
}

bool UGFInventoryComponent::RemoveAll(const TArray<FGFItemStack>& Stacks)
{
	if (!CanMutate() || !Inventory.RemoveAll(Stacks))
	{
		return false;
	}
	MarkChanged(NAME_None);
	return true;
}

bool UGFInventoryComponent::AddAll(const TArray<FGFItemStack>& Stacks)
{
	if (!CanMutate())
	{
		return false;
	}
	for (const FGFItemStack& S : Stacks)
	{
		Inventory.Add(S.ItemId, S.Count);
	}
	MarkChanged(NAME_None);
	return true;
}

bool UGFInventoryComponent::SetInventory(const FGFInventory& NewInventory)
{
	if (!CanMutate())
	{
		return false;
	}
	Inventory = NewInventory;
	MarkChanged(NAME_None);
	return true;
}

void UGFInventoryComponent::OnRep_Inventory()
{
	OnInventoryChanged.Broadcast(NAME_None, 0);
}
