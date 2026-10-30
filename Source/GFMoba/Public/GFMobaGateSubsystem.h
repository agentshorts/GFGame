#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFMobaGateSubsystem.generated.h"

/** Gate B: the MOBA (M4). Target is now a true 5v5 (up to 10 players); PLAN.md M4 still has the older 3v3-vs-bots scope. Stub. */
UCLASS()
class GFMOBA_API UGFMobaGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Moba"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Arena"); }
};
