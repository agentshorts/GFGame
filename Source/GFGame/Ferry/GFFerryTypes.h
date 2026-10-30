#pragma once

// Ferry config records. Faction ids, item ids, and species names come from DefaultGame.ini, not from code.

#include "CoreMinimal.h"
#include "GFFerryTypes.generated.h"

/** One landing. AnchorId is a territory faction id. StopA on a route is the home stop. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFFerryStop
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName AnchorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FVector Location = FVector::ZeroVector;
};

/** One trip between two stops. LandRouteHazardId is the wild hazard box beside the land route. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFFerryRoute
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName Id;

	/** Home stop. Trips are listed from this end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName StopA;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName StopB;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName LandRouteHazardId;
};

/** Fare, lifetime pass, and the flight. All of these are config. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFFerryTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName FareItem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	int32 FareCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName PassMaterialItem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	int32 PassPrice = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName PassItem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float FlightSeconds = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float CruiseAltitude = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float BoardRadius = 400.f;
};

/**
 * Land-route mobs. MobLevel scales AttackDamage by (1 + MobDamagePerLevel * (MobLevel - 1)).
 * There is no dino level stat; the product is the damage written onto the spawned AGFDino.
 */
USTRUCT(BlueprintType)
struct GFGAME_API FGFFerryMobSpawn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	TArray<FName> MobSpecies;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "1"))
	int32 MobLevel = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	float MobDamagePerLevel = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float SpawnIntervalSeconds = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	int32 MaxAlive = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float ActivationMargin = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float IdleDespawnSeconds = 60.f;
};
