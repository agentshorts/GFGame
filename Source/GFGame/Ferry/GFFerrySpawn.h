#pragma once

// Where a new player stands when the map has no PlayerStart. The anchor id is config.

#include "CoreMinimal.h"
#include "GFTerritoryTypes.h"

struct GFGAME_API FGFMapPlayerStart
{
	FName Tag;
	FVector Location = FVector::ZeroVector;
};

struct GFGAME_API FGFPlayerStartRules
{
	/** Added to the anchor center when the map has no PlayerStart. */
	static constexpr float FallbackZ = 120.f;

	/**
	 * Anchor center + FallbackZ. Unknown or empty anchor id is (0, 0, FallbackZ).
	 * Layout entries are territory anchors (FactionLayout).
	 * This is the point FGFSpawnDefaults::StarterFallback returns for the same id.
	 * ChoosePlayerStart uses it when the map has no PlayerStart.
	 */
	static FVector FallbackLocation(FName AnchorId, const TArray<FGFTerritoryFaction>& Layout);

	/** Ferry perch. (0, -300, 100), yaw 36. Not the no-PlayerStart spawn. */
	static FVector DefaultPerchLocation();
	static FRotator DefaultPerchRotation();
	static FTransform EmptyMapStart();

	/**
	 * Index of the start whose Tag equals AnchorId, if one exists.
	 * Otherwise the first start. INDEX_NONE when Starts is empty.
	 */
	static int32 ChooseStartIndex(const TArray<FGFMapPlayerStart>& Starts, FName AnchorId);
};
