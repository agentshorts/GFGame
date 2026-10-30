#pragma once

// Voxel overworld built from UGFTerritorySubsystem's config (curve, faction anchors, hazard boxes).
// Theme uses each faction's largest radius, so the ground is already the right blocks when membership grows.
// Build rights use the live radius. Rime and Verdant may both build where their live discs overlap.

#include "CoreMinimal.h"
#include "GFGameState.h"
#include "GFTerritoryMap.h"

/** Block ids stamped into the hub. Rows live in Data/CSV/DT_Blocks.csv. */
namespace GFOverworldBlock
{
	inline FName Ice() { return FName(TEXT("Ice")); }
	inline FName Stone() { return FName(TEXT("Stone")); }
	inline FName Grass() { return FName(TEXT("Grass")); }
	inline FName Dirt() { return FName(TEXT("Dirt")); }
	inline FName Basalt() { return FName(TEXT("Basalt")); }
	inline FName Lava() { return FName(TEXT("Lava")); }
	inline FName Farmland() { return FName(TEXT("Farmland")); }
	inline FName Log() { return FName(TEXT("Log")); }
	inline FName Leaves() { return FName(TEXT("Leaves")); }
}

namespace GFOverworldFaction
{
	inline FName Rime() { return FName(TEXT("Rime")); }
	inline FName Verdant() { return FName(TEXT("Verdant")); }
	inline FName Cinderwrights() { return FName(TEXT("Cinderwrights")); }
}

/** Ember #D9542B. The Cinderwrights render colour. */
inline FLinearColor GFOverworldCinderColour()
{
	return FLinearColor(217.f / 255.f, 84.f / 255.f, 43.f / 255.f, 1.f);
}

/**
 * One generated hub. Cell (0,0,0) is the block whose bottom-centre sits at Origin.
 * The surface layer's top is world Z = 0. FillLayers blocks of ground, then air for trunks.
 */
struct GFGAME_API FGFOverworldLayout
{
	static constexpr float BlockCm = 100.f;
	static constexpr int32 FillLayers = 4;
	static constexpr int32 GridZ = 16;

	FVector Origin = FVector::ZeroVector;
	FIntVector GridSize = FIntVector(64, 64, 32);
	/** Cell Z of the walkable surface. Its top is world Z = 0. */
	int32 SurfaceZ = FillLayers - 1;

	FGFTerritoryCurve Curve;
	TArray<FGFTerritoryFaction> Factions;
	TArray<FGFTerritoryHazard> Hazards;

	bool IsInside(const FIntVector& Cell) const;
	FIntVector WorldToCell(const FVector& World) const;
	/** Surface cell under World, ignoring World.Z. */
	FIntVector SurfaceCell(const FVector& World) const;
	FName BlockAt(const FIntVector& Cell) const;
};

struct GFGAME_API FGFOverworldBuild
{
	/** Skirt of wild ground past the discs, boxes and ferry stops. Matches AGFOverworldGround::Margin. */
	static constexpr float DefaultMarginCm = 2000.f;

	static FGFOverworldLayout Make(
		const FGFTerritoryCurve& Curve,
		const TArray<FGFTerritoryFaction>& Factions,
		const TArray<FGFTerritoryHazard>& Hazards,
		float MarginCm = DefaultMarginCm);

	/**
	 * Live build rights for FactionId at World. Map.Factions carry the current member counts.
	 * Own disc, the Rime/Verdant overlap (both), someone else's disc, or wild.
	 */
	static EGFOverworldEdit Classify(const FGFTerritoryMap& Map, FName FactionId, const FVector& World);
};
