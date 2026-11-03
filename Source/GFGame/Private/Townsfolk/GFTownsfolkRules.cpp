#include "GFTownsfolkRules.h"
#include "GFTerritoryMap.h"
#include "GFWanderComponent.h"

namespace GFTownsfolkPrivate
{
	const FString TownPrefix(TEXT("Town:"));

	FVector FallbackOffset(const TArray<FVector>& Offsets, int32 Index, float Ring)
	{
		if (Offsets.IsValidIndex(Index))
		{
			return Offsets[Index];
		}
		const float Angle = static_cast<float>(Index) * (2.f * PI / 4.f);
		const float R = FMath::Max(0.f, Ring);
		return FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R, 88.f);
	}

	void SortSpots(TArray<FGFTownsfolkSpot>& Spots)
	{
		Spots.Sort([](const FGFTownsfolkSpot& A, const FGFTownsfolkSpot& B)
		{
			if (A.Location.X != B.Location.X)
			{
				return A.Location.X < B.Location.X;
			}
			if (A.Location.Y != B.Location.Y)
			{
				return A.Location.Y < B.Location.Y;
			}
			return A.Location.Z < B.Location.Z;
		});
	}
}

FName FGFTownsfolkRules::VillagerSpotTag()
{
	return TEXT("GFVillagerSpot");
}

FName FGFTownsfolkRules::ElderSpotTag()
{
	return TEXT("GFElderSpot");
}

bool FGFTownsfolkRules::SpotFromTags(const TArray<FName>& Tags, const FVector& Location, FGFTownsfolkSpot& Out)
{
	bool bVillager = false;
	bool bElder = false;
	FName Town = NAME_None;
	for (const FName& Tag : Tags)
	{
		if (Tag == VillagerSpotTag())
		{
			bVillager = true;
		}
		else if (Tag == ElderSpotTag())
		{
			bElder = true;
		}
		else
		{
			const FString Text = Tag.ToString();
			if (Town.IsNone() && Text.StartsWith(GFTownsfolkPrivate::TownPrefix))
			{
				const FString Id = Text.Mid(GFTownsfolkPrivate::TownPrefix.Len());
				if (!Id.IsEmpty())
				{
					Town = FName(*Id);
				}
			}
		}
	}
	if (Town.IsNone() || bVillager == bElder)
	{
		return false;
	}
	Out.TownId = Town;
	Out.Role = bElder ? EGFTownsfolkRole::Elder : EGFTownsfolkRole::Villager;
	Out.Location = Location;
	return true;
}

void FGFTownsfolkRules::Resolve(
	const TArray<FGFTerritoryFaction>& Factions,
	const TArray<FGFTownsfolkSpot>& Tagged,
	const TArray<FVector>& VillagerOffsets,
	const FVector& ElderOffset,
	int32 InVillagersPerTown,
	int32 InEldersPerTown,
	TArray<FGFTownsfolkSpot>& Out)
{
	Out.Reset();
	const int32 VillagerCount = FMath::Max(0, InVillagersPerTown);
	const int32 ElderCount = FMath::Max(0, InEldersPerTown);
	for (const FGFTerritoryFaction& Faction : Factions)
	{
		if (Faction.Id.IsNone())
		{
			continue;
		}
		TArray<FGFTownsfolkSpot> Villagers;
		TArray<FGFTownsfolkSpot> Elders;
		for (const FGFTownsfolkSpot& Spot : Tagged)
		{
			if (Spot.TownId != Faction.Id)
			{
				continue;
			}
			if (Spot.Role == EGFTownsfolkRole::Elder)
			{
				Elders.Add(Spot);
			}
			else
			{
				Villagers.Add(Spot);
			}
		}
		if (Villagers.Num() == 0)
		{
			for (int32 Index = 0; Index < VillagerCount; ++Index)
			{
				FGFTownsfolkSpot Spot;
				Spot.TownId = Faction.Id;
				Spot.Role = EGFTownsfolkRole::Villager;
				Spot.Location = Faction.Center + GFTownsfolkPrivate::FallbackOffset(VillagerOffsets, Index, 250.f);
				Out.Add(Spot);
			}
		}
		else
		{
			GFTownsfolkPrivate::SortSpots(Villagers);
			Out.Append(Villagers);
		}
		if (Elders.Num() == 0)
		{
			for (int32 Index = 0; Index < ElderCount; ++Index)
			{
				FGFTownsfolkSpot Spot;
				Spot.TownId = Faction.Id;
				Spot.Role = EGFTownsfolkRole::Elder;
				FVector Offset = ElderOffset;
				Offset.X += 120.f * static_cast<float>(Index);
				Spot.Location = Faction.Center + Offset;
				Out.Add(Spot);
			}
		}
		else
		{
			GFTownsfolkPrivate::SortSpots(Elders);
			Out.Append(Elders);
		}
	}
}

bool FGFTownsfolkRules::InReach(const FVector& From, const FVector& To, float Reach)
{
	return Reach >= 0.f && FVector::Dist(From, To) <= Reach;
}

int32 FGFTownsfolkRules::NearestInReach(const FVector& From, const TArray<FVector>& Points, float Reach)
{
	int32 Best = INDEX_NONE;
	float BestDist = 0.f;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const float Dist = FVector::Dist(From, Points[Index]);
		if (!InReach(From, Points[Index], Reach))
		{
			continue;
		}
		if (Best == INDEX_NONE || Dist < BestDist)
		{
			Best = Index;
			BestDist = Dist;
		}
	}
	return Best;
}

