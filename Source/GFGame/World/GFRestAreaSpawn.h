#pragma once

// Issue 335: the world map starts at the rest area on the Verdant to Cinderwright road, where the caravan lives.
// Tools/place_towns_v2.py places PlayerStart_RestArea (PlayerStartTag RestArea) from the rest area layout file, plus
// NPC spot TargetPoints (NpcSpotTag + spot id) and the GatherZone_RestArea box. AGFGameMode::ChoosePlayerStart asks
// Choose() which spawn to use: home once the player has one, else the rest area, else the usual map start rules.

#include "CoreMinimal.h"
#include "GFFerrySpawn.h"

enum class EGFSpawnSource : uint8
{
	/** The map's own starts (FGFPlayerStartRules::ChooseStartIndex), or the no-start fallback when there are none. */
	Map,
	/** The RestArea-tagged start: the default for a player with no home yet. */
	RestArea,
	/** The player's home (AGFPlayerState::SetHome). Only on a map with a rest area start, where the home lives. */
	Home,
};

struct GFGAME_API FGFRestAreaSpawnChoice
{
	EGFSpawnSource Source = EGFSpawnSource::Map;
	/** Index into the starts for Map and RestArea; INDEX_NONE for Home or when the map has no starts. */
	int32 Index = INDEX_NONE;
};

struct GFGAME_API FGFRestAreaSpawn
{
	/** PlayerStartTag of the rest area start (rest_area.START_TAG). */
	static const FName StartTag;
	/** TargetPoint tag on each caravan NPC spot (the second tag is the spot id). */
	static const FName NpcSpotTag;
	/** Tag on the rest area gather zone box. */
	static const FName GatherZoneTag;
	/** Tag on the campfire point light. */
	static const FName CampfireLightTag;
	/** Seconds a transient home start lives after it has been handed to the spawn. */
	static constexpr float HomeStartLifeSeconds = 10.f;

	/** Index of the RestArea-tagged start, INDEX_NONE when the map has none. */
	static int32 RestAreaIndex(const TArray<FGFMapPlayerStart>& Starts);

	/**
	 * Home when bHasHome and the map has a rest area start; else the rest area start; else Map with
	 * FGFPlayerStartRules::ChooseStartIndex(Starts, AnchorId).
	 */
	static FGFRestAreaSpawnChoice Choose(const TArray<FGFMapPlayerStart>& Starts, bool bHasHome, FName AnchorId);
};
