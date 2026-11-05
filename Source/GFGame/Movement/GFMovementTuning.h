#pragma once

// Every stamina, sprint, dash, jump, traversal (wall climb included), hearth and arc number lives here.
// UGFCharacterMovementComponent uses the asset when one is assigned, otherwise this class default.

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GFBeatHopNinja.h"
#include "GFAccelHop.h"
#include "GFMovementTuning.generated.h"

UCLASS(BlueprintType)
class GFGAME_API UGFMovementTuning : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Pool size. Regen stops here. Costs cannot push it below zero. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "1"))
	float MaxStamina = 200.f;

	/** Stamina returned each second once RegenDelay has elapsed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0"))
	float RegenPerSecond = 25.f;

	/** Seconds after a sprint or dash spend before regen starts. A new spend starts it over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0"))
	float RegenDelay = 0.8f;

	/** Stamina spent each second while sprint is held and the pool is not empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sprint", meta = (ClampMin = "0"))
	float SprintCostPerSecond = 15.f;

	/**
	 * Multiplier on the live walk speed while sprinting.
	 * Applied in GetMaxSpeed. MaxWalkSpeed itself stays BaseRunSpeed so Beat Hop and the giant still read 500.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sprint", meta = (ClampMin = "1"))
	float SprintSpeedMultiplier = 1.4f;

	/** Copied onto CharacterMovement MaxWalkSpeed at init. Do not fold the sprint multiplier into this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sprint", meta = (ClampMin = "0"))
	float BaseRunSpeed = 500.f;

	/** Stamina spent the instant a dash is accepted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dash", meta = (ClampMin = "0"))
	float DashCost = 25.f;

	/** Horizontal burst speed, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dash", meta = (ClampMin = "0"))
	float DashSpeed = 900.f;

	/** How long the burst lasts, seconds. Gravity uses DashGravityScale for this window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dash", meta = (ClampMin = "0"))
	float DashDuration = 0.2f;

	/**
	 * Ground dash lockout, seconds, measured from the press (the burst counts toward it).
	 * Air dashes do not use this timer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dash", meta = (ClampMin = "0"))
	float GroundDashCooldown = 0.5f;

	/** Air dashes allowed between leaving the ground and the next landing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dash", meta = (ClampMin = "0"))
	int32 AirDashesPerAirtime = 1;

	/** Multiplier on gravity during the burst. Zero pauses gravity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dash")
	float DashGravityScale = 0.f;

	/** World location of the hub hearth spawned when the map has no AGFKindleRing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kindle")
	FVector KindleRingLocation = FVector(0.f, 0.f, 40.f);

	/** Radius of that hearth, cm. Overlap grants infinite stamina. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kindle", meta = (ClampMin = "0"))
	float KindleRingRadius = 400.f;

	/** The arc hides when stamina is within this of MaxStamina. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD", meta = (ClampMin = "0"))
	float HudHideEpsilon = 0.5f;

	/** Fraction of dash speed that must be lost on a wall hit before an auto-grip. Above this, not equal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float DashGripSpeedLoss = 0.6f;

	/** Longest cling, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0"))
	float DashGripMaxSeconds = 2.f;

	/** Stamina per second while clinging. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0"))
	float DashGripDrainPerSecond = 10.f;

	/** A hit whose normal Z is above this is a floor, not a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0", ClampMax = "1"))
	float WallNormalMaxZ = 0.35f;

	/** cm/s into the wall while clinging, so the capsule stays in contact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0"))
	float GripStickSpeed = 30.f;

	/** Horizontal speed of the jump off a cling, cm/s, along the wall normal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0"))
	float WallKickSpeed = 700.f;

	/** Upward speed added to a wall-kick, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip", meta = (ClampMin = "0"))
	float WallKickUp = 420.f;

	/** Wall run requires horizontal speed strictly above this, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun", meta = (ClampMin = "0"))
	float WallRunMinSpeed = 400.f;

	/** Longest wall run per visit to a wall, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun", meta = (ClampMin = "0"))
	float WallRunMaxSeconds = 1.5f;

	/** Stamina per second while wall running. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun", meta = (ClampMin = "0"))
	float WallRunDrainPerSecond = 15.f;

	/** cm/s into the wall while running, so the run stays stuck to the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun", meta = (ClampMin = "0"))
	float WallStickSpeed = 40.f;

	/** Minimum |dot| of the move input with the wall tangent. Into-the-wall input does not qualify. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun", meta = (ClampMin = "0", ClampMax = "1"))
	float WallRunAlongMin = 0.5f;

	/** How far past the capsule to look for a wall, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun", meta = (ClampMin = "0"))
	float WallProbeDistance = 48.f;

	/** #355: climb speed straight up the wall, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbSpeed = 350.f;

	/** cm/s into the wall while climbing, so the capsule stays on the face. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbStickSpeed = 60.f;

	/** Stamina per second while climbing. Free in the Kindle Ring and on cost-free runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbDrainPerSecond = 12.f;

	/** Longest climb per wall visit, seconds. Running out drops the climb until the input stops pushing into a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMaxSeconds = 1.5f;

	/** A climb may still start this long after leaving the floor, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbCoyoteSeconds = 0.12f;

	/** Minimum dot of the flat input against the wall normal. The input must also push in more than along. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0", ClampMax = "1"))
	float WallClimbIntoMin = 0.5f;

	/** How far past the capsule to look for a wall ahead, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbProbeDistance = 20.f;

	/** The wall-ahead probe sits this far above the step height, cm, so stairs and low blocks are never a climb. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbProbeAboveStep = 20.f;

	/**
	 * Run speed a climb carries into a wall run when the input turns along the wall, cm/s. Keep it above
	 * WallRunMinSpeed or the hand-off never passes the wall run speed gate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbToRunSpeed = 450.f;

	/** Highest lip a climb mantles onto, cm above the feet. Higher walls climb until the lip is this close. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMantleReach = 220.f;

	/** Lowest lip that counts as a mantle, cm above the feet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMantleMinLip = 30.f;

	/** How far past the wall face (beyond the capsule radius) the mantle stands, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMantleInset = 10.f;

	/** Stamina paid when a mantle starts. Free in the Kindle Ring and on cost-free runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMantleCost = 10.f;

	/** Mantle rise speed to the lip, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMantleUpSpeed = 600.f;

	/** Mantle step onto the lip, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMantleForwardSpeed = 450.f;

	/** Longest mantle, seconds. It ends here even if it never arrived, so nothing sticks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMantleMaxSeconds = 0.8f;

	/** Distance to the mantle target that counts as arrived, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallClimb", meta = (ClampMin = "0"))
	float WallClimbMantleArrive = 4.f;

	/** Untagged colliders wider than this are not swivels, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swivel", meta = (ClampMin = "0"))
	float SwivelMaxThickness = 80.f;

	/** Untagged colliders shorter than this are not swivels, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swivel", meta = (ClampMin = "0"))
	float SwivelMinHeight = 180.f;

	/** Untagged height must be at least this many times the horizontal thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swivel", meta = (ClampMin = "0"))
	float SwivelMinAspect = 2.f;

	/** Orbit speed used when the player hits the pole slower than this, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swivel", meta = (ClampMin = "0"))
	float SwivelMinSpeed = 300.f;

	/** Closest orbit radius, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swivel", meta = (ClampMin = "1"))
	float SwivelMinRadius = 40.f;

	/** Launch speed is the orbit speed times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swivel", meta = (ClampMin = "0"))
	float SwivelLaunchBoost = 1.5f;

	/** Upward speed added when launching off a swivel, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Swivel", meta = (ClampMin = "0"))
	float SwivelLaunchUp = 280.f;

	/** Grapple trace length, cm. 15 m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0"))
	float GrappleRange = 1500.f;

	/** Seconds before another grapple. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0"))
	float GrappleCooldown = 2.f;

	/** Stamina paid when a swing or a yank attaches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0"))
	float GrappleCost = 20.f;

	/** Tangential velocity scale when a swing is released. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0"))
	float GrappleReleaseBoost = 1.35f;

	/** Pull speed toward the anchor, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0"))
	float GrappleYankSpeed = 1400.f;

	/** Distance to the anchor that counts as arrived, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple", meta = (ClampMin = "0"))
	float GrappleYankArrive = 80.f;

	/** Gravity used by the pendulum, cm/s^2. Negative is down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple")
	float GrappleGravity = -980.f;

	/** How far ahead the landing trace looks, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.05"))
	float LandingLookahead = 2.5f;

	/** Seconds between ballistic samples. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.02"))
	float LandingSampleSeconds = 0.08f;

	/** Impact normals flatter than this are walls, and the marker skips them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0", ClampMax = "1"))
	float LandingMinFloorNormalZ = 0.5f;

	/** Upward speed of jump 1, cm/s. 520 clears one 100 cm block. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0"))
	float JumpZVelocity = 520.f;

	/** Jump presses allowed before the next landing or wall-kick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0"))
	int32 JumpsPerAirtime = 3;

	/**
	 * Jump 2 apex height relative to jump 1.
	 * Upward speed is scaled by the square root, because height grows with speed squared.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0"))
	float Jump2HeightScale = 0.85f;

	/** Jump 3 apex height relative to jump 1. Same square-root speed scale as jump 2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0"))
	float Jump3HeightScale = 1.10f;

	/** Horizontal speed added on jump 3, cm/s, along move input, else velocity, else the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0"))
	float Jump3ForwardBoost = 250.f;

	/** Stamina for jump 2 and jump 3. Jump 1 is free. The air dash does not spend this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0"))
	float ExtraJumpCost = 15.f;

	/** Seconds of the procedural front flip on jump 3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0"))
	float JumpFlipDuration = 0.45f;

	/** Mesh pitch over that flip, degrees. 360 is one full turn around the actor's right axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = "0"))
	float JumpFlipDegrees = 360.f;

	/**
	 * Beat Hop's copy of this kit (#277). Mini-games stay off until a run opts in.
	 * Costs, windows, obstacle sizes and which difficulty gets which move are all on this profile.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BeatHop")
	FGFBeatHopTraversalProfile BeatHop;

	/**
	 * Acceleration Hop (#334): hop tiles on the Verdant to Cinderwright road. Boost sizes, cap, length in beats,
	 * which Beat Hop windows judge a landing, and the server's checks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AccelerationHop")
	FGFAccelHopTuning AccelHop;

	/**
	 * Tallest ledge a dino walks up without jumping, cm. Copied onto CharacterMovement MaxStepHeight.
	 * Players keep AGFCharacter::PlayerMaxStepHeight (same default, config on the character) so one-block
	 * voxel stairs stay on that knob. One block is 100 cm. Stay under the capsule step limit
	 * (2 * half height - radius).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stair", meta = (ClampMin = "0"))
	float MaxStepHeight = 105.f;

	/**
	 * CharacterMovement WalkableFloorAngle, degrees, for players and dinos.
	 * Hub stair ramps stay under this so the capsule slides up the slope. A slope at the limit is not a floor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stair", meta = (ClampMin = "0", ClampMax = "90"))
	float WalkableFloorAngle = 50.f;

	/**
	 * How far the ankle sits above a tread, cm, so the sole plants.
	 * The player foot is 2 mv tall (28 mv = 175 cm, so 2 mv = 12.5 cm).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stair", meta = (ClampMin = "0"))
	float FootSoleOffsetCm = 12.5f;
};
