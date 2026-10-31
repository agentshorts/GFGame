#pragma once

// Return gates for the Beat Hop course that is on the pad (#278).
// The hub director's untagged Beat Hop gate stays where #266 put it. This pair moves with the selection.

#include "CoreMinimal.h"

class AGFBeatHopCourse;

namespace GFBeatHopMapGates
{
	GFGAME_API FName StartTag();
	GFGAME_API FName FinishTag();

	/** Server. No-ops until the untagged Beat Hop return gate exists, so Find still returns that one. */
	GFGAME_API void Refresh(AGFBeatHopCourse* Course);
}
