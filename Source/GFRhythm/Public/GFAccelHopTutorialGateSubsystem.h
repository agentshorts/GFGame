#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFAccelHopTutorialGateSubsystem.generated.h"

/**
 * Acceleration Hop tutorial (#348): the hub side gate beside Beat Hop opens this versioned map. The map is a copy of
 * L_Beat made by Tools/create_accel_hop_tutorial_map.py; L_Beat itself is never touched. Until that script has run the
 * gate says "Under construction" like any other missing map.
 */
UCLASS()
class GFRHYTHM_API UGFAccelHopTutorialGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("AccelHopTutorial"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_AccelHop_Tutorial_v1"); }
	virtual FText GetDisplayName() const override { return FText::FromString(TEXT("Accel Hop 101")); }
};
