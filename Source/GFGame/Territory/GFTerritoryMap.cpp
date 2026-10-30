#include "GFTerritoryMap.h"

void FGFTerritoryMap::SetDefaultFactions()
{
	Factions.Reset();
	const FName Ids[] = {
		FGFTerritoryFactions::Cinderwrights,
		FGFTerritoryFactions::Verdant,
		FGFTerritoryFactions::Rime
	};
	// Far enough from the hub that a one-member disc (default curve) does not cover the origin, so an
	// enabled neutral zone can sit on the hub until factions grow over it.
	const float Ring = 2800.f;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ids); ++Index)
	{
		const float Angle = FMath::DegreesToRadians(120.f * static_cast<float>(Index));
		FGFTerritoryFaction Faction;
		Faction.Id = Ids[Index];
		Faction.Center = FVector(FMath::Cos(Angle) * Ring, FMath::Sin(Angle) * Ring, 0.f);
		Faction.Members = 0;
		Factions.Add(Faction);
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
	if (Faction.Id.IsNone() || Count <= 0)
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

bool FGFTerritoryMap::BeatsOnTie(const FGFTerritoryFaction& Challenger, const FGFTerritoryFaction& Holder) const
{
	const int32 ChallengerMembers = EffectiveMembers(Challenger.Members);
	const int32 HolderMembers = EffectiveMembers(Holder.Members);
	if (ChallengerMembers != HolderMembers)
	{
		return ChallengerMembers > HolderMembers;
	}
	return Challenger.Id.ToString().Compare(Holder.Id.ToString(), ESearchCase::IgnoreCase) < 0;
}

FGFTerritoryZoneHit FGFTerritoryMap::GetZoneAt(const FVector& Location) const
{
	const FGFTerritoryFaction* Best = nullptr;
	float BestDist = 0.f;
	for (const FGFTerritoryFaction& Faction : Factions)
	{
		const FGFSafeRegion Region = RegionFor(Faction);
		const float Radius = Region.Radius();
		if (Radius <= 0.f)
		{
			continue;
		}
		if (FVector::DistSquared2D(Region.Center, Location) > FMath::Square(Radius))
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Region.Center, Location);
		if (!Best)
		{
			Best = &Faction;
			BestDist = Dist;
			continue;
		}
		const float Delta = Dist - BestDist;
		if (Delta < -OverlapTieSlopCm || (FMath::Abs(Delta) <= OverlapTieSlopCm && BeatsOnTie(Faction, *Best)))
		{
			Best = &Faction;
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
	if (Neutral.Contains(Location))
	{
		Hit.Zone = EGFTerritoryZone::Neutral;
		return Hit;
	}
	Hit.Zone = EGFTerritoryZone::Wild;
	return Hit;
}

bool FGFTerritoryMap::IsSafeFor(const FGFTerritorySubject& Who, const FVector& Location) const
{
	// bHost is AGFPlayerState::bIsHost. The host does not get land the faction doesn't own.
	if (Who.bWildCreature || Who.FactionId.IsNone())
	{
		return false;
	}
	const FGFTerritoryZoneHit Hit = GetZoneAt(Location);
	return Hit.Zone == EGFTerritoryZone::Faction && Hit.FactionId == Who.FactionId;
}

FGFSafeCircles FGFTerritoryMap::BuildCircles() const
{
	return FGFSafeCircles(CopySafeRegions());
}

bool FGFTerritoryMap::IsInsideSafe(const FVector& Location) const
{
	return BuildCircles().IsInsideSafe(Location);
}

FVector FGFTerritoryMap::ClosestSafePoint(const FVector& Location) const
{
	const FGFSafeCircles Circles = BuildCircles();
	if (!Circles.HasAnySafe())
	{
		return Location;
	}
	return Circles.ClosestSafePoint(Location);
}

bool FGFTerritoryMap::ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const
{
	const FGFSafeCircles Circles = BuildCircles();
	if (!Circles.HasAnySafe())
	{
		return false;
	}
	return Circles.ClosestEdge(Location, OutPoint, OutOutward);
}

bool FGFTerritoryMap::HasAnySafe() const
{
	return CopySafeRegions().Num() > 0;
}
