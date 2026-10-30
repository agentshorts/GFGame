#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFGrumbleDigGateSubsystem.generated.h"

/** Porch gate for Grumble Dig. The dig planner has no map of its own yet. */
UCLASS()
class GFGAME_API UGFGrumbleDigGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("GrumbleDig"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_GrumbleDig"); }
};
