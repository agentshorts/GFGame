#pragma once

// Home-zone leash and the revenge-gang travel record.
// This is not IGFSafeZoneQuery (that keeps the giant out of player safety) and not a taming PackId
// (that only spreads hostility). Map packages stay on the mini-game gates; this file only names the
// three factions the session cap is counted from.

#include "CoreMinimal.h"

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

/**
 * Circle the dino is allowed to path inside. Horizontal only (Z is kept, not tested), same as the safe-zone
 * helpers, but this circle is a leash: home dinos stay in, they are not pushed out.
 */
struct GFGAME_API FGFDinoHomeZone
{
	FName ZoneId = NAME_None;
	FVector Center = FVector::ZeroVector;
	/** cm. <= 0 means only the centre point is inside. */
	float RadiusCm = 0.f;

	bool Contains(const FVector& Point) const;
	/** Point unchanged when it is inside; otherwise the nearest point on the circle, with Point.Z kept. */
	FVector Clamp(const FVector& Point) const;
};

/** Who the revenge gang is chasing. Copied off AGFPlayerState so it does not need the actor to stay alive. */
struct GFGAME_API FGFGangTarget
{
	/** AGFPlayerState::PlayerKey. */
	FString PlayerKey;
	/** AGFPlayerState::FactionId. None skips the faction check when rebinding. */
	FName FactionId = NAME_None;
	/** AGFPlayerState::bIsHost. */
	bool bIsHost = false;

	bool IsSet() const { return !PlayerKey.IsEmpty(); }
	bool SameAs(const FGFGangTarget& Other) const
	{
		return PlayerKey == Other.PlayerKey && FactionId == Other.FactionId && bIsHost == Other.bIsHost;
	}

	static FGFGangTarget FromPlayer(const AGFPlayerState& PS);
};

/** One dino in the gang. Id is stable across the map load (not an actor unique id). Species is a diet-table row. */
struct GFGAME_API FGFRoamingMember
{
	FName Id;
	FName Species;
};

/**
 * What map travel keeps when the world is torn down. BeginTravel fills it; Consume clears bPending.
 * A second consume does nothing and leaves the gang that already applied it alone.
 */
struct GFGAME_API FGFRoamingHandoff
{
	bool bPending = false;
	FName FromMap = NAME_None;
	FName ToMap = NAME_None;
	FGFGangTarget Target;
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

/**
 * The session's factions. Opaque ids everywhere else; these three names are the ones this game seats.
 * Max players is Count() * AGFGameState::MaxPerFaction, never a written-down total.
 */
namespace GFRoamingFactions
{
	GFGAME_API FName Cinderwrights();
	GFGAME_API FName Verdant();
	GFGAME_API FName Rime();
	GFGAME_API const TArray<FName>& All();
	inline int32 Count() { return All().Num(); }
}

namespace GFRoamingLimits
{
	/** AGFGameState::MaxPerFaction from GS, or from the class default when GS is null. */
	GFGAME_API int32 SeatsPerFaction(const AGFGameState* GS);
	/** Faction count times seats. Seats <= 0 yields 0. */
	GFGAME_API int32 MaxSessionPlayers(int32 SeatsPerFaction);
}

namespace GFRoamingBind
{
	/**
	 * Player on GS that still matches the hand-off (key, host flag, faction when the record has one).
	 * Logged-out players are not a target. Null when the record is empty or the player no longer matches.
	 */
	GFGAME_API AGFPlayerState* FindTarget(const AGFGameState& GS, const FGFGangTarget& Target);
}
