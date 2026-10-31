#pragma once

// Spawns one parked ferry and one greybox stand per stop, and flies a rider between linked stops.
// Also spawns hostile dinos on a route's wild hazard while someone is on foot nearby.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFFerryRules.h"
#include "GFFerryDirector.generated.h"

class AGFDino;
class AGFFerryDino;
class AGFFerryStand;
class APawn;
class UGFFerryRiderComponent;
class UStaticMeshComponent;

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

	/**
	 * Server. Charges the fare or accepts a pass, then starts the flight.
	 * The destination is the other end of the first route that touches FromStop.
	 * RequestId is the fare TxId. A second call with the same id does not charge and does not start another flight.
	 */
	FGFFerryResult RequestTrip(APawn* Rider, FName FromStop, const FGuid& RequestId);

	/**
	 * Server. Same charge and flight as RequestTrip, to a named stop on a route from FromStop.
	 * NoRoute when both stops exist and nothing links them.
	 */
	FGFFerryResult RequestTripTo(APawn* Rider, FName FromStop, FName ToStop, const FGuid& RequestId);

	/** Server. Burns the pass material and mints one pass. A failed mint rolls the burn back. */
	FGFFerryResult BuyPass(APawn* Rider, FName AtStop, const FGuid& RequestId);

	/** Test hook. The next StartFlight returns false and mints the fare back. Cleared when it trips. */
	bool bFailNextFlight = false;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FFlight
	{
		TWeakObjectPtr<AGFFerryDino> Craft;
		TWeakObjectPtr<APawn> Rider;
		TWeakObjectPtr<UGFFerryRiderComponent> Ride;
		TWeakObjectPtr<UStaticMeshComponent> Basket;
		FVector From = FVector::ZeroVector;
		FVector To = FVector::ZeroVector;
		FVector Drop = FVector::ZeroVector;
		FVector ArriveBasket = FVector::ZeroVector;
		FVector BasketFrom = FVector::ZeroVector;
		float Elapsed = 0.f;
		float Duration = 0.f;
		float Altitude = 0.f;
		bool bSawController = false;
		bool bDisabledMovement = false;
		bool bBasketGrabbed = false;
		bool bBasketDown = false;
		bool bBasketReleased = false;
	};

	struct FRouteMob
	{
		TWeakObjectPtr<AGFDino> Dino;
		FName HazardId;
		float Age = 0.f;
	};

	void SpawnStopDinos();
	void SpawnStands();
	bool StartFlight(APawn* Rider, const FGFFerryStop& From, const FGFFerryStop& To);
	/** True when the flight entry should be dropped. */
	bool AdvanceFlight(FFlight& Flight, float DeltaSeconds);
	void FinishFlight(FFlight& Flight);
	void TickMobs(float DeltaSeconds);
	TArray<FGFFerryRoutePerson> GatherPeople() const;
	void DestroySpawned();

	UPROPERTY()
	TArray<TObjectPtr<AGFFerryDino>> StopDinos;

	UPROPERTY()
	TArray<TObjectPtr<AGFFerryStand>> Stands;

	TArray<FFlight> Flights;
	TArray<FRouteMob> Mobs;
	float SpawnWait = 0.f;
	FRandomStream MobRng;
};
