#pragma once

// Cross-module attention, so social play can call Big Grumble without the giant module depending on social.
// Server only by convention. Holds are the grab tethers right now. A taunt is a shout at a spot.
#include "CoreMinimal.h"

class UWorld;

/** One player being held by someone else. */
struct GFCORE_API FGFAttentionHold
{
	FString TargetKey;
	FString HolderKey;
	FString HolderName;
};

/** A player called Big Grumble toward a spot. */
struct GFCORE_API FGFGiantTaunt
{
	FVector Location = FVector::ZeroVector;
	/** cm. Listeners farther than this, and outside it, are not "in the area". */
	float Radius = 0.f;
	FString SourceKey;
	FString SourceName;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnGiantTaunt, UWorld* /*World*/, const FGFGiantTaunt& /*Taunt*/);

struct GFCORE_API FGFAttention
{
	/** Replaces the holds published for World (an empty list clears them). */
	static void SetHolds(UWorld* World, TArray<FGFAttentionHold> Holds);
	static void ClearWorld(UWorld* World);
	/** The live hold on TargetKey in World, if someone else has them. */
	static bool FindHold(const UWorld* World, const FString& TargetKey, FGFAttentionHold& Out);

	static FGFOnGiantTaunt& OnTaunt();
	/** Broadcasts Taunt (listeners check the world themselves). No world: nothing. */
	static void RaiseTaunt(UWorld* World, const FGFGiantTaunt& Taunt);
};
