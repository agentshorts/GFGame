#pragma once

// Home-zone leash and the revenge-gang travel record.
// The leash reuses FGFSafeCircles. Home dinos stay inside those circles; this is not the giant's
// stay-out rule and not a taming PackId. The gang target is UGFTamingComponent::GetTargetKey().
// Faction ids stay on AGFPlayerState. This file does not name them.

#include "CoreMinimal.h"
#include "GFSafeZone.h"

class AGFPlayerState;
class AGFGameState;

/** A place a dino may or may not enter. Id is an existing mode/zone name (DinoZone, OpenWorld, Moba, Rhythm, ...). */
struct GFGAME_API FGFRoamZone
{
	FName Id = NAME_None;

	FGFRoamZone() = default;
	explicit FGFRoamZone(FName InId) : Id(InId) {}
};

/** A map package (the gates' GetMapName(), e.g. /Game/Maps/L_Arena). */
struct GFGAME_API FGFRoamMap
{
	FName Id = NAME_None;

	FGFRoamMap() = default;
	explicit FGFRoamMap(FName InId) : Id(InId) {}
};

/** One dino in the gang. Id is unique and stable across the map load (not an actor unique id, and not the species). */
struct GFGAME_API FGFRoamingMember
{
	FName Id;
	FName Species;
};

/**
 * What map travel keeps when the world is torn down. BeginTravel fills it when the traveller is the
 * gang's target; Consume clears bPending. A second consume does nothing.
 */
struct GFGAME_API FGFRoamingHandoff
{
	bool bPending = false;
	FName FromMap = NAME_None;
	FName ToMap = NAME_None;
	/** UGFTamingComponent::GetTargetKey(). Empty means the gang is not chasing anyone. */
	FString TargetKey;
	TArray<FGFRoamingMember> Members;
};

/** Where a follow step is allowed to go. bMove false means stay put. */
struct GFGAME_API FGFFollowDecision
{
	bool bMove = false;
	FVector Goal = FVector::ZeroVector;
};

/** TryAdd results. */
namespace GFRoamingCodes
{
	inline FName Ok() { return FName(TEXT("Ok")); }
	inline FName Already() { return FName(TEXT("Already")); }
	inline FName Full() { return FName(TEXT("Full")); }
	inline FName NoDino() { return FName(TEXT("NoDino")); }
}

namespace GFRoamingLimits
{
	/**
	 * AGameSession::MaxPlayers. The world's game session when GS has one, otherwise the class default
	 * (DefaultGame.ini). <= 0 means the gang roster has no cap.
	 */
	GFGAME_API int32 MaxPlayers(const AGFGameState* GS);
}

namespace GFRoamingBind
{
	/**
	 * Player on GS whose PlayerKey is TargetKey. Logged-out players are not a target.
	 * Null when the key is empty or that player is gone.
	 */
	GFGAME_API AGFPlayerState* FindTarget(const AGFGameState& GS, const FString& TargetKey);
}
