#include "GFRoamingPolicy.h"
#include "Engine/World.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"

namespace
{
	bool HomeNamesZone(const FGFSafeCircles& Home, FName ZoneId)
	{
		if (ZoneId.IsNone())
		{
			return false;
		}
		for (const FGFSafeRegion& Region : Home.Regions)
		{
			if (!Region.Id.IsNone() && Region.Id == ZoneId)
			{
				return true;
			}
		}
		return false;
	}
}

void FGFRoamingPolicy::SetHome(const FGFSafeCircles& InHome, FName InHomeMap)
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
	return HomeNamesZone(Home, Zone.Id);
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
	return Home.IsInsideSafe(Goal);
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
	return Home.ClosestSafePoint(Goal);
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

bool FGFRevengeGangBoard::TryAdd(FName DinoId, FName Species, int32 MaxPlayers, FName& OutCode)
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
	if (MaxPlayers > 0 && Members.Num() >= MaxPlayers)
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
	Handoff.TargetKey = TargetKey;
	Handoff.Members = Members;
	Handoff.bPending = HasTarget();
	return Handoff;
}

bool FGFRevengeGangBoard::ConsumeHandoff(FGFRoamingHandoff& Record)
{
	if (!Record.bPending || Record.TargetKey.IsEmpty())
	{
		return false;
	}
	TargetKey = Record.TargetKey;
	Members = Record.Members;
	CurrentMap = Record.ToMap;
	Record.bPending = false;
	return true;
}

int32 GFRoamingLimits::MaxPlayers(const AGFGameState* GS)
{
	if (GS)
	{
		const UWorld* World = GS->GetWorld();
		const AGameModeBase* GM = World ? World->GetAuthGameMode() : nullptr;
		if (GM && GM->GameSession)
		{
			return GM->GameSession->MaxPlayers;
		}
	}
	const AGameSession* Defaults = GetDefault<AGameSession>();
	return Defaults ? Defaults->MaxPlayers : 0;
}

AGFPlayerState* GFRoamingBind::FindTarget(const AGFGameState& GS, const FString& TargetKey)
{
	if (TargetKey.IsEmpty())
	{
		return nullptr;
	}
	AGFPlayerState* PS = GS.FindPlayerByKey(TargetKey);
	if (!PS || PS->bLoggedOut)
	{
		return nullptr;
	}
	return PS;
}
