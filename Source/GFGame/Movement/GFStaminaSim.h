#pragma once

// Pure stamina and traversal step. No world, no movement component.
// Tests and UGFCharacterMovementComponent both call Step.
// Sprint that empties the pool stays off until the key is released, so regen cannot flicker the burst back on.
// Dash direction is horizontal: move input, or camera yaw when the stick is idle.

#include "CoreMinimal.h"
#include "Templates/Function.h"
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

	/**
	 * #355: a wall straight ahead of the move input (or the wall being climbed), probed above step height so stairs
	 * and low blocks never count. Separate from bWallContact, which only looks to the sides for a wall run.
	 */
	bool bWallAhead = false;
	FVector WallAheadNormal = FVector::ForwardVector;
	/** #355: a walkable lip above the climbed wall with room for the capsule on top. */
	bool bLedge = false;
	/** Capsule center standing on that lip. */
	FVector LedgeTarget = FVector::ZeroVector;
	/** Lip height above the feet, cm. */
	float LedgeHeight = 0.f;
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

	/** Beat Hop run (#277). Costs are skipped while this is set. Hub play leaves it false. */
	bool bCostsFree = false;
	/** Negative keeps the tuning asset's dash, wall-kick and jump values. */
	float GroundDashDurationOverride = -1.f;
	float AirDashDurationOverride = -1.f;
	float WallKickSpeedOverride = -1.f;
	int32 JumpsPerAirtimeOverride = -1;
	/** #355: false turns wall climb and its mantle off (Beat Hop runs). */
	bool bWallClimbAllowed = true;
};

/**
 * The one traversal move in charge. Exit velocity (a kick, a launch, a release, a stop) is not a move:
 * CurrentMove is None while that velocity is the only thing left.
 * Wall climb phase for replication stays EGFWallClimbPhase, derived from the climb and mantle flags.
 */
enum class EGFTraversalMove : uint8
{
	None = 0,
	Dash,
	Grip,
	WallRun,
	WallClimb,
	Mantle,
	Swivel,
	Swing,
	Yank,
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

	/** #355 wall climb: straight up the wall the input pushes into. */
	bool bWallClimbing = false;
	/** Set when a climb ran out of time or stamina. Cleared once the input stops pushing into a wall. */
	bool bWallClimbBlocked = false;
	float WallClimbTimeLeft = 0.f;
	/** Seconds since leaving the floor that a climb may still start. */
	float WallClimbCoyoteLeft = 0.f;
	FVector WallClimbNormal = FVector::ForwardVector;
	/** #355 climb-exit mantle: rise to the lip, then step onto it. */
	bool bMantling = false;
	float MantleTimeLeft = 0.f;
	FVector MantleTarget = FVector::ZeroVector;
	/** #356: lip height above the feet when this mantle started, cm (picks the Low or High mantle clip). */
	float MantleLipHeight = 0.f;

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

	/**
	 * The move in charge among dash, grip, wall run, wall climb, mantle, swivel, swing, and yank.
	 * Step publishes this after those flags settle. None while walking, falling, or on an exit velocity.
	 */
	EGFTraversalMove CurrentMove = EGFTraversalMove::None;

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

/**
 * Aim-follow air steering (#347): while airborne the flat velocity turns toward Aim at up to TurnRateDeg per second.
 * The flat speed is kept exactly; Z is never touched.
 */
struct GFGAME_API FGFAirSteer
{
	bool bOn = false;
	/** Where the camera aims. Only its flat direction counts. */
	FVector Aim = FVector::ForwardVector;
	float TurnRateDeg = 90.f;
	/** Below this flat speed nothing turns, cm/s. */
	float MinSpeed = 150.f;
	/** Aim further than this from the flight does not steer, degrees. */
	float MaxAngleDeg = 150.f;
};

/** The flight state the landing target is predicted from (#342). Built fresh every frame. */
struct GFGAME_API FGFFlightProbe
{
	FVector Origin = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	/** Sideways acceleration the player is steering with in the air (input x air control), cm/s^2. Z is ignored. */
	FVector LateralAccel = FVector::ZeroVector;
	/** Gravity for the flight, cm/s^2. Negative is down. */
	float GravityZ = -980.f;
	/** Steering never pushes the flat speed past this (or past the speed already carried, if higher). */
	float MaxLateralSpeed = 0.f;
	/** #347: the aim steering the flight will keep doing (the aim is held where it is now). */
	FGFAirSteer Steer;
	float Lookahead = 2.5f;
	float SampleSeconds = 0.08f;
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
	 * #355: input pushes into the wall more than along it (the inverse of the wall run test), with at least MinDot
	 * of the flat input against the wall normal.
	 */
	GFGAME_API bool IsIntoWall(const FVector& MoveDirection, const FVector& WallNormal, float MinDot);

