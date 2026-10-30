#pragma once

// World "noise" events: something loud happened at a spot (today: a player emoting outside safe territory in a mode
// with free-roaming enemies). Anything that reacts to noise (wild dinos via UGFTamingComponent) listens here, so the
// module that makes the noise doesn't need to know who hears it. Server only by convention.
#include "CoreMinimal.h"

class UWorld;

struct GFCORE_API FGFNoiseEvent
{
	FVector Location = FVector::ZeroVector;
	/** cm: listeners farther than this ignore it. */
	float Radius = 0.f;
	/** Player key of whoever made it (dinos turn on them). */
	FString SourceKey;
	/** Why ("Emote"). */
	FName Tag;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnNoise, UWorld* /*World*/, const FGFNoiseEvent& /*Noise*/);

struct GFCORE_API FGFNoise
{
	/** Every noise in any world (listeners filter by their own world). */
	static FGFOnNoise& OnNoise();
	/** Broadcasts Noise to every listener (they check the world themselves). */
	static void Raise(UWorld* World, const FGFNoiseEvent& Noise);
};
