#include "GFFerrySpawn.h"

FVector FGFPlayerStartRules::FallbackLocation(FName AnchorId, const TArray<FGFTerritoryFaction>& Layout)
{
	const FVector Origin(0.f, 0.f, FallbackZ);
	if (AnchorId.IsNone())
	{
		return Origin;
	}
	for (const FGFTerritoryFaction& Faction : Layout)
	{
		if (Faction.Id == AnchorId)
		{
			return FVector(Faction.Center.X, Faction.Center.Y, Faction.Center.Z + FallbackZ);
		}
	}
	return Origin;
}

FVector FGFPlayerStartRules::DefaultPerchLocation()
{
	return FVector(0.f, -300.f, 100.f);
}

FRotator FGFPlayerStartRules::DefaultPerchRotation()
{
	return FRotator(0.f, 36.f, 0.f);
}

FTransform FGFPlayerStartRules::EmptyMapStart()
{
	return FTransform(DefaultPerchRotation(), DefaultPerchLocation());
}

int32 FGFPlayerStartRules::ChooseStartIndex(const TArray<FGFMapPlayerStart>& Starts, FName AnchorId)
{
	if (Starts.Num() == 0)
	{
		return INDEX_NONE;
	}
	if (!AnchorId.IsNone())
	{
		for (int32 Index = 0; Index < Starts.Num(); ++Index)
		{
			if (Starts[Index].Tag == AnchorId)
			{
				return Index;
			}
		}
	}
	return 0;
}
