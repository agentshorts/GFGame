#pragma once

// Mantle query for Beat Hop (#277). The climb itself is #259 and is not on staging yet.
// Obstacle judging calls GFBeatHopNinja::IsMantleImplemented, which GFGame points at IsImplemented.

#include "CoreMinimal.h"
#include "GFBeatHopNinja.h"

namespace GFBeatHopMantle
{
	struct GFGAME_API FQuery
	{
		float HeightCm = 0.f;
		bool bForwardHeld = false;
		bool bAirborne = false;
		EGFBeatDifficulty Difficulty = EGFBeatDifficulty::Easy;
	};

	struct GFGAME_API FResult
	{
		/** False until the movement mantle from #259 exists. */
		bool bAvailable = false;
		bool bWouldAutoMantle = false;
		bool bWouldGrab = false;
		bool bBonk = false;
		/** Beat Hop runs do not spend stamina on a mantle. */
		bool bFreeInRun = false;
	};

	GFGAME_API bool IsImplemented();
	GFGAME_API FResult Query(const FQuery& Query, const FGFBeatHopTraversalProfile& Profile);
}
