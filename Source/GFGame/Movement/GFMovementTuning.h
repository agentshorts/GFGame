#pragma once

// Every stamina, sprint, dash, traversal, hearth and arc number lives here.
// UGFCharacterMovementComponent uses the asset when one is assigned, otherwise this class default.

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
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
};
