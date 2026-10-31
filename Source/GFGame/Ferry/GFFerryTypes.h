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

	/** Tier-1 building blocks the fare may burn. Any mix that adds up to FareCount is enough. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	TArray<FName> FareItems;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	int32 FareCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName PassMaterialItem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	int32 PassPrice = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName PassItem;

	/** Door to door, including takeoff and landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float FlightSeconds = 13.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float CruiseAltitude = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float BoardRadius = 400.f;
};

/**
 * One lava-bridge spawn box. Species is config. A point is legal only where the territory
 * hazard id is still wild, so owned land never receives a mob.
 */
USTRUCT(BlueprintType)
struct GFGAME_API FGFFerryMobBox
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName Species;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FVector Center = FVector::ZeroVector;

	/** Horizontal half-size. 500 by 300 is a 1000 by 600 cm box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FVector2D HalfExtent = FVector2D(500.f, 300.f);

	/** GetHazardAt must return this id (wild ground inside that hazard). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	FName HazardId;
};

/**
 * Land-route mobs. Level is the nearby party's level plus LevelBonus. There is no dino level
 * stat; AttackDamage is scaled by (1 + MobDamagePerLevel * (Level - 1)) and written onto the spawned AGFDino.
 * Each box spawns on its own timer, up to MaxAlivePerBox.
 */
USTRUCT(BlueprintType)
struct GFGAME_API FGFFerryMobSpawn
{
	GENERATED_BODY()

	/** Added to the highest in-party companion level among players who woke the box. An empty party counts as 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	int32 LevelBonus = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry")
	float MobDamagePerLevel = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float SpawnIntervalSeconds = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	int32 MaxAlivePerBox = 4;

	/** A non-flying player within this distance of a box wakes that box. 4000 cm is 40 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float ActivationMargin = 4000.f;

	/** Despawn a mob that wanders farther than this past its box. 800 cm is 8 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float LeashMargin = 800.f;

	/** Idle despawn looks for a player within this distance. 6000 cm is 60 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float DespawnRange = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Ferry", meta = (ClampMin = "0"))
	float IdleDespawnSeconds = 60.f;
};
