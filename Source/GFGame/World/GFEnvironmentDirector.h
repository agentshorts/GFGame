// Fills an empty map with a sun, sky, and fog. Spawned with the game state
// (AGFGameMode::StartupDirectorClasses). The actor replicates so every machine
// runs BeginPlay; the lights are created there and are not replicated.
// A piece the level already has is left alone.
// Runtime lights are Movable. Static lighting is off, so nothing asks for a bake.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFEnvironmentDirector.generated.h"

/**
 * Exposure value printed by the Lumen cached-lighting warning when auto exposure is off.
 * A bias of +11 is what showed up as "Exposure: -11.0".
 */
inline float GFFixedExposureEV(float ExposureBias)
{
	return -ExposureBias;
}

/** +0 when the value is a zero of either sign, so %.1f prints "0.0" and not "-0.0". */
inline float GFNormalizedExposureEV(float ExposureEV)
{
	return FMath::IsNearlyZero(ExposureEV) ? 0.f : ExposureEV;
}

/**
 * UE 5.8 window for r.EyeAdaptation.CachedLightingPreExposure.
 * The default offset of 4 is the range [-8, 12]. An offset of 0 (this project's fixed exposure) is [-12, 8].
 */
inline float GFLumenCachedLightingMinEV(float PreExposureOffsetEV)
{
	return PreExposureOffsetEV - 12.f;
}

inline float GFLumenCachedLightingMaxEV(float PreExposureOffsetEV)
{
	return PreExposureOffsetEV + 8.f;
}

inline bool GFLumenCachedLightingCovers(float PreExposureOffsetEV, float ExposureEV)
{
	return ExposureEV >= GFLumenCachedLightingMinEV(PreExposureOffsetEV)
		&& ExposureEV <= GFLumenCachedLightingMaxEV(PreExposureOffsetEV);
}

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
	 * Auto exposure is off. 0 reads right at 10 lux; 11 blows the hub out to white.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "GF|World")
	float ExposureBias;
};
