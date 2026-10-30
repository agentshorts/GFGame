#pragma once

// Every stamina, sprint, dash, hearth and arc number lives here.
// A traversal kit should add fields to this asset instead of planting new constants in the movement component.
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
};
