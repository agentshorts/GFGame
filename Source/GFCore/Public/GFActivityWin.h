#pragma once

// A mini-game win, or a brand new character, told as a name. GFRhythm and GFMine broadcast.
// GFFashion listens and mints wardrobe pieces. Neither side includes the other.

#include "CoreMinimal.h"

class UWorld;

DECLARE_MULTICAST_DELEGATE_FourParams(FGFOnActivityWin, UWorld* /*World*/, const FString& /*PlayerKey*/, FName /*Activity*/, FName /*Context*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnNewCharacter, UWorld* /*World*/, const FString& /*PlayerKey*/);

struct GFCORE_API FGFActivityWin
{
	static FGFOnActivityWin& OnActivity();
	static FGFOnNewCharacter& OnNewCharacter();
};
