// Fills an empty map with a sun, sky, and fog. Spawned with the game state
// (AGFGameMode::StartupDirectorClasses). The actor replicates so every machine
// runs BeginPlay; the lights are created there and are not replicated.
// A piece the level already has is left alone.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFEnvironmentDirector.generated.h"

UCLASS(Config = Game)
class GFGAME_API AGFEnvironmentDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFEnvironmentDirector();

	virtual void BeginPlay() override;

	/**
	 * Spawns whichever of the sun, sky atmosphere, sky light, and height fog this world
	 * does not already have. Safe to call more than once.
	 */
	void EnsureEnvironment();

	/** Lux. 10 keeps the sky atmosphere in its usual range. */
	UPROPERTY(Config, EditAnywhere, Category = "GF|World", meta = (ClampMin = "0"))
	float SunIntensity;

	/**
	 * Exposure bias added only when this director creates the sun.
	 * Auto exposure is off, so 10 lux would otherwise be black.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "GF|World")
	float ExposureBias;
};
