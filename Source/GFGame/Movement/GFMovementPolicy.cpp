#include "GFMovementPolicy.h"
#include "GFMiniGameGateSubsystem.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

namespace GFMovementPolicyDetail
{
	bool GFMovementIsConcreteGate(const UClass* Class)
	{
		return Class
			&& Class->IsNative()
			&& Class->IsChildOf(UGFMiniGameGateSubsystem::StaticClass())
			&& !Class->HasAnyClassFlags(CLASS_Abstract)
			&& !Class->GetName().StartsWith(TEXT("SKEL_"))
			&& !Class->GetName().StartsWith(TEXT("REINST_"));
	}

	const TArray<FString>& GFMovementGateShortNames()
	{
		// Built on first use, after game modules have registered their gate classes.
		// PerformMovement asks every frame on the server, so this walk does not repeat.
		static const TArray<FString> Names = []
		{
			TArray<FString> Found;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (!GFMovementIsConcreteGate(*It))
				{
					continue;
				}
				const UGFMiniGameGateSubsystem* Gate = Cast<UGFMiniGameGateSubsystem>(It->GetDefaultObject());
				if (!Gate || Gate->GetMapName().IsNone())
				{
					continue;
				}
				const FString Short = GFMovementPolicy::ShortMapName(Gate->GetMapName().ToString());
				if (!Short.IsEmpty())
				{
					Found.AddUnique(Short);
				}
			}
			return Found;
		}();
		return Names;
	}

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
	for (const FString& Gate : GFMovementPolicyDetail::GFMovementGateShortNames())
	{
		if (Short.Equals(Gate, ESearchCase::IgnoreCase))
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
