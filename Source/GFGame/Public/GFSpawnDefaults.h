#pragma once

// Where a new player stands when the map has no PlayerStart, and which faction they join.
// The ferry branch (PR #146) adds DefaultSpawnAnchor and FGFPlayerStartRules on its own.
// This header does not add that property. StarterFallback is the same point FallbackLocation
// returns for the starter id: the anchor centre plus FallbackZ. A tagged PlayerStart still wins,
// and any other map PlayerStart still wins over this fallback, so mini-game maps keep their starts.
// When the ferry change merges, keep DefaultSpawnAnchor equal to StarterFaction and call
// StarterFallback for the empty-map case instead of growing a second anchor.

#include "CoreMinimal.h"

/** One faction anchor, copied from the territory layout so this header stays free of that module. */
struct GFGAME_API FGFSpawnAnchor
{
	FName Id;
	FVector Center = FVector::ZeroVector;
};

struct GFGAME_API FGFSpawnDefaults
{
	/** Added to the anchor centre when the map has no PlayerStart. */
	static constexpr float FallbackZ = 120.f;

	/** Starter faction. Rime is the starter zone. */
	static FName StarterFactionId();

	/**
	 * Anchor centre + FallbackZ. An unknown or empty id, or a missing anchor, is (0, 0, FallbackZ).
	 */
	static FVector StarterFallback(FName StarterId, const TArray<FGFSpawnAnchor>& Anchors);
};
