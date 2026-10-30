#pragma once

// Native zone tags. UE_DEFINE_GAMEPLAY_TAG lives in GFZoneTags.cpp. Anchor() keeps the
// registrars linked so a Windows linker with /OPT:REF does not drop them.

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Kindle);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Fringe);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Badlands);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_DeepBadlands);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Faction);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_Arena);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_Arena_Core);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_PebbleMine);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_PebbleMine_Core);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_BeatHop);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_BeatHop_Core);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_Fashion);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_Fashion_Core);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_Valley);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_Valley_Core);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_HallowNight);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_HallowNight_Core);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_GrumbleDig);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_GrumbleDig_Core);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_Reserved1);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Porch_Reserved1_Core);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Hazard);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Hazard_LavaBridge);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Zone_Hazard_DeadlyForest);

/** Touches every registrar. Call once when the catalog loads. Same module only. */
struct FGFZoneTags
{
	static void Anchor();
};