FVector FGFTownsfolkRules::ClampToDisc(const FVector& Center, const FVector& Point, float Radius)
{
	const float Limit = FMath::Max(0.f, Radius);
	const float DX = Point.X - Center.X;
	const float DY = Point.Y - Center.Y;
	const float Dist = FMath::Sqrt(DX * DX + DY * DY);
	if (Limit <= KINDA_SMALL_NUMBER || Dist <= KINDA_SMALL_NUMBER)
	{
		return FVector(Center.X, Center.Y, Point.Z);
	}
	if (Dist <= Limit)
	{
		return Point;
	}
	const float Scale = Limit / Dist;
	return FVector(Center.X + DX * Scale, Center.Y + DY * Scale, Point.Z);
}

float FGFTownsfolkRules::CampRadius(const FGFTerritoryFaction& Faction, const FGFTerritoryCurve& Curve, int32 Members)
{
	FGFTerritoryFaction Copy = Faction;
	Copy.Members = FMath::Max(1, Members);
	FGFTerritoryMap Map;
	Map.Curve = Curve;
	return Map.RadiusOf(Copy);
}

FString FGFTownsfolkRules::DisplayName(FName TownId, EGFTownsfolkRole Role)
{
	const TCHAR* Word = (Role == EGFTownsfolkRole::Elder) ? TEXT("Elder") : TEXT("Villager");
	if (TownId.IsNone())
	{
		return Word;
	}
	return FString::Printf(TEXT("%s %s"), *TownId.ToString(), Word);
}

bool FGFTownsfolkRules::IsFactionsWorld(const FString& MapOrPackage)
{
	return MapOrPackage.Contains(TEXT("L_World_Factions_A"), ESearchCase::IgnoreCase);
}

bool FGFTownsfolkMind::CanTransition(EGFTownsfolkBehavior From, EGFTownsfolkBehavior To)
{
	if (From == To)
	{
		return true;
	}
	switch (From)
	{
	case EGFTownsfolkBehavior::Idle:
		return To == EGFTownsfolkBehavior::Wander || To == EGFTownsfolkBehavior::Talk;
	case EGFTownsfolkBehavior::Wander:
		return To == EGFTownsfolkBehavior::Idle || To == EGFTownsfolkBehavior::Talk;
	case EGFTownsfolkBehavior::Talk:
		return To == EGFTownsfolkBehavior::Idle;
	default:
		// Warn and Flee are not states yet. Until they exist, an unknown value does not move.
		return false;
	}
}

EGFTownsfolkBehavior FGFTownsfolkMind::Transition(EGFTownsfolkBehavior From, EGFTownsfolkBehavior To)
{
	return CanTransition(From, To) ? To : From;
}

bool FGFTownsfolkMind::BeginTalk(float Seconds)
{
	const EGFTownsfolkBehavior Next = Transition(State, EGFTownsfolkBehavior::Talk);
	if (Next != EGFTownsfolkBehavior::Talk)
	{
		return false;
	}
	State = Next;
	TimeLeft = FMath::Max(0.f, Seconds);
	return true;
}

float FGFTownsfolkMind::PickIdleWait(FRandomStream& Rng) const
{
	float Min = IdleMin;
	float Max = IdleMax;
	if (Min > Max)
	{
		Swap(Min, Max);
	}
	Min = FMath::Max(0.f, Min);
	Max = FMath::Max(0.f, Max);
	return (Max > Min) ? Rng.FRandRange(Min, Max) : Min;
}

void FGFTownsfolkMind::Advance(float DeltaSeconds, FRandomStream& Rng, FVector& Location)
{
	const float Dt = FMath::Max(0.f, DeltaSeconds);
	Location = FGFTownsfolkRules::ClampToDisc(TownCenter, Location, TownRadius);
	if (Dt <= 0.f)
	{
		return;
	}

	if (State == EGFTownsfolkBehavior::Talk)
	{
		TimeLeft -= Dt;
		if (TimeLeft <= 0.f)
		{
			State = Transition(State, EGFTownsfolkBehavior::Idle);
			TimeLeft = PickIdleWait(Rng);
		}
		return;
	}

	if (State == EGFTownsfolkBehavior::Idle)
	{
		TimeLeft -= Dt;
		if (TimeLeft > 0.f)
		{
			return;
		}
		const EGFTownsfolkBehavior Next = Transition(State, EGFTownsfolkBehavior::Wander);
		if (Next != EGFTownsfolkBehavior::Wander)
		{
			TimeLeft = PickIdleWait(Rng);
			return;
		}
		State = Next;
		const FVector Picked = UGFWanderComponent::PickWanderPoint(Home, FMath::Max(0.f, WanderRadius), Rng);
		WanderTarget = FGFTownsfolkRules::ClampToDisc(TownCenter, Picked, TownRadius);
		WanderTarget.Z = Location.Z;
		return;
	}

	if (State == EGFTownsfolkBehavior::Wander)
	{
		const float Stop = FMath::Max(0.f, StopDistance);
		const float Dist = FVector::Dist2D(Location, WanderTarget);
		const FVector Dir = (WanderTarget - Location).GetSafeNormal2D();
		if (Dist <= Stop || WalkSpeed <= 0.f || Dir.IsNearlyZero())
		{
			State = Transition(State, EGFTownsfolkBehavior::Idle);
			TimeLeft = PickIdleWait(Rng);
			return;
		}
		const FVector Stepped = Location + Dir * WalkSpeed * Dt;
		const FVector Clamped = FGFTownsfolkRules::ClampToDisc(TownCenter, Stepped, TownRadius);
		const float After = FVector::Dist2D(Clamped, WanderTarget);
		Location = Clamped;
		if (After <= Stop || After >= Dist - KINDA_SMALL_NUMBER)
		{
			State = Transition(State, EGFTownsfolkBehavior::Idle);
			TimeLeft = PickIdleWait(Rng);
		}
		return;
	}

	State = EGFTownsfolkBehavior::Idle;
	TimeLeft = PickIdleWait(Rng);
}
