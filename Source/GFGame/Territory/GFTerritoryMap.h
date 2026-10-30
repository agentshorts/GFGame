#pragma once

// Faction land as discs that grow with membership. Radius and area come from FGFTerritoryCurve, and the
// radius number itself is FGFSafeRegion::Radius (base shifted by one step so every member, including the
// first, adds RadiusPerMember). Inside tests and border queries go through one cached FGFSafeCircles.
//
// Overlap rule (nearest center, then size, then name):
//   A point inside several discs belongs to the faction whose center is closest in the horizontal plane.
//   Distances within OverlapTieSlopCm (1 cm) count as a tie. The tied faction with more effective members
//   wins. If the member counts match too, the lexicographically smaller faction id wins (case-insensitive),
//   so the result does not depend on array order and does not flicker. The loser does not own the point.
//   Overlap does not become neutral. Neutral is only the optional disc, and only where no faction owns.
//   Z is ignored. A faction with no effective members owns nothing.
//
// IsSafeFor matches FGFSocialPlayer::Faction to that owner. bHost is carried on the social player and is
// not a land grant.

#include "CoreMinimal.h"
#include "GFSafeZone.h"
#include "GFSocialTypes.h"
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
	 * The world subsystem sets this from AGFGameState::MaxPerFaction only.
	 */
	int32 MaxMembers = 0;

	FGFTerritoryFaction* FindFaction(FName Id);
	const FGFTerritoryFaction* FindFaction(FName Id) const;
	bool SetMembers(FName Id, int32 InMembers);

	int32 EffectiveMembers(int32 Members) const;

	/** The FGFSafeRegion whose Radius() is this faction's land. Empty factions come back with radius 0. */
	FGFSafeRegion RegionFor(const FGFTerritoryFaction& Faction) const;
	float RadiusOf(const FGFTerritoryFaction& Faction) const;
	/** Horizontal disc area, PI * radius^2. */
	float AreaOf(const FGFTerritoryFaction& Faction) const;

	/**
	 * Owned discs. Members stays the effective head count and BaseRadius is the one-member radius, so
	 * FGFSafeRegion::Radius() equals the curve. This does not collapse a disc to a single member.
	 */
	TArray<FGFSafeRegion> CopySafeRegions() const;

	FGFTerritoryZoneHit GetZoneAt(const FVector& Location) const;

	/** True only when FactionId owns this point. None is never safe. */
	bool IsSafeFor(FName FactionId, const FVector& Location) const;
	/** Uses Who.Faction. Who.bHost does not add land. */
	bool IsSafeFor(const FGFSocialPlayer& Who, const FVector& Location) const;

	virtual bool IsInsideSafe(const FVector& Location) const override;
	virtual FVector ClosestSafePoint(const FVector& Location) const override;
	virtual bool ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const override;
	virtual bool HasAnySafe() const override;

private:
	bool BeatsOnTie(const FGFSafeRegion& Challenger, const FGFSafeRegion& Holder) const;
	/** FGFSafeCircles::IsInsideSafe on a one-disc list: the same test the safe-zone union uses. */
	static bool InDisc(const FGFSafeRegion& Region, const FVector& Location);
	const FGFSafeCircles& Circles() const;
	bool CircleCacheMatches() const;

	mutable bool bCirclesReady = false;
	mutable int32 CachedMaxMembers = 0;
	mutable float CachedBase = 0.f;
	mutable float CachedPer = 0.f;
	mutable float CachedMaxRadius = 0.f;
	mutable TArray<FGFTerritoryFaction> CachedFactions;
	mutable FGFSafeCircles CachedCircles;
};
