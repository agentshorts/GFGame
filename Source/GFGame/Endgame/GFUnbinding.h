#pragma once

// The Unbinding (mode id "Unbinding"). A repeatable gathering whose only yield is one taming material for
// Big Grumble, given to the recipient the rule names. It does not release anyone, drop anyone from the
// session, or rewrite faction, host, presence, net id, or ties. Spending the material is a later step, and
// only once Big Grumble has spawned: the spawn itself tames nobody.
//
// Player fields are the ones AGFPlayerState already carries (PlayerKey, bIsHost, FactionId, bLoggedOut,
// ColorIndex). Jail and social ties are observed here so a caller can prove they are left alone; this
// folder does not call UGFSocialComponent::ServerRelease or anything under GFJail*. The default headcount is
// the game's max player count (GameSession MaxPlayers, 10 in DefaultGame.ini), not per-faction seats.

#include "CoreMinimal.h"
#include "GFItemTypes.h"

class AGFDino;
class AGFGameState;
class AGFPlayerState;
class UDataTable;

namespace GFUnbindingIds
{
	inline FName ModeId() { return TEXT("Unbinding"); }
	/** Granted by a completion and burned by a successful tame. Not a diet food. */
	inline FName MaterialId() { return TEXT("BigGrumbleTame"); }
	/** DT_GiantDino display name (row "Giant"). */
	inline const TCHAR* GiantDisplayName() { return TEXT("Big Grumble"); }
}

/** Who receives the material for one completion. */
enum class EGFUnbindingRecipient : uint8
{
	/** The present player with bIsHost. Default. */
	HostOnly,
	/** Tuning.ExplicitRecipientKey, and only if that player is present. */
	Explicit,
	/** The next present player after the previous completion's recipient (roster order). */
	NextPresent
};

struct FGFUnbindingTuning
{
	/**
	 * How many present players the gathering needs. <= 0 means the session max (GameSession MaxPlayers),
	 * so the default is "everyone who can be here" (10).
	 */
	int32 RequiredPlayers = 0;
	EGFUnbindingRecipient RecipientRule = EGFUnbindingRecipient::HostOnly;
	FString ExplicitRecipientKey;
};

/** One player, as the gathering reads them. Copied in; never written back. */
struct FGFUnbindingPlayer
{
	FString Key;
	FName FactionId;
	bool bHost = false;
	/** In the session for this gathering. Logged-out players are not present. */
	bool bPresent = false;
	bool bLoggedOut = false;
	/** Observed only. The gathering never clears it. */
	bool bJailed = false;
	int32 ColorIndex = 0;
	FString NetId;
	/** Other player keys this player is tied to. Observed only. */
	TArray<FString> TieKeys;

	bool SameNetwork(const FGFUnbindingPlayer& Other) const
	{
		return Key == Other.Key
			&& FactionId == Other.FactionId
			&& bHost == Other.bHost
			&& bPresent == Other.bPresent
			&& bLoggedOut == Other.bLoggedOut
			&& bJailed == Other.bJailed
			&& ColorIndex == Other.ColorIndex
			&& NetId == Other.NetId
			&& TieKeys == Other.TieKeys;
	}

	/** Reads AGFPlayerState. Does not read jail or ties (those stay on the social component). */
	static FGFUnbindingPlayer FromPlayerState(const AGFPlayerState& PS);
};

struct FGFUnbindingCompletion
{
	/** 1-based, in the order gatherings finished. */
	int32 Index = 0;
	FString RecipientKey;
	EGFUnbindingRecipient Rule = EGFUnbindingRecipient::HostOnly;
};

struct FGFUnbindingCodes
{
	static const FName Ok;
	static const FName TooFewPlayers;
	static const FName NoRecipient;
	static const FName RecipientAbsent;
	static const FName NoMaterial;
	static const FName NotSpawned;
	/** AGFDino (diet tame) is not Big Grumble. */
	static const FName NotBigGrumble;
	/** The bags do not hold unique items from three of the five classes. */
	static const FName NeedPaths;
};

struct FGFUnbindingResult
{
	bool bOk = false;
	FName Code;
	FString RecipientKey;
	/** True only when a tame burned one material. */
	bool bConsumed = false;
};

struct FGFUnbindingRules
{
	static constexpr int32 MaterialPerCompletion = 1;

	/** Used only when no GameSession max is available. */
	static constexpr int32 DefaultSessionMaxPlayers = 10;
	/** The game's max player count: GS's world GameSession MaxPlayers, else the AGameSession class default
	 *  (DefaultGame.ini), else DefaultSessionMaxPlayers. Never derived from AGFGameState::MaxPerFaction. */
	static int32 SessionMaxPlayers(const AGFGameState* GS = nullptr);
	/** Tuning.RequiredPlayers when set, otherwise SessionMaxPlayers. */
	static int32 RequiredCount(const FGFUnbindingTuning& Tuning, const AGFGameState* GS = nullptr);

	static bool IsPresent(const FGFUnbindingPlayer& Player);
	static int32 PresentCount(const TArray<FGFUnbindingPlayer>& Players);

	static bool IsBigGrumbleName(const FString& DisplayName);
	/** Any named AGFDino species is a companion (diet machine); Big Grumble is AGFGiantDino. The material never applies to companions. */
	static bool IsCompanionSpecies(FName Species);

	/** Online players on the game state. Logged-out copies are not present. */
	static TArray<FGFUnbindingPlayer> ReadSession(const AGFGameState& GS);
};

/**
 * Server-side record of gatherings and tames. Bags hold the taming material (and anything the caller
 * already stored). TryComplete and AttemptTame are the only mutators; both leave the player list alone.
 */
struct FGFUnbinding
{
	FGFUnbindingTuning Tuning;
	bool bGiantSpawned = false;
	TMap<FString, FGFInventory> Bags;
	TArray<FGFUnbindingCompletion> Completions;
	TMap<FString, int32> TamedCounts;

	int32 MaterialOf(const FString& Key) const;
	bool HasTamed(const FString& Key) const;
	int32 TameCount(const FString& Key) const;
	/** Keys with at least one successful tame, sorted. */
	TArray<FString> Tamers() const;

	/**
	 * One gathering. Fails with TooFewPlayers when fewer than the required number are present, and then
	 * grants nothing. On success, only the named recipient gains MaterialPerCompletion of the material.
	 */
	FGFUnbindingResult TryComplete(const TArray<FGFUnbindingPlayer>& Players);

	/**
	 * Big Grumble has spawned (AGFGiantDino display name). Does not grant, consume, or tame.
	 * Any other name (including a companion species) is ignored.
	 */
	bool NotifySpawned(const FString& DisplayName);
	/** He left. Materials stay; a tame now fails until the next spawn. */
	void NotifyGone();

	/**
	 * Spend one material on the spawned giant. Fails (and keeps the material) when he has not spawned
	 * or the player is not holding it. Companion bodies are refused without spending.
	 */
	FGFUnbindingResult AttemptTame(const FString& ByKey);
	/**
	 * The endgame tame. Needs unique items from three of the five classes somewhere in Bags, then spends
	 * one of each (out of the bag that already holds it) and one tamestone from ByKey.
	 * AttemptTame alone still only spends the tamestone, so older callers stay put.
	 */
	FGFUnbindingResult AttemptEndgame(const FString& ByKey, const UDataTable* Items);
	FGFUnbindingResult RejectCompanion(const AGFDino* Dino) const;

	/** Server: make PS's inventory match the material this record says they hold. Faction and host stay put. */
	bool PushMaterial(AGFPlayerState& PS) const;
};
