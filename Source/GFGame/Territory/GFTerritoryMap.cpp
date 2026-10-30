#include "GFTerritoryMap.h"

namespace
{
	bool InHazardBox(const FGFTerritoryHazard& Hazard, const FVector& Location)
	{
		const float DX = FMath::Abs(Location.X - Hazard.Center.X);
		const float DY = FMath::Abs(Location.Y - Hazard.Center.Y);
		return DX <= FMath::Abs(Hazard.HalfExtent.X) && DY <= FMath::Abs(Hazard.HalfExtent.Y);
	}
}

FGFTerritoryFaction* FGFTerritoryMap::FindFaction(FName Id)
{
	return Factions.FindByPredicate([Id](const FGFTerritoryFaction& Faction) { return Faction.Id == Id; });
}

const FGFTerritoryFaction* FGFTerritoryMap::FindFaction(FName Id) const
{
	return Factions.FindByPredicate([Id](const FGFTerritoryFaction& Faction) { return Faction.Id == Id; });
}

bool FGFTerritoryMap::SetMembers(FName Id, int32 InMembers)
{
	if (FGFTerritoryFaction* Faction = FindFaction(Id))
	{
		Faction->Members = FMath::Max(0, InMembers);
		return true;
	}
	return false;
}

int32 FGFTerritoryMap::EffectiveMembers(int32 Members) const
{
	int32 Count = FMath::Max(0, Members);
	if (MaxMembers > 0)
	{
		Count = FMath::Min(Count, MaxMembers);
	}
	return Count;
}

FGFSafeRegion FGFTerritoryMap::RegionFor(const FGFTerritoryFaction& Faction) const
{
	FGFSafeRegion Region;
	Region.Id = Faction.Id;
	Region.Center = Faction.Center;
	const int32 Count = EffectiveMembers(Faction.Members);
	if (Faction.Id.IsNone())
	{
		Region.Members = 0;
		Region.BaseRadius = 0.f;
		Region.RadiusPerExtraMember = 0.f;
		Region.MaxRadius = 0.f;
		return Region;
	}

	if (Faction.RadiusByMembers.Num() > 0)
	{
		// Members stays the effective count so overlap can still break ties on size. Radius() is the
		// table value because the per-member step is zero and the cap equals the base.
		const int32 Idx = FMath::Min(Count, Faction.RadiusByMembers.Num() - 1);
		const float Radius = FMath::Max(0.f, Faction.RadiusByMembers[Idx]);
		Region.Members = Count;
		Region.BaseRadius = Radius;
		Region.RadiusPerExtraMember = 0.f;
		Region.MaxRadius = Radius;
		return Region;
	}

	if (Count <= 0)
	{
		Region.Members = 0;
		Region.BaseRadius = 0.f;
		Region.RadiusPerExtraMember = 0.f;
		Region.MaxRadius = 0.f;
		return Region;
	}

	// FGFSafeRegion::Radius = Base + PerExtra * (Members - 1). Shift the base up by one step so the
	// published radius is Curve.BaseRadius + Curve.RadiusPerMember * Count (every member counts).
	const float PerMember = Curve.RadiusPerMember;
	const float OneMember = FMath::Max(0.f, Curve.BaseRadius + PerMember);
	Region.Members = Count;
	Region.BaseRadius = OneMember;
	Region.RadiusPerExtraMember = PerMember;
	Region.MaxRadius = Curve.MaxRadius > 0.f ? Curve.MaxRadius : TNumericLimits<float>::Max();
	return Region;
}

float FGFTerritoryMap::RadiusOf(const FGFTerritoryFaction& Faction) const
{
	return RegionFor(Faction).Radius();
}

float FGFTerritoryMap::AreaOf(const FGFTerritoryFaction& Faction) const
{
	const float Radius = RadiusOf(Faction);
	return UE_PI * Radius * Radius;
}

bool FGFTerritoryMap::Touches(FName A, FName B) const
{
	const FGFTerritoryFaction* FactionA = FindFaction(A);
	const FGFTerritoryFaction* FactionB = FindFaction(B);
	if (!FactionA || !FactionB)
	{
		return false;
	}
	const float RadiusA = RadiusOf(*FactionA);
	const float RadiusB = RadiusOf(*FactionB);
	if (RadiusA <= 0.f || RadiusB <= 0.f)
	{
		return false;
	}
	return FVector::Dist2D(FactionA->Center, FactionB->Center) <= RadiusA + RadiusB;
}

TArray<FGFSafeRegion> FGFTerritoryMap::CopySafeRegions() const
{
	TArray<FGFSafeRegion> Out;
	for (const FGFTerritoryFaction& Faction : Factions)
	{
		const FGFSafeRegion Region = RegionFor(Faction);
		if (Region.Radius() > 0.f)
		{
			Out.Add(Region);
		}
	}
	return Out;
}

bool FGFTerritoryMap::BeatsOnTie(const FGFSafeRegion& Challenger, const FGFSafeRegion& Holder) const
{
	if (Challenger.Members != Holder.Members)
	{
		return Challenger.Members > Holder.Members;
	}
	return Challenger.Id.ToString().Compare(Holder.Id.ToString(), ESearchCase::IgnoreCase) < 0;
}

bool FGFTerritoryMap::InDisc(const FGFSafeRegion& Region, const FVector& Location)
{
	if (Region.Radius() <= 0.f)
	{
		return false;
	}
	return FGFSafeCircles(TArray<FGFSafeRegion>{ Region }).IsInsideSafe(Location);
}

