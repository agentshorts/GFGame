#pragma once

// Pure stamina and traversal step. No world, no movement component.
// Tests and UGFCharacterMovementComponent both call Step.
// Sprint that empties the pool stays off until the key is released, so regen cannot flicker the burst back on.
// Dash direction is horizontal: move input, or camera yaw when the stick is idle.

#include "CoreMinimal.h"
#include "GFMovementTuning.h"

/** World facts for one step. The movement component fills this from traces. Tests fill it directly. */
struct GFGAME_API FGFTraversalContact
{
	bool bDashImpact = false;
	FVector ImpactNormal = FVector::UpVector;
	float SpeedBefore = 0.f;
	float SpeedAfter = 0.f;
	bool bWallContact = false;
	FVector WallNormal = FVector::ForwardVector;
	bool bSwivelContact = false;
	FVector SwivelOrigin = FVector::ZeroVector;
	bool bGrappleHit = false;
	FVector GrapplePoint = FVector::ZeroVector;
};

/** One tick of intent. Edges (dash, yank) are only true on the press. */
struct GFGAME_API FGFStaminaCommand
{
	bool bSprintHeld = false;
	bool bDashPressed = false;
	bool bJumpPressed = false;
	bool bGrappleSwingHeld = false;
	bool bGrappleYankPressed = false;
	bool bGrounded = true;
	bool bStaminaEnabled = true;
	bool bInsideKindleRing = false;
	float DeltaSeconds = 0.f;
	float HorizontalSpeed = 0.f;
	FVector MoveDirection = FVector::ZeroVector;
	FVector CameraForward = FVector::ForwardVector;
	FVector Velocity = FVector::ZeroVector;
	FVector PlayerLocation = FVector::ZeroVector;
	FGFTraversalContact Contact;
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

	bool bGripping = false;
	float GripTimeLeft = 0.f;
	FVector GripNormal = FVector::ForwardVector;

	bool bWallRunning = false;
	bool bWallRunBlocked = false;
	float WallRunTimeLeft = 1.5f;
	float WallRunSpeed = 0.f;
	FVector WallRunDir = FVector::ForwardVector;
	FVector WallNormal = FVector::ForwardVector;

	bool bSwiveling = false;
	float SwivelAngle = 0.f;
	float SwivelRadius = 0.f;
	float SwivelSpeed = 0.f;
	float SwivelSign = 1.f;
	FVector SwivelOrigin = FVector::ZeroVector;

	bool bSwinging = false;
	bool bYanking = false;
	float GrappleCooldownLeft = 0.f;
	float RopeLength = 0.f;
	FVector GrappleAnchor = FVector::ZeroVector;
	FVector TravelLocation = FVector::ZeroVector;
	FVector SwingVelocity = FVector::ZeroVector;

	bool bOverrideVelocity = false;
	FVector OverrideVelocity = FVector::ZeroVector;

	/** Jump presses since the last landing or wall-kick. Jump 1 is free. The air dash does not touch this. */
	int32 JumpsUsed = 0;
	/** This step's jump press launched a wall-kick or a swivel, so it is not one of the three hops. */
	bool bTraversalJump = false;
	bool bJumpAccepted = false;
	float JumpZSpeed = 0.f;
	FVector JumpForward = FVector::ZeroVector;
	/** Seconds left on the jump-3 front flip. The mesh reads this. Landing clears it. */
	float FlipTimeLeft = 0.f;

	void ResetFull(const UGFMovementTuning& Tuning);
};

struct GFGAME_API FGFLandingPrediction
{
	bool bHit = false;
	float Time = 0.f;
	FVector Point = FVector::ZeroVector;
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

namespace GFTraversalSim
{
	/** Actor tag that marks a swivel even when the bounds are not a thin post. */
	GFGAME_API FName SwivelTag();

	/** True for the Swivel tag, or for a tall thin bounds. */
	GFGAME_API bool IsSwivelTarget(bool bTagged, const FVector& BoundsSize, const UGFMovementTuning& Tuning);

	/**
	 * Grip, wall run, swivel, and grapple for one step.
	 * Returns true when stamina was spent. May clear a dash that just hit a wall or a pole.
	 */
	GFGAME_API bool Apply(FGFStaminaState& Next, const FGFStaminaCommand& Command, const UGFMovementTuning& Tuning);

	/** Drops active traversal. Leaves the grapple cooldown where it was. */
	GFGAME_API void ClearActivity(FGFStaminaState& State, const UGFMovementTuning& Tuning);

	/** Airborne, air dash, or a swing. Hidden while clinging, wall running, or orbiting. */
	GFGAME_API bool ShouldShowLandingMarker(const FGFStaminaState& State, bool bGrounded, bool bEnabled);

	GFGAME_API FVector BallisticPoint(const FVector& Origin, const FVector& Velocity, float GravityZ, float Time);

	/** First positive root of the parabola against a flat floor. */
	GFGAME_API FGFLandingPrediction PredictLandingOnPlane(const FVector& Origin, const FVector& Velocity, float GravityZ, float FloorZ, float MaxTime);
}
