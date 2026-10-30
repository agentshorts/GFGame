#pragma once

// Faction land as discs that grow with membership. Radius and area come from FGFTerritoryCurve, and the
// radius number itself is FGFSafeRegion::Radius (base shifted by one step so every member, including the
// first, adds RadiusPerMember). Circle tests and border queries are FGFSafeCircles, so this does not grow
// a second geometry.
//
// Overlap rule (nearest center, then size, then name):
//   A point inside several discs belongs to the faction whose center is closest in the horizontal plane.
//   Distances within OverlapTieSlopCm (1 cm) count as a tie. The tied faction with more effective members
//   wins. If the member counts match too, the lexicographically smaller faction id wins (case-insensitive),
//   so the result does not depend on array order and does not flicker. The loser does not own the point.
//   Overlap does not become neutral. Neutral is only the optional disc, and only where no faction owns.
//   Z is ignored. A faction with no effective members owns nothing.

#include "CoreMinimal.h"
#include "GFSafeZone.h"
#include "GFTerritoryTypes.h"

class GFGAME_API FGFTerritoryMap : public IGFSafeZoneQuery
{
public:
	/** Distances within this many centimetres are the same distance for the overlap rule. */
	static constexpr float OverlapTieSlopCm = 1.f;

	FGFTerritoryCurve Curve;
	FGFNeutralZone Neutral;
	TArray<FGFTerritoryFaction> Factions;

	/**
	 * Max members that may affect the radius. 0 = do not clamp.
	 * The world subsystem sets this from FGFTerritoryLimits (session cap and AGFGameState::MaxPerFaction).
	 */
	int32 MaxMembers = 0;

	void SetDefaultFactions();

	FGFTerritoryFaction* FindFaction(FName Id);
	const FGFTerritoryFaction* FindFaction(FName Id) const;
	bool SetMembers(FName Id, int32 InMembers);

	int32 EffectiveMembers(int32 Members) const;

	/** The FGFSafeRegion whose Radius() is this faction's land. Empty factions come back with radius 0. */
	FGFSafeRegion RegionFor(const FGFTerritoryFaction& Faction) const;
	float RadiusOf(const FGFTerritoryFaction& Faction) const;
	/** Horizontal disc area, PI * radius^2. */
	float AreaOf(const FGFTerritoryFaction& Faction) const;

	TArray<FGFSafeRegion> CopySafeRegions() const;

	FGFTerritoryZoneHit GetZoneAt(const FVector& Location) const;

	/** True only on the subject's own faction land. Wild creatures, unaffiliated players, and hosts with no faction are not safe. */
	bool IsSafeFor(const FGFTerritorySubject& Who, const FVector& Location) const;

	virtual bool IsInsideSafe(const FVector& Location) const override;
	virtual FVector ClosestSafePoint(const FVector& Location) const override;
	virtual bool ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const override;
	virtual bool HasAnySafe() const override;

private:
	bool BeatsOnTie(const FGFTerritoryFaction& Challenger, const FGFTerritoryFaction& Holder) const;
	FGFSafeCircles BuildCircles() const;
};
