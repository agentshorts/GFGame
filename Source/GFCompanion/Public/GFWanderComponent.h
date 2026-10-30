#pragma once

// Idle wander for a wild AGFDino that has no attack target and is not resting.
// AGFDino::Tick calls TickWander from its default branch. The director sets Home to the spawn point.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFWanderComponent.generated.h"

class AGFDino;

UCLASS(ClassGroup = (GF), Config = Game, meta = (BlueprintSpawnableComponent))
class GFCOMPANION_API UGFWanderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFWanderComponent();

	/** Horizontal disc around Home. Default 800 cm. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wander", meta = (ClampMin = "0"))
	float WanderRadius = 800.f;

	/** Seconds between new wander points. Each pick is random in [Min, Max]. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wander", meta = (ClampMin = "0"))
	float WanderEveryMin = 4.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wander", meta = (ClampMin = "0"))
	float WanderEveryMax = 9.f;

	/** Stop walking once this close to the wander point (cm, horizontal). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wander", meta = (ClampMin = "0"))
	float StopDistance = 100.f;

	/** cm/s when the species is missing from WalkSpeedBySpecies. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wander", meta = (ClampMin = "0"))
	float DefaultWalkSpeed = 250.f;

	/** cm/s per species. Empty key uses DefaultWalkSpeed. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wander")
	TMap<FName, float> WalkSpeedBySpecies;

	void SetHome(const FVector& InHome);
	const FVector& GetHome() const { return Home; }
	bool HasHome() const { return bHomeSet; }

	FVector GetWanderTarget() const { return WanderTarget; }
	bool HasWanderTarget() const { return bHasTarget; }

	/** Uniform point in the horizontal disc. Z stays on Home. */
	static FVector PickWanderPoint(const FVector& InHome, float Radius, FRandomStream& Rng);

	/** WalkSpeedBySpecies, else DefaultWalkSpeed. Reads the class config. */
	static float SpeedFor(FName Species);

	/** Copies SpeedFor onto the owner's MaxWalkSpeed. */
	void ApplyWalkSpeed();

	/** Authority wander step. No-op unless the owner is a wild idle dino with no attack target. */
	void TickWander(float DeltaSeconds);

private:
	void SeedRng();

	FVector Home = FVector::ZeroVector;
	FVector WanderTarget = FVector::ZeroVector;
	FRandomStream Rng;
	float NextPickAt = 0.f;
	bool bHomeSet = false;
	bool bHasTarget = false;
	bool bRngSeeded = false;
};
