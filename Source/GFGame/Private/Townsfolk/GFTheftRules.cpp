#include "GFTheftRules.h"
#include "GFTownsfolkRules.h"

bool FGFTheftRules::Sees(EGFTownsfolkRole Role, float DistanceCm, bool bLineOfSight, float RangeCm)
{
	return Role == EGFTownsfolkRole::Villager && bLineOfSight && DistanceCm >= 0.f && DistanceCm <= FMath::Max(0.f, RangeCm);
}

bool FGFTheftRules::ShouldHire(int32 Witnesses, bool bAlreadyHired)
{
	return Witnesses > 0 && !bAlreadyHired;
}

FGFHirePlan FGFTheftRules::PlanHire(const TArray<FGFHireCandidate>& Candidates, int32 SpeciesCount)
{
	TArray<int32> Order;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].bWild && Candidates[Index].bInRange)
		{
			Order.Add(Index);
		}
	}
	Order.Sort([&Candidates](int32 A, int32 B)
	{
		const float DA = Candidates[A].Distance;
		const float DB = Candidates[B].Distance;
		if (DA < DB)
		{
			return true;
		}
		if (DB < DA)
		{
			return false;
		}
		return A < B;
	});

	FGFHirePlan Plan;
	const int32 Take = FMath::Min(MaxHired, Order.Num());
	for (int32 Index = 0; Index < Take; ++Index)
	{
		Plan.Existing.Add(Order[Index]);
	}
	if (SpeciesCount > 0 && Plan.Existing.Num() < SpawnBelow)
	{
		Plan.SpawnCount = SpawnBelow - Plan.Existing.Num();
	}
	return Plan;
}

FName FGFTheftRules::ResolveTown(const TArray<FGFTerritoryFaction>& Factions, const FGFTerritoryCurve& Curve, FName ZoneFaction, const FVector& Location)
{
	if (!ZoneFaction.IsNone())
	{
		return ZoneFaction;
	}
	FName Best = NAME_None;
	float BestDist = 0.f;
	for (const FGFTerritoryFaction& Faction : Factions)
	{
		if (Faction.Id.IsNone())
		{
			continue;
		}
		const float Radius = FGFTownsfolkRules::CampRadius(Faction, Curve, 1);
		const float Dist = FVector::Dist2D(Faction.Center, Location);
		if (Dist <= Radius && (Best.IsNone() || Dist < BestDist))
		{
			Best = Faction.Id;
			BestDist = Dist;
		}
	}
	return Best;
}

bool FGFTheftRules::NoteWitness(FGFTheftHeat& Heat, FName Town)
{
	if (Town.IsNone() || Heat.State == EGFTheftHeat::Hunted)
	{
		return false;
	}
	if (Heat.State == EGFTheftHeat::Spotted)
	{
		return Heat.TownId == Town;
	}
	Heat.State = EGFTheftHeat::Spotted;
	Heat.TownId = Town;
	Heat.HuntLeft = 0.f;
	Heat.Away = 0.f;
	Heat.bReportedClear = false;
	return true;
}

bool FGFTheftRules::NoteHire(FGFTheftHeat& Heat)
{
	if (Heat.State != EGFTheftHeat::Spotted)
	{
		return false;
	}
	Heat.State = EGFTheftHeat::Hunted;
	Heat.HuntLeft = HuntSeconds;
	Heat.Away = 0.f;
	Heat.bReportedClear = false;
	return true;
}

bool FGFTheftRules::AdvanceHeat(FGFTheftHeat& Heat, float Dt, bool bInTown, int32 AliveHired)
{
	if (Heat.State != EGFTheftHeat::Hunted || Heat.bReportedClear)
	{
		return false;
	}
	const float Step = FMath::Max(0.f, Dt);
	Heat.HuntLeft -= Step;
	if (bInTown)
	{
		Heat.Away = 0.f;
	}
	else
	{
		Heat.Away += Step;
	}
	if (Heat.HuntLeft > 0.f && Heat.Away < AwaySeconds && AliveHired > 0)
	{
		return false;
	}
	Heat.State = EGFTheftHeat::Clear;
	Heat.HuntLeft = 0.f;
	Heat.Away = 0.f;
	Heat.bReportedClear = true;
	return true;
}

bool FGFTheftRules::IsChasing(bool bAlive, EGFTameState State, const FString& TargetKey, const FString& ThiefKey)
{
	return bAlive && State == EGFTameState::Hostile && !ThiefKey.IsEmpty() && TargetKey == ThiefKey;
}

bool FGFTheftRules::ShouldRehire(EGFTameState State)
{
	return State != EGFTameState::Tamed && State != EGFTameState::Fled;
}

EGFHireRelease FGFTheftRules::ReleaseFor(bool bSpawnedByHire, EGFTameState State)
{
	if (State == EGFTameState::Tamed)
	{
		return EGFHireRelease::Keep;
	}
	return bSpawnedByHire ? EGFHireRelease::Despawn : EGFHireRelease::Release;
}

FGFTheftHeat& FGFTheftHeatBook::FindOrAdd(const FString& Player, FName Town)
{
	if (FGFTheftHeat* Found = Find(Player, Town))
	{
		return *Found;
	}
	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Player = Player;
	Entry.Heat.TownId = Town;
	return Entry.Heat;
}

FGFTheftHeat* FGFTheftHeatBook::Find(const FString& Player, FName Town)
{
	for (FEntry& Entry : Entries)
	{
		if (Entry.Player == Player && Entry.Heat.TownId == Town)
		{
			return &Entry.Heat;
		}
	}
	return nullptr;
}

const FGFTheftHeat* FGFTheftHeatBook::Find(const FString& Player, FName Town) const
{
	return const_cast<FGFTheftHeatBook*>(this)->Find(Player, Town);
}

bool FGFTheftHeatBook::IsHot(const FString& Player) const
{
	const FGFTheftHeat* Top = Hottest(Player);
	return Top && Top->State != EGFTheftHeat::Clear;
}

const FGFTheftHeat* FGFTheftHeatBook::Hottest(const FString& Player) const
{
	const FGFTheftHeat* Best = nullptr;
	for (const FEntry& Entry : Entries)
	{
		if (Entry.Player == Player && (!Best || static_cast<uint8>(Entry.Heat.State) > static_cast<uint8>(Best->State)))
		{
			Best = &Entry.Heat;
		}
	}
	return Best;
}

TArray<FString> FGFTheftHeatBook::Players() const
{
	TArray<FString> Out;
	for (const FEntry& Entry : Entries)
	{
		Out.AddUnique(Entry.Player);
	}
	return Out;
}
