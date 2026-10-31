#pragma once

// A game mode implements this when it wants a say. Presence is the opt-in.
// Returning true turns stamina on even on a gate map. Returning false turns it off even on the hub.
// Modes that share AGFGameMode can call UGFMovementWorldSubsystem::SetStaminaOptIn instead.

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GFStaminaMode.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGFStaminaMode : public UInterface
{
	GENERATED_BODY()
};

class GFGAME_API IGFStaminaMode
{
	GENERATED_BODY()

public:
	virtual bool WantsPlayerStamina() const = 0;
};
