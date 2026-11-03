#pragma once

// Villager and elder roles, and what a townsfolk is doing.
// Warn and Flee are not values yet. A later piece adds them. Moves go through FGFTownsfolkMind::Transition.

#include "CoreMinimal.h"
#include "GFTownsfolkTypes.generated.h"

UENUM(BlueprintType)
enum class EGFTownsfolkRole : uint8
{
	Villager,
	Elder
};

UENUM(BlueprintType)
enum class EGFTownsfolkBehavior : uint8
{
	Idle,
	Wander,
	Talk
};
