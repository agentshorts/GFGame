#include "GFJailRules.h"

const FName FGFJailCodes::Ok(TEXT("Ok"));
const FName FGFJailCodes::Self(TEXT("Self"));
const FName FGFJailCodes::NotAllowed(TEXT("NotAllowed"));
const FName FGFJailCodes::TargetIsHost(TEXT("TargetIsHost"));
const FName FGFJailCodes::AlreadyJailed(TEXT("AlreadyJailed"));
const FName FGFJailCodes::NotJailed(TEXT("NotJailed"));
const FName FGFJailCodes::NoSuchPlayer(TEXT("NoSuchPlayer"));
const FName FGFJailCodes::NotAuthority(TEXT("NotAuthority"));

bool FGFJailContext::IsJailed(const FString& PlayerId) const
{
	return !PlayerId.IsEmpty() && JailedIds.Contains(PlayerId);
}

namespace
{
	bool Earlier(const FGFJailMember& A, const FGFJailMember& B)
	{
		if (A.JoinOrder != B.JoinOrder)
		{
			return A.JoinOrder < B.JoinOrder;
		}
		return A.PlayerId.Compare(B.PlayerId) < 0;
	}
}

FString FGFJailRules::ChooseLeader(const TArray<FGFJailMember>& Remaining)
{
	const FGFJailMember* BestFree = nullptr;
	const FGFJailMember* BestAny = nullptr;
	for (const FGFJailMember& Member : Remaining)
	{
		if (Member.PlayerId.IsEmpty())
		{
			continue;
		}
		if (!BestAny || Earlier(Member, *BestAny))
		{
			BestAny = &Member;
		}
		if (!Member.bJailed && (!BestFree || Earlier(Member, *BestFree)))
		{
			BestFree = &Member;
		}
	}
	if (BestFree)
	{
		return BestFree->PlayerId;
	}
	return BestAny ? BestAny->PlayerId : FString();
}

FString FGFJailRules::LeaderOf(FName FactionId, const FGFJailContext& Context)
{
	if (FactionId.IsNone())
	{
		return FString();
	}
	TArray<FGFJailMember> Remaining;
	for (const FGFJailActor& Player : Context.Players)
	{
		if (!Player.bOnline || Player.FactionId != FactionId || Player.PlayerId.IsEmpty())
		{
			continue;
		}
		FGFJailMember Member;
		Member.PlayerId = Player.PlayerId;
		Member.JoinOrder = Player.JoinOrder;
		Member.bJailed = Context.IsJailed(Player.PlayerId);
		Remaining.Add(Member);
	}
	return ChooseLeader(Remaining);
}

namespace
{
	FName JailPower(const FGFJailActor& Actor, const FGFJailActor& Target, const FGFJailContext& Context)
	{
		if (Actor.PlayerId.IsEmpty() || Target.PlayerId.IsEmpty())
		{
			return FGFJailCodes::NoSuchPlayer;
		}
		if (Actor.PlayerId == Target.PlayerId)
		{
			return FGFJailCodes::Self;
		}
		// The host is above faction leadership, including when the host is also a member of the target's faction.
		if (Actor.bHost)
		{
			return FGFJailCodes::Ok;
		}
		if (Target.bHost)
		{
			return FGFJailCodes::TargetIsHost;
		}
		if (!Actor.FactionId.IsNone() && Actor.FactionId == Target.FactionId
			&& FGFJailRules::LeaderOf(Actor.FactionId, Context) == Actor.PlayerId)
		{
			return FGFJailCodes::Ok;
		}
		return FGFJailCodes::NotAllowed;
	}
}

FName FGFJailRules::CanJail(const FGFJailActor& Actor, const FGFJailActor& Target, const FGFJailContext& Context)
{
	if (Actor.PlayerId != Target.PlayerId && Context.IsJailed(Target.PlayerId))
	{
		return FGFJailCodes::AlreadyJailed;
	}
	return JailPower(Actor, Target, Context);
}

FName FGFJailRules::CanRelease(const FGFJailActor& Actor, const FGFJailActor& Target, const FGFJailContext& Context)
{
	if (Actor.PlayerId != Target.PlayerId && !Context.IsJailed(Target.PlayerId))
	{
		return FGFJailCodes::NotJailed;
	}
	return JailPower(Actor, Target, Context);
}

bool FGFJailRules::IsMuted(const FString& PlayerId, const FGFJailContext& Context)
{
	return Context.IsJailed(PlayerId);
}

bool FGFJailRules::CanEmote(const FString& PlayerId, const FGFJailContext& Context)
{
	return !IsMuted(PlayerId, Context);
}
