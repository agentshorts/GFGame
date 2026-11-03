#pragma once

// Who may take a faction seat. No world. AGFGameState::ServerSetFactionLeader applies Decide.
// The first claimer of an empty seat is a separate rule and stays in ServerClaimFaction.

#include "CoreMinimal.h"
#include "GFQuestTypes.h"

struct GFNET_API FGFFactionLead
{
	enum class EResult : uint8
	{
		Ok,
		Same,
		NotChampion,
		WrongFaction,
		Missing
	};

	/**
	 * A Champion of TargetFaction may take that seat.
	 * bHasPlayer is false when the key is not in this world.
	 * CurrentLeader equal to PlayerKey is Same, so a repeat call does not count as a change.
	 */
	static EResult Decide(bool bHasPlayer, EGFFactionRank Rank, FName PlayerFaction, FName TargetFaction, const FString& PlayerKey, const FString& CurrentLeader);

	static FName Code(EResult Result);
};
