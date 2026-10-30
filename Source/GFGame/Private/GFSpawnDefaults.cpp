#include "GFSpawnDefaults.h"

FName FGFSpawnDefaults::StarterFactionId()
{
	return TEXT("Rime");
}

FVector FGFSpawnDefaults::StarterFallback(FName StarterId, const TArray<FGFSpawnAnchor>& Anchors)
{
	const FVector Origin(0.f, 0.f, FallbackZ);
	if (StarterId.IsNone())
	{
		return Origin;
	}
	for (const FGFSpawnAnchor& Anchor : Anchors)
	{
		if (Anchor.Id == StarterId)
		{
			return FVector(Anchor.Center.X, Anchor.Center.Y, Anchor.Center.Z + FallbackZ);
		}
	}
	return Origin;
}
