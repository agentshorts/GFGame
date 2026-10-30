#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFMobaGateSubsystem.generated.h"

/** Gate B: mini arena, 3v3 one lane vs bots (M4). Stub. */
UCLASS()
class GFMOBA_API UGFMobaGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Moba"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Arena"); }
};
