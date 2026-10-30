#pragma once

// Pure stamina step. No world, no movement component. Tests and UGFCharacterMovementComponent both call Step.
// Sprint that empties the pool stays off until the key is released, so regen cannot flicker the burst back on.
// Dash direction is horizontal: move input, or camera yaw when the stick is idle. Look pitch is left for traversal.

#include "CoreMinimal.h"
#include "GFMovementTuning.h"

/** One tick of intent. Edges (dash) are only true on the press. */
struct GFGAME_API FGFStaminaCommand
{
	bool bSprintHeld = false;
	bool bDashPressed = false;
	bool bGrounded = true;
	bool bStaminaEnabled = true;
	bool bInsideKindleRing = false;
	float DeltaSeconds = 0.f;
	FVector MoveDirection = FVector::ZeroVector;
	FVector CameraForward = FVector::ForwardVector;
};

/** Result of the last steps. Copied into saved moves so a replay starts from the same pool. */
struct GFGAME_API FGFStaminaState
{
	float Stamina = 200.f;
	float RegenDelayLeft = 0.f;
	float DashTimeLeft = 0.f;
	float GroundCooldownLeft = 0.f;
	int32 AirDashesUsed = 0;
	bool bSprintBlocked = false;
	bool bSprinting = false;
	bool bDashing = false;
	bool bWasGrounded = true;
	FVector DashDir = FVector::ForwardVector;

	void ResetFull(const UGFMovementTuning& Tuning);
};

namespace GFStaminaSim
{
	GFGAME_API FGFStaminaState Step(const FGFStaminaState& State, const FGFStaminaCommand& Command, const UGFMovementTuning& Tuning);

	/** Unit XY direction. Move input wins. Camera yaw is the fallback. World +X if both are empty. */
	GFGAME_API FVector ResolveDashDirection(const FVector& MoveDirection, const FVector& CameraForward);

	/** Dash speed, else live walk speed times the sprint multiplier, else live walk speed. */
	GFGAME_API float HorizontalSpeed(const FGFStaminaState& State, const UGFMovementTuning& Tuning, float LiveWalkSpeed);

	/** 1 while walking or falling normally. DashGravityScale during a burst. */
	GFGAME_API float GravityScale(const FGFStaminaState& State, const UGFMovementTuning& Tuning);

	/** False when the feature is off or the pool is full (within HideEpsilon). */
	GFGAME_API bool ShouldShowArc(float Stamina, float MaxStamina, bool bEnabled, float HideEpsilon);
}
