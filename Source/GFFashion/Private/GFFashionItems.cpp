#include "GFFashionItems.h"

bool FGFFashionItems::SlotForItemType(const FString& ItemType, EGFOutfitSlot& OutSlot)
{
	// Hair is the head slot: visors, beanies, hoods and tiaras hang there.
	// A full suit is one item, so it hangs on Top (there is no suit slot).
	// Bags, jewellery and cosmetics share Accessory.
	if (ItemType == TEXT("hat/mask"))
	{
		OutSlot = EGFOutfitSlot::Hair;
		return true;
	}
	if (ItemType == TEXT("top") || ItemType == TEXT("full suit"))
	{
		OutSlot = EGFOutfitSlot::Top;
		return true;
	}
	if (ItemType == TEXT("bottoms"))
	{
		OutSlot = EGFOutfitSlot::Bottom;
		return true;
	}
	if (ItemType == TEXT("shoes"))
	{
		OutSlot = EGFOutfitSlot::Shoes;
		return true;
	}
	if (ItemType == TEXT("bag/purse") || ItemType == TEXT("accessory") || ItemType == TEXT("cosmetic"))
	{
		OutSlot = EGFOutfitSlot::Accessory;
		return true;
	}
	return false;
}

FName FGFFashionItems::RowNameForSlot(EGFOutfitSlot Slot)
{
	switch (Slot)
	{
	case EGFOutfitSlot::Hair: return TEXT("Hair");
	case EGFOutfitSlot::Top: return TEXT("Top");
	case EGFOutfitSlot::Bottom: return TEXT("Bottom");
	case EGFOutfitSlot::Shoes: return TEXT("Shoes");
	case EGFOutfitSlot::Accessory: return TEXT("Accessory");
	default: return NAME_None;
	}
}

bool FGFFashionItems::SlotFromRowName(FName SlotName, EGFOutfitSlot& OutSlot)
{
	if (SlotName == TEXT("Hair"))
	{
		OutSlot = EGFOutfitSlot::Hair;
		return true;
	}
	if (SlotName == TEXT("Top"))
	{
		OutSlot = EGFOutfitSlot::Top;
		return true;
	}
	if (SlotName == TEXT("Bottom"))
	{
		OutSlot = EGFOutfitSlot::Bottom;
		return true;
	}
	if (SlotName == TEXT("Shoes"))
	{
		OutSlot = EGFOutfitSlot::Shoes;
		return true;
	}
	if (SlotName == TEXT("Accessory"))
	{
		OutSlot = EGFOutfitSlot::Accessory;
		return true;
	}
	return false;
}
