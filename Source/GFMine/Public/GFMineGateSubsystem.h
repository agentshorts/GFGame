#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFMineGateSubsystem.generated.h"

/** Porch gate for Pebblefall Quarry. The mine director on the hub is unchanged. */
UCLASS()
class GFMINE_API UGFMineGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Mine"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Mine"); }
};
