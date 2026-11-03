#pragma once

// Something a player can talk to. The character finds the nearest one and asks the server to use it.

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GFInteractable.generated.h"

class APawn;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGFInteractable : public UInterface
{
	GENERATED_BODY()
};

class GFGAME_API IGFInteractable
{
	GENERATED_BODY()

public:
	virtual FText GetInteractPrompt() const = 0;

	/** True when Pawn is allowed to use this. Reach is part of the check. */
	virtual bool CanInteract(APawn* Pawn) const = 0;

	/** Server. One successful press. */
	virtual void Interact(APawn* Pawn) = 0;
};