	/** QL-b: a mantle was running at the start of the step and is not at the end (arrived, timed out, or cut). */
	GFGAME_API bool MantleFinished(const FGFStaminaState& Before, const FGFStaminaState& After);

	/**
	 * Grip, wall climb, mantle, wall run, swivel, and grapple for one step.
	 * Returns true when stamina was spent. May clear a dash that just hit a wall or a pole.
	 */
	GFGAME_API bool Apply(FGFStaminaState& Next, const FGFStaminaCommand& Command, const UGFMovementTuning& Tuning);

	/** Drops active traversal. Leaves the grapple cooldown where it was. */
	GFGAME_API void ClearActivity(FGFStaminaState& State, const UGFMovementTuning& Tuning);

	/**
	 * One move in charge, and a wall-run or wall-climb block clears that move.
	 * Writes CurrentMove from the flag that remains. A dash drops any override so it is the only velocity.
	 */
	GFGAME_API void PublishCurrentMove(FGFStaminaState& State);

	/** Airborne, air dash, or a swing. Hidden while clinging, climbing, mantling, wall running, or orbiting. */
	GFGAME_API bool ShouldShowLandingMarker(const FGFStaminaState& State, bool bGrounded, bool bEnabled);

	GFGAME_API FVector BallisticPoint(const FVector& Origin, const FVector& Velocity, float GravityZ, float Time);

	/** First positive root of the parabola against a flat floor. */
	GFGAME_API FGFLandingPrediction PredictLandingOnPlane(const FVector& Origin, const FVector& Velocity, float GravityZ, float FloorZ, float MaxTime);

	/**
	 * One step of the predicted flight (#342): gravity exact, steering added to the flat velocity and clamped to
	 * max(MaxLateralSpeed, speed carried in). With no steering it lands on BallisticPoint exactly.
	 */
	GFGAME_API void StepFlight(FVector& Position, FVector& Velocity, const FVector& LateralAccel, float GravityZ, float MaxLateralSpeed, float Dt);

	/**
	 * #347: the same step with aim steering after the push, in the order the movement runs it (CalcVelocity, then the
	 * turn). Steer off is exactly StepFlight.
	 */
	GFGAME_API void StepFlight(FVector& Position, FVector& Velocity, const FVector& LateralAccel, const FGFAirSteer& Steer, float GravityZ, float MaxLateralSpeed, float Dt);

	/**
	 * #347: Velocity with its flat part turned toward Steer.Aim by at most TurnRateDeg x Dt. Same flat speed, same Z.
	 * Unchanged when steering is off, too slow, the aim is flat zero, or the aim is more than MaxAngleDeg away.
	 */
	GFGAME_API FVector SteerTowardAim(const FVector& Velocity, const FGFAirSteer& Steer, float Dt);

	/**
	 * Walks the flight in SampleSeconds steps out to Lookahead and asks Trace(From, To, OutHitPoint) for the first
	 * floor. The movement component traces the world; tests pass a flat floor.
	 */
	GFGAME_API FGFLandingPrediction PredictFlightLanding(const FGFFlightProbe& Probe, TFunctionRef<bool(const FVector&, const FVector&, FVector&)> Trace);
}
