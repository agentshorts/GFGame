#pragma once

// Copies stair numbers onto a CharacterMovementComponent.
// Players pass AGFCharacter::PlayerMaxStepHeight. Dinos pass UGFMovementTuning::MaxStepHeight.

#include "CoreMinimal.h"

class UCharacterMovementComponent;

namespace GFStairMovement
{
	/** Step height is clamped to the capsule's StepUp limit when the capsule is already on the component. */
	void Apply(UCharacterMovementComponent* Move, float StepHeightCm, float WalkableFloorAngleDeg);
}

/** Installed from the game module. AGFDino::BeginPlay calls it so dinos pick up stair tuning and foot IK. */
void GFInstallDinoStairHook();
