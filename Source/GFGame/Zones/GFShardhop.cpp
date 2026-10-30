#include "GFShardhop.h"

#include "GFZoneCatalog.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

namespace
{
	bool RecentPvP(const FGFShardhopView& View)
	{
		return View.LastPvPDamageSeconds >= 0.0
			&& (View.NowSeconds - View.LastPvPDamageSeconds) < static_cast<double>(FGFZoneConstants::PvPBlockSeconds);
	}
}

EGFShardhopResult FGFShardhop::TryStart(const FGFZoneCatalog& Catalog, const FGFShardhopView& View, FName DestId, FGFShardhopChannel& Channel)
{
	if (Channel.bActive)
	{
		return EGFShardhopResult::Busy;
	}
	const FGFShardstoneRow* Dest = Catalog.FindStone(DestId);
	if (!Dest)
	{
		return EGFShardhopResult::NoStone;
	}
	if (!Catalog.IsUnlocked(*Dest, View.Unlocked))
	{
		return EGFShardhopResult::Locked;
	}
	if (View.bVillainMode)
	{
		return EGFShardhopResult::Villain;
	}
	if (RecentPvP(View))
	{
		return EGFShardhopResult::RecentPvP;
	}
	if (!Catalog.IsNearAnyUnlocked(View.Location, View.Unlocked))
	{
		return EGFShardhopResult::TooFar;
	}
	Channel.bActive = true;
	Channel.DestId = DestId;
	Channel.StartedAt = View.NowSeconds;
	Channel.DamageSerialAtStart = View.DamageSerial;
	return EGFShardhopResult::Started;
}

EGFShardhopResult FGFShardhop::Tick(const FGFZoneCatalog& Catalog, const FGFShardhopView& View, FGFShardhopChannel& Channel)
{
	if (!Channel.bActive)
	{
		return EGFShardhopResult::Cancelled;
	}
	const bool bHurt = View.DamageSerial != Channel.DamageSerialAtStart;
	const bool bLeft = !Catalog.IsNearAnyUnlocked(View.Location, View.Unlocked);
	if (View.bVillainMode || bHurt || bLeft)
	{
		Channel.bActive = false;
		return EGFShardhopResult::Cancelled;
	}
	if ((View.NowSeconds - Channel.StartedAt) >= static_cast<double>(FGFZoneConstants::ChannelSeconds))
	{
		Channel.bActive = false;
		return EGFShardhopResult::Completed;
	}
	return EGFShardhopResult::Running;
}

FVector FGFShardhop::ArrivalPoint(const FGFShardstoneRow& Stone, int32 Slot)
{
	const float Angle = FMath::DegreesToRadians(static_cast<float>(Slot) * 90.f);
	const FVector Ring(
		FMath::Cos(Angle) * FGFZoneConstants::ArrivalSpreadCm,
		FMath::Sin(Angle) * FGFZoneConstants::ArrivalSpreadCm,
		0.f);
	return Stone.Location + Stone.ArrivalOffset + Ring;
}

int32 FGFShardhop::SlotOf(const AGameStateBase* GameState, const APlayerState* Player)
{
	if (!GameState || !Player)
	{
		return 0;
	}
	const int32 Index = GameState->PlayerArray.IndexOfByKey(Player);
	return Index == INDEX_NONE ? 0 : Index;
}

bool FGFShardhop::ShouldBringCompanion(bool bTamed, const FString& OwnerKey, const FString& PlayerKey, float DistanceCm)
{
	return bTamed && !PlayerKey.IsEmpty() && OwnerKey == PlayerKey && DistanceCm <= FGFZoneConstants::CompanionRadiusCm;
}

FName FGFShardhop::PickDestination(const FGFZoneCatalog& Catalog, const TArray<FName>& Saved, const FVector& From, float ViewYawDeg)
{
	FName Best = NAME_None;
	float BestDelta = MAX_FLT;
	for (const FGFShardstoneRow& Stone : Catalog.Stones)
	{
		if (!Catalog.IsUnlocked(Stone, Saved))
		{
			continue;
		}
		if (FVector::Dist2D(Stone.Location, From) <= FGFZoneConstants::HopRadiusCm)
		{
			continue;
		}
		const FVector Delta = Stone.Location - From;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		const float Err = FMath::Abs(FMath::FindDeltaAngleDegrees(ViewYawDeg, Yaw));
		if (Err < BestDelta)
		{
			BestDelta = Err;
			Best = Stone.Id;
		}
	}
	return Best;
}

FString FGFShardhop::DenyText(EGFShardhopResult Result)
{
	switch (Result)
	{
	case EGFShardhopResult::Villain:
		return TEXT("Shardhop waits until Villain Mode is off.");
	case EGFShardhopResult::RecentPvP:
		return TEXT("Shardhop waits out the fight.");
	case EGFShardhopResult::TooFar:
		return TEXT("Stand by an unlocked Shardstone.");
	case EGFShardhopResult::Locked:
		return TEXT("That Shardstone is still dark.");
	case EGFShardhopResult::NoStone:
		return TEXT("No such Shardstone.");
	default:
		return FString();
	}
}