bool FGFTerritoryMap::CircleCacheMatches() const
{
	if (!bCirclesReady || CachedMaxMembers != MaxMembers || CachedBase != Curve.BaseRadius || CachedPer != Curve.RadiusPerMember || CachedMaxRadius != Curve.MaxRadius || CachedFactions.Num() != Factions.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Factions.Num(); ++Index)
	{
		const FGFTerritoryFaction& Live = Factions[Index];
		const FGFTerritoryFaction& Cached = CachedFactions[Index];
		if (Live.Id != Cached.Id || Live.Members != Cached.Members || Live.Center != Cached.Center || Live.RadiusByMembers != Cached.RadiusByMembers)
		{
			return false;
		}
	}
	return true;
}

const FGFSafeCircles& FGFTerritoryMap::Circles() const
{
	if (!CircleCacheMatches())
	{
		CachedCircles = FGFSafeCircles(CopySafeRegions());
		CachedFactions = Factions;
		CachedMaxMembers = MaxMembers;
		CachedBase = Curve.BaseRadius;
		CachedPer = Curve.RadiusPerMember;
		CachedMaxRadius = Curve.MaxRadius;
		bCirclesReady = true;
	}
	return CachedCircles;
}

FGFTerritoryZoneHit FGFTerritoryMap::GetZoneAt(const FVector& Location) const
{
	const FGFSafeRegion* Best = nullptr;
	float BestDist = 0.f;
	for (const FGFSafeRegion& Region : Circles().Regions)
	{
		if (!InDisc(Region, Location))
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Region.Center, Location);
		if (!Best)
		{
			Best = &Region;
			BestDist = Dist;
			continue;
		}
		const float Delta = Dist - BestDist;
		if (Delta < -OverlapTieSlopCm || (FMath::Abs(Delta) <= OverlapTieSlopCm && BeatsOnTie(Region, *Best)))
		{
			Best = &Region;
			BestDist = Dist;
		}
	}

	FGFTerritoryZoneHit Hit;
	if (Best)
	{
		Hit.Zone = EGFTerritoryZone::Faction;
		Hit.FactionId = Best->Id;
		return Hit;
	}
	if (Neutral.bEnabled)
	{
		FGFSafeRegion Disc;
		Disc.Center = Neutral.Center;
		Disc.Members = 1;
		Disc.BaseRadius = FMath::Max(0.f, Neutral.Radius);
		Disc.RadiusPerExtraMember = 0.f;
		Disc.MaxRadius = Disc.BaseRadius;
		if (InDisc(Disc, Location))
		{
			Hit.Zone = EGFTerritoryZone::Neutral;
			return Hit;
		}
	}
	Hit.Zone = EGFTerritoryZone::Wild;
	return Hit;
}

FGFTerritoryHazardHit FGFTerritoryMap::GetHazardAt(const FVector& Location) const
{
	FGFTerritoryHazardHit Hit;
	if (GetZoneAt(Location).Zone != EGFTerritoryZone::Wild)
	{
		return Hit;
	}
	for (const FGFTerritoryHazard& Hazard : Hazards)
	{
		if (!InHazardBox(Hazard, Location))
		{
			continue;
		}
		Hit.bHazard = true;
		Hit.Id = Hazard.Id;
		Hit.Kind = Hazard.Kind;
		return Hit;
	}
	return Hit;
}

bool FGFTerritoryMap::IsSafeFor(FName FactionId, const FVector& Location) const
{
	if (FactionId.IsNone())
	{
		return false;
	}
	const FGFTerritoryZoneHit Hit = GetZoneAt(Location);
	return Hit.Zone == EGFTerritoryZone::Faction && Hit.FactionId == FactionId;
}

bool FGFTerritoryMap::IsSafeFor(const FGFSocialPlayer& Who, const FVector& Location) const
{
	return IsSafeFor(Who.Faction, Location);
}

bool FGFTerritoryMap::IsSafeFor(const FGFSafeSubject& Who, const FVector& Location) const
{
	return FGFProtectionRules::IsProtectedOn(Who.Key, Who.GroupId, Who.bHost, OwnerAt(Location), GetAccess());
}

FName FGFTerritoryMap::OwnerAt(const FVector& Location) const
{
	const FGFTerritoryZoneHit Hit = GetZoneAt(Location);
	return Hit.Zone == EGFTerritoryZone::Faction ? Hit.FactionId : NAME_None;
}

const FGFFactionAccess& FGFTerritoryMap::GetAccess() const
{
	if (AccessSource)
	{
		if (const FGFFactionAccess* Live = AccessSource())
		{
			return *Live;
		}
	}
	return Access;
}

bool FGFTerritoryMap::IsInsideSafe(const FVector& Location) const
{
	return Circles().IsInsideSafe(Location);
}

FVector FGFTerritoryMap::ClosestSafePoint(const FVector& Location) const
{
	const FGFSafeCircles& Cached = Circles();
	if (!Cached.HasAnySafe())
	{
		return Location;
	}
	return Cached.ClosestSafePoint(Location);
}

bool FGFTerritoryMap::ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const
{
	const FGFSafeCircles& Cached = Circles();
	if (!Cached.HasAnySafe())
	{
		return false;
	}
	return Cached.ClosestEdge(Location, OutPoint, OutOutward);
}

bool FGFTerritoryMap::HasAnySafe() const
{
	return Circles().HasAnySafe();
}
