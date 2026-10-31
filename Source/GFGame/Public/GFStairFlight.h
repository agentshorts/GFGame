#pragma once

// One flight of hub stairs. The visible treads are static meshes with no pawn collision.
// A simple box ramp (plus a flat pad at each end) is what the capsule walks, so it slides
// instead of catching a riser. Foot IK reads TreadTopZ and plants on the visible steps.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFStairFlight.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct GFGAME_API FGFStairFlightSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Stair")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Stair", meta = (ClampMin = "1"))
	int32 Steps = 4;

	/** Riser of one visible step, cm. The ramp pitch is atan(Riser / Tread). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Stair", meta = (ClampMin = "1"))
	float RiserCm = 20.f;

	/** Tread depth along the walk, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Stair", meta = (ClampMin = "1"))
	float TreadCm = 32.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Stair", meta = (ClampMin = "40"))
	float WidthCm = 180.f;

	/** World location of the bottom of the first riser. The actor is spawned here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Stair")
	FVector Origin = FVector::ZeroVector;

	/** Yaw, degrees. Zero walks toward +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Stair")
	float YawDeg = 0.f;

	float RiseCm() const { return static_cast<float>(Steps) * RiserCm; }
	float RunCm() const { return static_cast<float>(Steps) * TreadCm; }
	float PitchDegrees() const { return FMath::RadiansToDegrees(FMath::Atan2(RiserCm, TreadCm)); }
};

UCLASS()
class GFGAME_API AGFStairFlight : public AActor
{
	GENERATED_BODY()

public:
	AGFStairFlight();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Spec, EditAnywhere, BlueprintReadOnly, Category = "GF|Stair")
	FGFStairFlightSpec Spec;

	UFUNCTION()
	void OnRep_Spec();

	/** Rebuilds the step meshes and the ramp from Spec. Safe to call again. */
	void Rebuild();

	UBoxComponent* GetRamp() const { return Ramp; }

	/** Visible treads, in step order. They do not block the pawn. */
	TArray<UStaticMeshComponent*> GetStepMeshes() const;

	/**
	 * World Z of the tread under WorldPoint, when the point is over this flight.
	 * The approach and the landing report the flat ends. False when the point is off to the side.
	 */
	bool TreadTopZ(const FVector& WorldPoint, float& OutWorldZ) const;

	/** Highest tread among the flights in World. False when the point is over none of them. */
	static bool SurfaceZAt(const UWorld* World, const FVector& WorldPoint, float& OutSurfaceZ);

	/** Capsule centre on the lower pad. LocalY shifts a second walker sideways. */
	FVector ApproachLocation(float CapsuleHalfHeight, float LocalY = 0.f) const;

	/** Capsule centre on the upper landing. */
	FVector LandingLocation(float CapsuleHalfHeight, float LocalY = 0.f) const;

	/**
	 * Ramp box in actor space. HalfExtent is the box half size.
	 * The top face runs from the local origin up to (Run, 0, Rise).
	 */
	static void RampPlacement(const FGFStairFlightSpec& InSpec, FVector& OutCenter, FRotator& OutRotation, FVector& OutHalfExtent);

	static constexpr float RampThicknessCm = 12.f;

private:
	void ClearParts();

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> Ramp;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> Parts;

	bool bPartsBuilt = false;
	int32 BuiltSteps = 0;
	float BuiltRiser = 0.f;
	float BuiltTread = 0.f;
	float BuiltWidth = 0.f;
};
