#include "GFZoneTags.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_Zone, "Zone");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Kindle, "Zone.Kindle");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Fringe, "Zone.Fringe");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Badlands, "Zone.Badlands");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_DeepBadlands, "Zone.DeepBadlands");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Faction, "Zone.Faction");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch, "Zone.Porch");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_Arena, "Zone.Porch.Arena");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_Arena_Core, "Zone.Porch.Arena.Core");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_PebbleMine, "Zone.Porch.PebbleMine");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_PebbleMine_Core, "Zone.Porch.PebbleMine.Core");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_BeatHop, "Zone.Porch.BeatHop");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_BeatHop_Core, "Zone.Porch.BeatHop.Core");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_Fashion, "Zone.Porch.Fashion");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_Fashion_Core, "Zone.Porch.Fashion.Core");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_Valley, "Zone.Porch.Valley");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_Valley_Core, "Zone.Porch.Valley.Core");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_HallowNight, "Zone.Porch.HallowNight");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_HallowNight_Core, "Zone.Porch.HallowNight.Core");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_GrumbleDig, "Zone.Porch.GrumbleDig");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_GrumbleDig_Core, "Zone.Porch.GrumbleDig.Core");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_Reserved1, "Zone.Porch.Reserved1");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Porch_Reserved1_Core, "Zone.Porch.Reserved1.Core");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Hazard, "Zone.Hazard");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Hazard_LavaBridge, "Zone.Hazard.LavaBridge");
UE_DEFINE_GAMEPLAY_TAG(TAG_Zone_Hazard_DeadlyForest, "Zone.Hazard.DeadlyForest");

void FGFZoneTags::Anchor()
{
	(void)TAG_Zone.GetTag();
	(void)TAG_Zone_Kindle.GetTag();
	(void)TAG_Zone_Fringe.GetTag();
	(void)TAG_Zone_Badlands.GetTag();
	(void)TAG_Zone_DeepBadlands.GetTag();
	(void)TAG_Zone_Faction.GetTag();
	(void)TAG_Zone_Porch.GetTag();
	(void)TAG_Zone_Porch_Arena.GetTag();
	(void)TAG_Zone_Porch_Arena_Core.GetTag();
	(void)TAG_Zone_Porch_PebbleMine.GetTag();
	(void)TAG_Zone_Porch_PebbleMine_Core.GetTag();
	(void)TAG_Zone_Porch_BeatHop.GetTag();
	(void)TAG_Zone_Porch_BeatHop_Core.GetTag();
	(void)TAG_Zone_Porch_Fashion.GetTag();
	(void)TAG_Zone_Porch_Fashion_Core.GetTag();
	(void)TAG_Zone_Porch_Valley.GetTag();
	(void)TAG_Zone_Porch_Valley_Core.GetTag();
	(void)TAG_Zone_Porch_HallowNight.GetTag();
	(void)TAG_Zone_Porch_HallowNight_Core.GetTag();
	(void)TAG_Zone_Porch_GrumbleDig.GetTag();
	(void)TAG_Zone_Porch_GrumbleDig_Core.GetTag();
	(void)TAG_Zone_Porch_Reserved1.GetTag();
	(void)TAG_Zone_Porch_Reserved1_Core.GetTag();
	(void)TAG_Zone_Hazard.GetTag();
	(void)TAG_Zone_Hazard_LavaBridge.GetTag();
	(void)TAG_Zone_Hazard_DeadlyForest.GetTag();
}
