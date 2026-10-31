#pragma once

// The Kindle column on L_Hub: a 100 m spire and the shardstone cylinder share one ground point.
// That point is Stone.Kindle. The Rime stand is the origin, so the column stays off the spawn sightline.
// GFGame.World.SpawnSightline fails if the column or a solid overworld cell blocks that view.

#include "CoreMinimal.h"

namespace GFHubDressing
{
	/** Engine cube, 2 m square and 100 m tall. Centre pivot, then lifted to sit on Z = 0. */
	inline FVector SpireScale() { return FVector(2.f, 2.f, 100.f); }

	/** Gold sphere on top of the spire. */
	inline FVector CrownScale() { return FVector(6.f, 6.f, 6.f); }
	inline float CrownHeightCm() { return 10000.f; }

	/** Engine cylinder on the shardstone. 3 m across, 4 m tall. */
	inline FVector ShardstoneScale() { return FVector(3.f, 3.f, 4.f); }

	/**
	 * Ground radius of the column. Engine basic shapes are 100 cm and centre-pivoted,
	 * so half a side is scale * 50. The cylinder is wider than the spire.
	 */
	inline float ColumnRadiusCm()
	{
		const FVector Spire = SpireScale();
		const FVector Stone = ShardstoneScale();
		const float SpireHalf = FMath::Max(Spire.X, Spire.Y) * 50.f;
		const float StoneRadius = FMath::Max(Stone.X, Stone.Y) * 50.f;
		return FMath::Max(SpireHalf, StoneRadius);
	}

	/** Shipped Stone.Kindle XY when the row is missing. Kept off the spawn sightline. */
	inline FVector FallbackOrigin() { return FVector(-400.f, 400.f, 0.f); }

	inline FVector ColumnOrigin(const FVector& StoneLocation)
	{
		return FVector(StoneLocation.X, StoneLocation.Y, 0.f);
	}
}
