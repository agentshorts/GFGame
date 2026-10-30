#pragma once

// Mini-game maps leave stamina off. A mode opts in by saying so (game mode interface or the world subsystem).
// The hub and any map that is not one of the four gates stay on.

#include "CoreMinimal.h"

class UWorld;

namespace GFMovementPolicy
{
	/** Last path segment, without a PIE prefix and without a .Asset suffix. */
	GFGAME_API FString ShortMapName(const FString& MapOrPackage);

	/** True for the taming, fashion, arena and Beat Hop gate maps. */
	GFGAME_API bool IsMiniGameMapName(const FString& MapOrPackage);

	GFGAME_API bool IsMiniGameWorld(const UWorld* World);

	/**
	 * bModeOptedIn means the mode made a choice. bOptInValue is that choice.
	 * With no choice, mini-game maps are off and everything else is on.
	 */
	GFGAME_API bool IsStaminaEnabled(const FString& MapOrPackage, bool bModeOptedIn, bool bOptInValue);
}
