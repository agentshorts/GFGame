#include "GFRoamingPolicy.h"
#include "GFGameState.h"
#include "GFPlayerState.h"

bool FGFDinoHomeZone::Contains(const FVector& Point) const
{
	const float Radius = FMath::Max(0.f, RadiusCm);
	return FVector::DistSquared2D(Point, Center) <= FMath::Square(Radius);
}

FVector FGFDinoHomeZone::Clamp(const FVector& Point) const
{
	const float Radius = FMath::Max(0.f, RadiusCm);
	const FVector2D Delta(Point.X - Center.X, Point.Y - Center.Y);
	const float Dist = Delta.Size();
	if (Dist <= Radius || Dist < KINDA_SMALL_NUMBER)
	{
		return Point;
	}
	const FVector2D Edge = FVector2D(Center.X, Center.Y) + Delta * (Radius / Dist);
	return FVector(Edge.X, Edge.Y, Point.Z);
}

void FGFRoamingPolicy::SetHome(const FGFDinoHomeZone& InHome, FName InHomeMap)
{
	Home = InHome;
	HomeMap = InHomeMap;
}

bool FGFRoamingPolicy::CanEnter(const FGFRoamZone& Zone) const
{
	if (bRevengeGang)
	{
		return true;
	}
	return !Home.ZoneId.IsNone() && Zone.Id == Home.ZoneId;
}

bool FGFRoamingPolicy::CanEnter(const FGFRoamMap& Map) const
{
	if (bRevengeGang)
	{
		return true;
	}
	return !HomeMap.IsNone() && Map.Id == HomeMap;
}

bool FGFRoamingPolicy::CanPathTo(const FVector& Goal) const
{
	if (bRevengeGang)
	{
		return true;
	}
	return Home.Contains(Goal);
}

bool FGFRoamingPolicy::CanFollow(const FGFRoamZone& Zone, const FGFRoamMap& Map) const
{
	return CanEnter(Zone) && CanEnter(Map);
}

FVector FGFRoamingPolicy::ClampFollowGoal(const FVector& Goal) const
{
	if (bRevengeGang)
	{
		return Goal;
	}
	return Home.Clamp(Goal);
}

FGFFollowDecision FGFRoamingPolicy::DecideFollow(const FVector& RawGoal, const FGFRoamZone& Zone, const FGFRoamMap& Map) const
{
	FGFFollowDecision Decision;
	if (!CanFollow(Zone, Map))
	{
		return Decision;
	}
	if (CanPathTo(RawGoal))
	{
		Decision.bMove = true;
		Decision.Goal = RawGoal;
		return Decision;
	}
	const FVector Clamped = ClampFollowGoal(RawGoal);
	if (CanPathTo(Clamped))
	{
		Decision.bMove = true;
		Decision.Goal = Clamped;
	}
	return Decision;
}

bool FGFRevengeGangBoard::Contains(FName DinoId) const
{
	return Members.ContainsByPredicate([DinoId](const FGFRoamingMember& Member) { return Member.Id == DinoId; });
}

bool FGFRevengeGangBoard::TryAdd(FName DinoId, FName Species, int32 SeatsPerFaction, FName& OutCode)
{
	if (DinoId.IsNone())
	{
		OutCode = GFRoamingCodes::NoDino();
		return false;
	}
	if (Contains(DinoId))
	{
		OutCode = GFRoamingCodes::Already();
		return true;
	}
	if (Members.Num() >= GFRoamingLimits::MaxSessionPlayers(SeatsPerFaction))
	{
		OutCode = GFRoamingCodes::Full();
		return false;
	}
	FGFRoamingMember Member;
	Member.Id = DinoId;
	Member.Species = Species;
	Members.Add(Member);
	OutCode = GFRoamingCodes::Ok();
	return true;
}

FGFRoamingHandoff FGFRevengeGangBoard::ExportHandoff(FName ToMap) const
{
	FGFRoamingHandoff Handoff;
	Handoff.FromMap = CurrentMap;
	Handoff.ToMap = ToMap;
	Handoff.Target = Target;
	Handoff.Members = Members;
	Handoff.bPending = Target.IsSet();
	return Handoff;
}

bool FGFRevengeGangBoard::ConsumeHandoff(FGFRoamingHandoff& Record)
{
	if (!Record.bPending || !Record.Target.IsSet())
	{
		return false;
	}
	Target = Record.Target;
	Members = Record.Members;
	CurrentMap = Record.ToMap;
	Record.bPending = false;
	return true;
}

FGFGangTarget FGFGangTarget::FromPlayer(const AGFPlayerState& PS)
{
	FGFGangTarget Target;
	Target.PlayerKey = PS.PlayerKey;
	Target.FactionId = PS.FactionId;
	Target.bIsHost = PS.bIsHost;
	return Target;
}

FName GFRoamingFactions::Cinderwrights()
{
	return FName(TEXT("Cinderwrights"));
}

FName GFRoamingFactions::Verdant()
{
	return FName(TEXT("Verdant"));
}

FName GFRoamingFactions::Rime()
{
	return FName(TEXT("Rime"));
}

const TArray<FName>& GFRoamingFactions::All()
{
	static const TArray<FName> Names = { Cinderwrights(), Verdant(), Rime() };
	return Names;
}

int32 GFRoamingLimits::SeatsPerFaction(const AGFGameState* GS)
{
	const AGFGameState* Source = GS ? GS : GetDefault<AGFGameState>();
	return Source ? Source->MaxPerFaction : 0;
}

int32 GFRoamingLimits::MaxSessionPlayers(int32 InSeatsPerFaction)
{
	return GFRoamingFactions::Count() * FMath::Max(0, InSeatsPerFaction);
}

AGFPlayerState* GFRoamingBind::FindTarget(const AGFGameState& GS, const FGFGangTarget& Target)
{
	if (!Target.IsSet())
	{
		return nullptr;
	}
	AGFPlayerState* PS = GS.FindPlayerByKey(Target.PlayerKey);
	if (!PS || PS->bLoggedOut || PS->bIsHost != Target.bIsHost)
	{
		return nullptr;
	}
	if (!Target.FactionId.IsNone() && PS->FactionId != Target.FactionId)
	{
		return nullptr;
	}
	return PS;
}
