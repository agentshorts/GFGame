#pragma once

// Region slots, gate ids, and porch colours. Positions in the world come from DT_Zones.csv.
// This table is the gate wiring: which slot is enabled, and which existing gate id it calls.

#include "CoreMinimal.h"
#include "GFZoneTypes.h"

class UGFMiniGameGateSubsystem;
class UGameInstance;

struct GFGAME_API FGFRegionSlot
{
	float BearingDeg = 0.f;
	FName RegionId;
	FName StoneId;
	FName GateId;
	bool bEnabled = false;
	FString DisplayName;
	FLinearColor Color = FLinearColor::White;
};

struct GFGAME_API FGFRegionTravelPlan
{
	bool bTravel = false;
	FString Message;
	FName PendingStoneId;
	FName GateId;
	FName MapPackage;
};

struct GFGAME_API FGFZoneLayout
{
	static const TArray<FGFRegionSlot>& Regions();
	static const FGFRegionSlot* FindRegion(FName RegionId);
	static const FGFRegionSlot* FindByGate(FName GateId);
	static FVector BearingPoint(float BearingDeg, float RadiusCm);

	/** CDO of the UGFMiniGameGateSubsystem whose GetGateId matches. Null when the module is not loaded. */
	static UClass* FindGateClass(FName GateId);
	static UGFMiniGameGateSubsystem* FindLiveGate(UGameInstance* GI, FName GateId);

	/**
	 * bPackageExists is the caller's FPackageName::DoesPackageExist result.
	 * A missing package does not travel and does not set a pending stone.
	 */
	static FGFRegionTravelPlan PlanTravel(const FGFRegionSlot& Slot, bool bPackageExists);
};
