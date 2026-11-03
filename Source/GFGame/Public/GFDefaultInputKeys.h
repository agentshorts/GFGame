#pragma once

// The default key table for the player and the HUD. BuildDefaultInput and the HUD mapping both read this,
// so a test can see that X belongs only to Interact.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

struct GFGAME_API FGFMappedKey
{
	FName Action;
	FKey Key;
	bool bSwizzle = false;
	bool bNegate = false;
};

struct GFGAME_API FGFDefaultInputKeys
{
	static FKey InteractKey();

	/** Pawn mapping, in the order the default context binds them. */
	static void CharacterKeys(TArray<FGFMappedKey>& Out);

	/** HUD mapping. ReturnKey is Home unless it was rebound. */
	static void HudKeys(TArray<FGFMappedKey>& Out, FKey ReturnKey);

	/** True when Key is on Action and on no other row. */
	static bool KeyIsOnly(const TArray<FGFMappedKey>& Keys, FKey Key, FName Action);
};
