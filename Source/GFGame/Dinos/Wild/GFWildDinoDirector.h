#pragma once

// Authority spawner for wild dinos around faction anchors. AGFGameMode starts it from
// StartupDirectorClasses. Spawn rows live in DefaultGame.ini (AGFWildDinoDirector::Spawns).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFDino.h"
#include "GFWildDinoDirector.generated.h"

/** One ring of wild dinos. AnchorId matches UGFTerritorySubsystem::FactionLayout. Location is the fallback point. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFWildDinoSpawn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Wild")
	FName AnchorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Wild")
	FName Species;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Wild", meta = (ClampMin = "0"))
	int32 Count = 1;

	/** Outer radius of the ring, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Wild", meta = (ClampMin = "0"))
	float Radius = 1500.f;

	/** Inner radius of the ring, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Wild", meta = (ClampMin = "0"))
	float MinRadius = 600.f;

	/** Used when AnchorId is not a faction centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Wild")
	FVector Location = FVector::ZeroVector;
};

/** One living or waiting spawn. Not a reflected type. */
struct FGFWildDinoSlot
{
	int32 SpawnIndex = INDEX_NONE;
	TWeakObjectPtr<AGFDino> Dino;
	bool bLived = false;
	float DeadAt = -1.f;
};

UCLASS(Config = Game)
class GFGAME_API AGFWildDinoDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFWildDinoDirector();

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wild")
	TArray<FGFWildDinoSpawn> Spawns;

	/** Living dinos spawned by this director. Further slots wait. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wild", meta = (ClampMin = "0"))
	int32 MaxAlive = 24;

	/** Seconds after a spawned dino is destroyed before its slot spawns again. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wild", meta = (ClampMin = "0"))
	float RespawnSeconds = 60.f;

	/**
	 * False: leave the tame state Wild. True: mark Hostile with an empty target key.
	 * Proximity aggro and noise still run either way.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Wild")
	bool bSpawnHostile = false;

	/**
	 * Uniform-by-area point on the horizontal ring. Distance from Center is in [MinRadius, Radius].
	 * Z is Center.Z. Swapped radii are ordered first.
	 */
	static FVector PickRingPoint(const FVector& Center, float MinRadius, float Radius, FRandomStream& Rng);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void RebuildSlots();
	void FillSlots();
	AGFDino* SpawnOne(const FGFWildDinoSpawn& Entry);
	FVector ResolveCenter(const FGFWildDinoSpawn& Entry) const;
	float ResolveGroundZ(const FVector& Center, float X, float Y) const;
	int32 CountAlive() const;

	TArray<FGFWildDinoSlot> Slots;
	FRandomStream Rng;
};
