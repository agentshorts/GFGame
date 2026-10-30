#pragma once

// Faction land as discs that grow with membership. A faction with a RadiusByMembers table uses that
// radius: the index is the effective member count, and a count past the end uses the last entry. An empty
// table uses FGFTerritoryCurve. Either way the radius number is FGFSafeRegion::Radius (for the curve, the
// base is shifted by one step so every member, including the first, adds RadiusPerMember). Inside tests
// and border queries go through one cached FGFSafeCircles.
//
// Hazard boxes live on this map. GetHazardAt returns one only where GetZoneAt is wild, so faction land
// and the neutral disc both override a hazard. The box is horizontal; Z is ignored. Kind is a name.
//
// Overlap rule (nearest center, then size, then name):
//   A point inside several discs belongs to the faction whose center is closest in the horizontal plane.
//   Distances within OverlapTieSlopCm (1 cm) count as a tie. The tied faction with more effective members
//   wins. If the member counts match too, the lexicographically smaller faction id wins (case-insensitive),
//   so the result does not depend on array order and does not flicker. The loser does not own the point.
//   Overlap does not become neutral. Neutral is only the optional disc, and only where no faction owns.
//   Z is ignored. A nameless faction owns nothing. With an empty radius table, a faction with no
//   effective members owns nothing. A set table uses index 0 for that count.
//
// IsSafeFor(FName) and IsSafeFor(FGFSocialPlayer) match the faction to that owner (own land only; dinos use this).
// The IGFSafeZoneQuery override IsSafeFor(FGFSafeSubject) adds faction protection (FGFProtectionRules): own land
// always, another faction's land for the host, a protected visitor or a faction guest, never neutral or wild.
// HubRadius is the extra (#136 option 1): a subject with no faction is safe inside that circle. It does not
// change IsInsideSafe, OwnerAt, or the giant's pathing shape. Radius 0 leaves it off.

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
	/** Config boxes. GetHazardAt reads these. */
	TArray<FGFTerritoryHazard> Hazards;

	/**
	 * Max members that may affect the radius. 0 = do not clamp.
	 * The world subsystem sets this from AGFGameState::MaxPerFaction only.
	 */
	int32 MaxMembers = 0;

	FGFTerritoryFaction* FindFaction(FName Id);
	const FGFTerritoryFaction* FindFaction(FName Id) const;
	bool SetMembers(FName Id, int32 InMembers);

	int32 EffectiveMembers(int32 Members) const;

	/** The FGFSafeRegion whose Radius() is this faction's land. A nameless faction has radius 0. */
	FGFSafeRegion RegionFor(const FGFTerritoryFaction& Faction) const;
	float RadiusOf(const FGFTerritoryFaction& Faction) const;
	/** Horizontal disc area, PI * radius^2. */
	float AreaOf(const FGFTerritoryFaction& Faction) const;

	/**
	 * Owned discs. Members stays the effective head count so overlap can still compare size.
	 * With an empty RadiusByMembers table, BaseRadius is the one-member curve radius and
	 * RadiusPerExtraMember is the curve step, so FGFSafeRegion::Radius() equals the curve.
	 * With a table, BaseRadius and MaxRadius are that count's radius and RadiusPerExtraMember is 0,
	 * so Radius() is the table value. This does not collapse a disc to a single member.
	 */
	TArray<FGFSafeRegion> CopySafeRegions() const;

	/**
	 * True only when both factions are on the map, both radii are greater than zero, and the horizontal
	 * distance between their centers is less than or equal to the sum of the radii.
	 */
	bool Touches(FName A, FName B) const;

	FGFTerritoryZoneHit GetZoneAt(const FVector& Location) const;

	/** First hazard box that contains Location, and only when GetZoneAt is wild. Z is ignored. */
	FGFTerritoryHazardHit GetHazardAt(const FVector& Location) const;

	/** True only when FactionId owns this point. None is never safe. */
	bool IsSafeFor(FName FactionId, const FVector& Location) const;
	/** Uses Who.Faction. Who.bHost does not add land. Own land only (no visitor protection). */
	bool IsSafeFor(const FGFSocialPlayer& Who, const FVector& Location) const;

	/** "In safe land AND protected": faction land with FGFProtectionRules::IsProtectedOn (host, protected, guest). */
	virtual bool IsSafeFor(const FGFSafeSubject& Who, const FVector& Location) const override;
	virtual bool IsHazardAt(const FVector& Location) const override;
	virtual int32 SafeRegionCount() const override;
	virtual bool SafeRegionAt(int32 Index, FName& OutId, FVector& OutCenter, float& OutRadius) const override;

	/**
	 * Hub shelter for a subject with no faction (#136 option 1). The world subsystem sets the centre to the
	 * starter anchor and the radius from the online player count (1200 + 400 per extra player, cap 3200).
	 * IsInsideSafe ignores it, so it is not a pathing disc.
	 */
	FVector HubCenter = FVector::ZeroVector;
	float HubRadius = 0.f;
	/** The faction that owns this point (GetZoneAt), or None for neutral and wild. */
	virtual FName OwnerAt(const FVector& Location) const override;

	/** Protection book used by IsSafeFor(FGFSafeSubject). Tests fill Access; the world subsystem sets AccessSource. */
	FGFFactionAccess Access;
	/** Live book (the social component's replicated copy). Null result falls back to Access. */
	TFunction<const FGFFactionAccess*()> AccessSource;
	const FGFFactionAccess& GetAccess() const;

	virtual bool IsInsideSafe(const FVector& Location) const override;
	virtual FVector ClosestSafePoint(const FVector& Location) const override;
	virtual bool ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const override;
	virtual bool HasAnySafe() const override;
	/** GetHazardAt(Location).bHazard: wild hazards only (a hazard box under faction land is not one). */
	virtual bool IsHazardAt(const FVector& Location) const override;

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
