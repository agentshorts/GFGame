#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFArenaGateSubsystem.generated.h"

/** Porch gate for Cinder Crucible. The in-world arena director stays the hub shortcut and does not set a return stone. */
UCLASS()
class GFARENA_API UGFArenaGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Arena"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Crucible"); }
};
