#pragma once

// Arena wagers (PLAN.md M6): materials only, capped, and the AI always puts up a fair counter-bet.
#include "CoreMinimal.h"
#include "GFItemTypes.h"

class UDataTable;
struct FGFRecipeRow;

enum class EGFWagerCheck : uint8
{
	Ok,
	WagersOff,
	Empty,
	TooManyStacks,
	InvalidStacks,
	NotWagerable,
	Insufficient,
	OverCap,
	PinnedProtected
};

struct GFARENA_API FGFWagerRules
{
	static constexpr float CapFraction = 0.25f;
	static constexpr float SuggestFraction = 0.10f;
	static constexpr int32 MaxStakeStacks = 3;
	static constexpr int32 MaxCounterStacks = 4;
	static constexpr float ValueTolerance = 0.10f;

	static const TCHAR* ToString(EGFWagerCheck R);

	/** Biggest total stake value allowed: 25 % of her inventory value (rounded down). */
	static int64 CapValue(const UDataTable* Items, const FGFInventory& Inv);
	/** How many of ItemId the pinned recipe needs (protected from wagers). */
	static int32 ProtectedCount(const FGFRecipeRow* Pinned, FName ItemId);
	/** Validates her stake against her inventory and the safeguards. Stake must be normalised (unique ids). */
	static EGFWagerCheck CheckStake(const UDataTable* Items, const FGFInventory& Inv, const TArray<FGFItemStack>& Stake, const FGFRecipeRow* Pinned, bool bWagersOff);
	/** About 10 % of her inventory value in up to 3 readable stacks, never protected items (empty if nothing fits). */
	static TArray<FGFItemStack> Suggest(const UDataTable* Items, const FGFInventory& Inv, const FGFRecipeRow* Pinned);
	/** What she needs: inputs of the pinned recipe she can't cover yet (then the pinned inputs themselves). */
	static TArray<FName> Needs(const FGFInventory& Inv, const FGFRecipeRow* Pinned);

	/**
	 * The AI's counter-bet (seeded, deterministic): value within +-10 % of the stake and in the same band,
	 * every item tier within +-1 of her tiers, >= 50 % of the value from her staked categories and the rest from
	 * her needs, at most 4 stacks, never exclusive items (Hallow Night / lab drops stay where they belong).
	 * Empty if no fair counter exists (she gets asked to change her stake).
	 */
	static TArray<FGFItemStack> MatchCounter(const UDataTable* Items, const TArray<FGFItemStack>& Stake, const TArray<FName>& InNeeds, int32 Seed);
	/** Checks every counter-bet rule above; Why gets the first broken one. */
	static bool CheckCounter(const UDataTable* Items, const TArray<FGFItemStack>& Stake, const TArray<FGFItemStack>& Counter, const TArray<FName>& InNeeds, FString* Why = nullptr);
};
