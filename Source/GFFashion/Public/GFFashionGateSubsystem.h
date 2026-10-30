#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFFashionGateSubsystem.generated.h"

/** Gate C: fashion show (M3). Stub. */
UCLASS()
class GFFASHION_API UGFFashionGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Fashion"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Runway"); }
};
