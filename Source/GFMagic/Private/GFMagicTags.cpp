#include "GFMagicTags.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Mining, "Ability.Mining");
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Mining_Field, "Ability.Mining.Field");
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Mining_Clawcall, "Ability.Mining.Clawcall");
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Mining_ToggleElement, "Ability.Mining.ToggleElement");
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Mining_Overcharge, "Ability.Mining.Overcharge");

UE_DEFINE_GAMEPLAY_TAG(TAG_Element_Spark, "Element.Spark");
UE_DEFINE_GAMEPLAY_TAG(TAG_Element_Magma, "Element.Magma");
UE_DEFINE_GAMEPLAY_TAG(TAG_Element_Root, "Element.Root");
UE_DEFINE_GAMEPLAY_TAG(TAG_Element_Frost, "Element.Frost");

UE_DEFINE_GAMEPLAY_TAG(TAG_Cell_Mark_Charged, "Cell.Mark.Charged");
UE_DEFINE_GAMEPLAY_TAG(TAG_Cell_Mark_Burning, "Cell.Mark.Burning");
UE_DEFINE_GAMEPLAY_TAG(TAG_Cell_Mark_Rooted, "Cell.Mark.Rooted");
UE_DEFINE_GAMEPLAY_TAG(TAG_Cell_Mark_Frozen, "Cell.Mark.Frozen");

UE_DEFINE_GAMEPLAY_TAG(TAG_Combo_Slagburst, "Combo.Slagburst");
UE_DEFINE_GAMEPLAY_TAG(TAG_Combo_LightningRoot, "Combo.LightningRoot");
UE_DEFINE_GAMEPLAY_TAG(TAG_Combo_Shatterfrost, "Combo.Shatterfrost");
UE_DEFINE_GAMEPLAY_TAG(TAG_Combo_SteamBlast, "Combo.SteamBlast");
UE_DEFINE_GAMEPLAY_TAG(TAG_Combo_Wildfire, "Combo.Wildfire");
UE_DEFINE_GAMEPLAY_TAG(TAG_Combo_Frostbloom, "Combo.Frostbloom");

UE_DEFINE_GAMEPLAY_TAG(TAG_Faction_Cinderwrights, "Faction.Cinderwrights");
UE_DEFINE_GAMEPLAY_TAG(TAG_Faction_Verdant, "Faction.Verdant");
UE_DEFINE_GAMEPLAY_TAG(TAG_Faction_Rime, "Faction.Rime");

UE_DEFINE_GAMEPLAY_TAG(TAG_Path_Delver, "Path.Delver");
UE_DEFINE_GAMEPLAY_TAG(TAG_Path_Wrangler, "Path.Wrangler");
UE_DEFINE_GAMEPLAY_TAG(TAG_Path_Scavenger, "Path.Scavenger");
UE_DEFINE_GAMEPLAY_TAG(TAG_Path_PendingA, "Path.PendingA");
UE_DEFINE_GAMEPLAY_TAG(TAG_Path_PendingB, "Path.PendingB");

UE_DEFINE_GAMEPLAY_TAG(TAG_GameplayCue_Mining_Field, "GameplayCue.Mining.Field");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameplayCue_Mining_Clawcall, "GameplayCue.Mining.Clawcall");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameplayCue_Mining_Magnet, "GameplayCue.Mining.Magnet");

UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Mining_Break, "Event.Mining.Break");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Mining_Quench, "Event.Mining.Quench");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Mining_Drop, "Event.Mining.Drop");

UE_DEFINE_GAMEPLAY_TAG(TAG_Upgrade_Mining, "Upgrade.Mining");

void FGFMagicTags::Anchor()
{
	(void)TAG_Ability_Mining.GetTag();
	(void)TAG_Ability_Mining_Field.GetTag();
	(void)TAG_Ability_Mining_Clawcall.GetTag();
	(void)TAG_Ability_Mining_ToggleElement.GetTag();
	(void)TAG_Ability_Mining_Overcharge.GetTag();
	(void)TAG_Element_Spark.GetTag();
	(void)TAG_Element_Magma.GetTag();
	(void)TAG_Element_Root.GetTag();
	(void)TAG_Element_Frost.GetTag();
	(void)TAG_Cell_Mark_Charged.GetTag();
	(void)TAG_Cell_Mark_Burning.GetTag();
	(void)TAG_Cell_Mark_Rooted.GetTag();
	(void)TAG_Cell_Mark_Frozen.GetTag();
	(void)TAG_Combo_Slagburst.GetTag();
	(void)TAG_Combo_LightningRoot.GetTag();
	(void)TAG_Combo_Shatterfrost.GetTag();
	(void)TAG_Combo_SteamBlast.GetTag();
	(void)TAG_Combo_Wildfire.GetTag();
	(void)TAG_Combo_Frostbloom.GetTag();
	(void)TAG_Faction_Cinderwrights.GetTag();
	(void)TAG_Faction_Verdant.GetTag();
	(void)TAG_Faction_Rime.GetTag();
	(void)TAG_Path_Delver.GetTag();
	(void)TAG_Path_Wrangler.GetTag();
	(void)TAG_Path_Scavenger.GetTag();
	(void)TAG_Path_PendingA.GetTag();
	(void)TAG_Path_PendingB.GetTag();
	(void)TAG_GameplayCue_Mining_Field.GetTag();
	(void)TAG_GameplayCue_Mining_Clawcall.GetTag();
	(void)TAG_GameplayCue_Mining_Magnet.GetTag();
	(void)TAG_Event_Mining_Break.GetTag();
	(void)TAG_Event_Mining_Quench.GetTag();
	(void)TAG_Event_Mining_Drop.GetTag();
	(void)TAG_Upgrade_Mining.GetTag();
}
