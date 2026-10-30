#pragma once

// Actors marked GF.FactionProtected. Lives in GFCore so a module can ask without depending on GFGame.

#include "CoreMinimal.h"

class AActor;

struct GFCORE_API FGFProtectedActor
{
	/** Actor tag applied to a faction-protected body. */
	static const FName Tag;

	static bool Is(const AActor* Actor);
};
