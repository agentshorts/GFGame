#pragma once

// Spawns one parked ferry per stop and flies a rider between the stops on that route.
// Also spawns hostile dinos on the route's wild hazard while someone is on foot nearby.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFFerryRules.h"
#include "GFFerryDirector.generated.h"

class AGFDino;
class AGFFerryDino;
class APawn;
class UGFFerryRiderComponent;

UCLASS(Config = Game)
class GFGAME_API AGFFerryDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFFerryDirector();

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	TArray<FGFFerryStop> Stops;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	TArray<FGFFerryRoute> Routes;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	FGFFerryTuning Tuning;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	FGFFerryMobSpawn MobSpawn;

	/** Server. Charges the fare or accepts a pass, then starts the flight. */
	FGFFerryResult RequestTrip(APawn* Rider, FName FromStop);

	/** Server. Burns the pass material and mints one pass when PlanPass says so. */
	FGFFerryResult BuyPass(APawn* Rider, FName AtStop);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FFlight
	{
		TWeakObjectPtr<AGFFerryDino> Craft;
		TWeakObjectPtr<APawn> Rider;
		TWeakObjectPtr<UGFFerryRiderComponent> Ride;
		FVector From = FVector::ZeroVector;
		FVector To = FVector::ZeroVector;
		FVector Drop = FVector::ZeroVector;
		float Elapsed = 0.f;
		float Duration = 0.f;
		float Altitude = 0.f;
		float RiderLift = 0.f;
		bool bSawController = false;
		bool bDisabledMovement = false;
	};

	struct FRouteMob
	{
		TWeakObjectPtr<AGFDino> Dino;
		FName HazardId;
		float Age = 0.f;
	};

	void SpawnStopDinos();
	void StartFlight(APawn* Rider, const FGFFerryStop& From, const FGFFerryStop& To);
	/** True when the flight entry should be dropped. */
	bool AdvanceFlight(FFlight& Flight, float DeltaSeconds);
	void FinishFlight(FFlight& Flight);
	void TickMobs(float DeltaSeconds);
	TArray<FGFFerryRoutePerson> GatherPeople() const;
	void DestroySpawned();

	UPROPERTY()
	TArray<TObjectPtr<AGFFerryDino>> StopDinos;

	TArray<FFlight> Flights;
	TArray<FRouteMob> Mobs;
	float SpawnWait = 0.f;
	FRandomStream MobRng;
};
