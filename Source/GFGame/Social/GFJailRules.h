#pragma once

// Pure jail permissions and leadership. No UObject, so automation tests can call this without a world.
// UGFJailSubsystem is what applies it on the server. Factions are opaque ids (AGFPlayerState::FactionId).
#include "CoreMinimal.h"

/** One player, as the rules see them. Filled from AGFPlayerState; not a UObject. */
struct GFGAME_API FGFJailActor
{
	/** AGFPlayerState::PlayerKey. Stable across a rejoin in the same session. */
	FString PlayerId;
	/** AGFPlayerState::FactionId. None = not in a faction. */
	FName FactionId;
	/** AGFPlayerState::bIsHost. */
	bool bHost = false;
	/**
	 * Order this player first claimed FactionId. Smaller = earlier. The first claimer of a faction is the
	 * lowest order among its members. 0 = none (no faction, or not recorded).
	 */
	int32 JoinOrder = 0;
	/** Still in the session. A logged-out player is not a remaining member. */
	bool bOnline = true;
};

/** Roster and the jailed set. The rules never reach into the world themselves. */
struct GFGAME_API FGFJailContext
{
	TArray<FGFJailActor> Players;
	/** Persistent player ids jailed right now. No timer: only jail and release change this. */
	TArray<FString> JailedIds;

	bool IsJailed(const FString& PlayerId) const;
};

/** One remaining faction member, for leader handover. */
struct GFGAME_API FGFJailMember
{
	FString PlayerId;
	int32 JoinOrder = 0;
	bool bJailed = false;
};

/** Result codes (same words the rest of the game uses: "Ok", "Self", "NotAllowed", ...). */
struct GFGAME_API FGFJailCodes
{
	static const FName Ok;
	static const FName Self;
	static const FName NotAllowed;
	static const FName TargetIsHost;
	static const FName AlreadyJailed;
	static const FName NotJailed;
	static const FName NoSuchPlayer;
	/** Jail/Release was called without server authority. The jailed flag was not touched. */
	static const FName NotAuthority;
};

struct GFGAME_API FGFJailRules
{
	/**
	 * Host: anyone except themselves. Faction leader: online members of their own faction, never the host,
	 * never themselves. Everyone else: denied. Already-jailed targets are AlreadyJailed.
	 * The leader is ChooseLeader on the faction's remaining (online) members.
	 */
	static FName CanJail(const FGFJailActor& Actor, const FGFJailActor& Target, const FGFJailContext& Context);
	/**
	 * Same power as CanJail, and the target must already be jailed. Not limited to whoever jailed them.
	 * There is no timer.
	 */
	static FName CanRelease(const FGFJailActor& Actor, const FGFJailActor& Target, const FGFJailContext& Context);

	/**
	 * Leader handover. Earliest JoinOrder among Remaining wins. A jailed member is skipped while any
	 * remaining member is free. If every remaining member is jailed, the earliest of them leads.
	 * Empty if Remaining is empty. Equal JoinOrder breaks ties by PlayerId.
	 */
	static FString ChooseLeader(const TArray<FGFJailMember>& Remaining);
	/** ChooseLeader for FactionId, using online players in Context. None and an empty faction have no leader. */
	static FString LeaderOf(FName FactionId, const FGFJailContext& Context);

	/** Voice and text. True exactly when PlayerId is in the jailed set. */
	static bool IsMuted(const FString& PlayerId, const FGFJailContext& Context);
	/** False exactly when the player is muted. */
	static bool CanEmote(const FString& PlayerId, const FGFJailContext& Context);
};
