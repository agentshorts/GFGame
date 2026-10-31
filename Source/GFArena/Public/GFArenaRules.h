#pragma once

// Arena rule helpers (roster caps, family rep, purse, side pots, anti-cheese).
// Pure: no world, no director. The director calls these and the automation tests drive them directly.

#include "CoreMinimal.h"
#include "GFArenaTypes.h"

/** How many dinos one player may place, and how a side is allowed to be built. */
struct GFARENA_API FGFArenaRosterRules
{
	static constexpr int32 SideCap = 6;
	static constexpr int32 SoloPartyCap = 3;
	static constexpr int32 SpeciesCap = 2;

	/** Under 3 players a side, each still brings up to 3. At 3 or more, floor(6 / players), at least 1. */
	static int32 PerPlayerCap(int32 PlayersOnSide);

	/**
	 * Ok, PartySplit, SpeciesCap, or TeamFull.
	 * AlreadyOnSide is everyone else's dinos. Adding is this player's species list, in place order.
	 * SideSpecies is the species already placed by the other players.
	 */
	static FName CheckPlacement(int32 PlayersOnSide, int32 AlreadyOnSide, const TArray<FName>& SideSpecies, const TArray<FName>& Adding);

	/** Species index Roll, walking forward until that species is under the cap. */
	static FName PickSpecies(const TArray<FName>& SpeciesIds, const TMap<FName, int32>& Used, int32 Roll);
};

/** Per-family rep bands used by rentals and rival AI. */
enum class EGFArenaStanding : uint8
{
	Hostile,
	Wary,
	Neutral,
	Friendly
};

struct GFARENA_API FGFArenaRep
{
	static constexpr int32 FriendlyAt = 20;
	static constexpr int32 WaryAt = -8;
	static constexpr int32 HostileAt = -40;

	static constexpr int32 WinDelta = 3;
	static constexpr int32 QuitDelta = -5;
	static constexpr int32 InsultDelta = -1;

	static EGFArenaStanding Standing(int32 Rep);
	static bool CanRent(int32 Rep);
	static float RentalPriceMul(int32 Rep);
	static bool ArrivesWellFed(int32 Rep);
	static bool CanControl(int32 Rep);
	static bool ForcesChampion(int32 Rep);
};

/** Arena purse is bookkeeping on the dino. It does not take items from the player. */
struct GFARENA_API FGFArenaPurse
{
	/** 5% of the match reward value, rounded down. */
	static int32 Purse(int32 RewardValue);
	/** 10% of a rental's material winnings, the elder's cut. */
	static int32 ElderCut(int32 WinningsValue);
};

struct GFARENA_API FGFArenaSideBet
{
	FString Key;
	int32 Side = 0;
	int64 Value = 0;
};

struct GFARENA_API FGFArenaSidePots
{
	/**
	 * Key that receives the losing stakes. Empty when the fight is a draw, nobody bet the winning side,
	 * or the winning bets are tied on value (tie breaks to the earlier key, so empty only for a draw
	 * or an empty winning side). Tied values pick the lexicographic smaller key.
	 */
	static FString PotWinner(EGFArenaOutcome Outcome, const TArray<FGFArenaSideBet>& Bets);
};

/** Laundering limit and the free-roll rate limit. Times are seconds on the same clock the caller uses. */
struct GFARENA_API FGFArenaCheese
{
	static constexpr double HourSeconds = 3600.0;
	static constexpr int32 PvpWagersPerHour = 3;
	static constexpr int32 RollsPerHour = 10;

	static int32 CountInWindow(const TArray<double>& Times, double Now, double WindowSeconds);
	static bool WagersBlocked(const TArray<double>& PairTimes, double Now);
	/** Stars fed into the free roll. After the hourly cap the roll is the 0-star table. */
	static int32 RollStars(int32 Stars, int32 RollsThisHour);
};

/** Snack before the fight. Wrong food is a refusal here; the attack lives on the wild tame. */
struct GFARENA_API FGFArenaSnack
{
	/** Ok, NotPlaced, AlreadyFed, NoFood, or Refused. */
	static FName Check(bool bPlaced, bool bAlreadyFed, bool bHasItem, bool bFavourite);
};

/** Lines elders and the book can read. Kinds: Brag, RentalQuit, ElderNice. */
struct GFARENA_API FGFArenaPost
{
	FName Kind;
	FString Line;

	static FGFArenaPost Brag(int32 Stars);
	static FGFArenaPost RentalQuit(FName Species);
	static FGFArenaPost ElderNice(FName Family);
};
