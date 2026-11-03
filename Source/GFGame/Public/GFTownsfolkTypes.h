#pragma once

// Villager and elder roles, and what a townsfolk is doing.
// Moves go through FGFTownsfolkMind::Transition. Warn then Flee is the theft response.

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
	Talk,
	/** Faces the thief and shouts. Lasts WarnSeconds, then Flee. */
	Warn,
	/** Runs to this town's elder. Arrival or the timeout asks for the hire. */
	Flee
};

/** What one Advance of a townsfolk mind reported. */
enum class EGFTownsfolkPulse : uint8
{
	None,
	StartedFlee,
	AskHire
};
