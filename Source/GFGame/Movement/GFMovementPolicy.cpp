#include "GFMovementPolicy.h"
#include "GFBeatHopGateSubsystem.h"
#include "GFFashionGateSubsystem.h"
#include "GFMobaGateSubsystem.h"
#include "GFTamingGateSubsystem.h"
#include "Engine/World.h"

namespace GFMovementPolicyDetail
{
	FString GFMovementStripPiePrefix(const FString& Name)
	{
		if (!Name.StartsWith(TEXT("UEDPIE_")))
		{
			return Name;
		}
		const int32 Marker = Name.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7);
		if (Marker == INDEX_NONE)
		{
			return Name;
		}
		return Name.Mid(Marker + 1);
	}
}

FString GFMovementPolicy::ShortMapName(const FString& MapOrPackage)
{
	FString Name = MapOrPackage;
	int32 Slash = INDEX_NONE;
	if (Name.FindLastChar(TEXT('/'), Slash))
	{
		Name = Name.Mid(Slash + 1);
	}
	Name = GFMovementPolicyDetail::GFMovementStripPiePrefix(Name);
	int32 Dot = INDEX_NONE;
	if (Name.FindChar(TEXT('.'), Dot))
	{
		Name = Name.Left(Dot);
	}
	return Name;
}

bool GFMovementPolicy::IsMiniGameMapName(const FString& MapOrPackage)
{
	const FString Short = ShortMapName(MapOrPackage);
	if (Short.IsEmpty())
	{
		return false;
	}
	const FName Gates[] = {
		GetDefault<UGFTamingGateSubsystem>()->GetMapName(),
		GetDefault<UGFFashionGateSubsystem>()->GetMapName(),
		GetDefault<UGFMobaGateSubsystem>()->GetMapName(),
		GetDefault<UGFBeatHopGateSubsystem>()->GetMapName(),
	};
	for (const FName Gate : Gates)
	{
		if (Short.Equals(ShortMapName(Gate.ToString()), ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool GFMovementPolicy::IsMiniGameWorld(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	if (IsMiniGameMapName(World->GetMapName()))
	{
		return true;
	}
	const UPackage* Package = World->GetOutermost();
	return Package && IsMiniGameMapName(Package->GetName());
}

bool GFMovementPolicy::IsStaminaEnabled(const FString& MapOrPackage, bool bModeOptedIn, bool bOptInValue)
{
	if (bModeOptedIn)
	{
		return bOptInValue;
	}
	return !IsMiniGameMapName(MapOrPackage);
}
