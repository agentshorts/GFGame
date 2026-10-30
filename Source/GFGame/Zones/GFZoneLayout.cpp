#include "GFZoneLayout.h"

#include "GFMiniGameGateSubsystem.h"
#include "GFZoneTypes.h"
#include "Engine/GameInstance.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FGFRegionSlot MakeSlot(float Bearing, const TCHAR* Suffix, const TCHAR* Name, const TCHAR* Gate, bool bEnabled, const FLinearColor& Color)
	{
		FGFRegionSlot Slot;
		Slot.BearingDeg = Bearing;
		Slot.RegionId = FName(*FString::Printf(TEXT("Region.%s"), Suffix));
		Slot.StoneId = FName(*FString::Printf(TEXT("Stone.%s"), Suffix));
		Slot.GateId = (bEnabled && Gate && Gate[0] != 0) ? FName(Gate) : NAME_None;
		Slot.bEnabled = bEnabled;
		Slot.DisplayName = Name;
		Slot.Color = Color;
		return Slot;
	}

	const TArray<FGFRegionSlot>& Build()
	{
		static const TArray<FGFRegionSlot> Slots = {
			MakeSlot(0.f, TEXT("Arena"), TEXT("Cinder Crucible"), TEXT("Arena"), true, FLinearColor(0.85f, 0.33f, 0.17f)),
			MakeSlot(45.f, TEXT("PebbleMine"), TEXT("Pebblefall Quarry"), TEXT("Mine"), true, FLinearColor(0.55f, 0.52f, 0.45f)),
			MakeSlot(90.f, TEXT("BeatHop"), TEXT("Thrum Hollow"), TEXT("BeatHop"), true, FLinearColor(0.85f, 0.25f, 0.70f)),
			MakeSlot(135.f, TEXT("Fashion"), TEXT("Glimmer Terrace"), TEXT("Fashion"), true, FLinearColor(0.95f, 0.55f, 0.75f)),
			MakeSlot(180.f, TEXT("Valley"), TEXT("Wildgrass Valley"), TEXT("Taming"), true, FLinearColor(0.35f, 0.72f, 0.30f)),
			MakeSlot(225.f, TEXT("HallowNight"), TEXT("Hallow Mire"), TEXT("Depths"), true, FLinearColor(0.15f, 0.45f, 0.42f)),
			MakeSlot(270.f, TEXT("GrumbleDig"), TEXT("Grumble Dig"), TEXT("GrumbleDig"), true, FLinearColor(0.62f, 0.42f, 0.18f)),
			MakeSlot(315.f, TEXT("Reserved1"), TEXT("Splitcrystal Flats"), nullptr, false, FLinearColor(0.55f, 0.40f, 0.85f)),
		};
		return Slots;
	}
}

const TArray<FGFRegionSlot>& FGFZoneLayout::Regions()
{
	return Build();
}

const FGFRegionSlot* FGFZoneLayout::FindRegion(FName RegionId)
{
	return Regions().FindByPredicate([RegionId](const FGFRegionSlot& Slot) { return Slot.RegionId == RegionId; });
}

const FGFRegionSlot* FGFZoneLayout::FindByGate(FName GateId)
{
	if (GateId.IsNone())
	{
		return nullptr;
	}
	return Regions().FindByPredicate([GateId](const FGFRegionSlot& Slot) { return Slot.GateId == GateId; });
}

FVector FGFZoneLayout::BearingPoint(float BearingDeg, float RadiusCm)
{
	const float Rad = FMath::DegreesToRadians(BearingDeg);
	return FVector(FMath::Cos(Rad) * RadiusCm, FMath::Sin(Rad) * RadiusCm, 0.f);
}

UClass* FGFZoneLayout::FindGateClass(FName GateId)
{
	if (GateId.IsNone())
	{
		return nullptr;
	}
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UGFMiniGameGateSubsystem::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		const UGFMiniGameGateSubsystem* CDO = Cast<UGFMiniGameGateSubsystem>(It->GetDefaultObject());
		if (CDO && CDO->GetGateId() == GateId)
		{
			return *It;
		}
	}
	return nullptr;
}

UGFMiniGameGateSubsystem* FGFZoneLayout::FindLiveGate(UGameInstance* GI, FName GateId)
{
	UClass* Class = FindGateClass(GateId);
	if (!GI || !Class)
	{
		return nullptr;
	}
	return Cast<UGFMiniGameGateSubsystem>(GI->GetSubsystemBase(Class));
}

FGFRegionTravelPlan FGFZoneLayout::PlanTravel(const FGFRegionSlot& Slot, bool bPackageExists)
{
	FGFRegionTravelPlan Plan;
	Plan.GateId = Slot.GateId;
	if (!Slot.bEnabled || Slot.GateId.IsNone())
	{
		return Plan;
	}
	if (UClass* Class = FindGateClass(Slot.GateId))
	{
		if (const UGFMiniGameGateSubsystem* CDO = Cast<UGFMiniGameGateSubsystem>(Class->GetDefaultObject()))
		{
			Plan.MapPackage = CDO->GetMapName();
		}
	}
	if (!bPackageExists || Plan.MapPackage.IsNone())
	{
		Plan.Message = GFZoneCopy::UnderConstruction();
		return Plan;
	}
	Plan.bTravel = true;
	Plan.PendingStoneId = Slot.StoneId;
	return Plan;
}
