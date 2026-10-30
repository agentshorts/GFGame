#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFTamingGateSubsystem.generated.h"

/** Gate A: dino taming in the Valley (M2). Stub. */
UCLASS()
class GFCOMPANION_API UGFTamingGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Taming"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Valley"); }
};
