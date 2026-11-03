#pragma once

// Pure rules between the fashion stations and The Elder's Errand. No world.
// Atelier, the wardrobe and the runway ask these, then raise FGFQuestEvents.

#include "CoreMinimal.h"
#include "GFItemTypes.h"
#include "GFModeTypes.h"
#include "GFQuestLine.h"
#include "GFRunwayJudge.h"

enum class EGFFashionCarryState : uint8
{
	Empty,
	Holding
};

enum class EGFFashionCarryEvent : uint8
{
	Stash,
	Take
};

/**
 * Quest progress, faction seats and session factions for one map trip.
 * The game instance keeps one of these across OpenLevel. Stash and Take are the only writers.
 */
struct GFFASHION_API FGFFashionCarry
{
	EGFFashionCarryState State = EGFFashionCarryState::Empty;
	TArray<uint8> QuestBytes;
	TArray<FGFFactionLeader> Leaders;
	TMap<FString, FName> Sessions;

	static EGFFashionCarryState Transition(EGFFashionCarryState From, EGFFashionCarryEvent Event);

	void Stash(TArray<uint8> InBytes, TArray<FGFFactionLeader> InLeaders, TMap<FString, FName> InSessions);
	/** False when nothing is held. A second take is false and leaves the carry empty. */
	bool Take(TArray<uint8>& OutBytes, TArray<FGFFactionLeader>& OutLeaders, TMap<FString, FName>& OutSessions);
};

struct GFFASHION_API FGFFashionQuest
{
	static const FName HideId;
	static const FName ScalesId;
	static const FName AtelierStation;

	/** True when an input stack is Hide or Scales with a count above zero. */
	static bool UsesDinoMaterial(const TArray<FGFItemStack>& Inputs);

	/**
	 * Output item id for OutfitCrafted.
	 * None unless the craft succeeded at the Atelier and an input is Hide or Scales.
	 */
	static FName CraftEventItem(bool bCrafted, FName StationId, const TArray<FGFItemStack>& Inputs, FName OutputItemId);

	/** Piece id for OutfitWorn. None when the equip was refused or the id is empty. */
	static FName WornEventItem(bool bWorn, FName PieceId);

	/** EGFRunwayTier as the quest score: C 0, B 1, A 2, S 3. */
	static int32 TierValue(EGFRunwayTier Tier);

	/**
	 * WinShow. Place 1 and tier A or S, matching FGFQuestShowRule.
	 * ContestantCount of 1 is a normal show. Zero contestants is not a win.
	 */
	static bool IsShowWin(int32 Place, int32 Tier, int32 ContestantCount);
};
