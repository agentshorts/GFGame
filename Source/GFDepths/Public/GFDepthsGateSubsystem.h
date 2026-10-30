#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFDepthsGateSubsystem.generated.h"

/** Porch gate for Hallow Mire. The depths director on the hub is unchanged. */
UCLASS()
class GFDEPTHS_API UGFDepthsGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Depths"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Depths"); }
};
