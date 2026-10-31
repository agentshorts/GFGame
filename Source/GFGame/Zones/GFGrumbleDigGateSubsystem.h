#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFGrumbleDigGateSubsystem.generated.h"

/** Porch gate for Grumble Dig. The map name stays L_GrumbleDig. While that package is missing, the arch drops you on AGFGrumbleDigSite. */
UCLASS()
class GFGAME_API UGFGrumbleDigGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("GrumbleDig"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_GrumbleDig"); }
};
