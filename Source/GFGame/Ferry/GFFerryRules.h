#pragma once

// Pure ferry rules: fare, lifetime pass, flight arc, and which points may spawn land-route mobs.
// No world, no faction names, no species names. Callers pass config and inventory.

#include "CoreMinimal.h"
#include "GFEconomy.h"
#include "GFFerryTypes.h"
#include "GFItemTypes.h"
#include "GFTerritoryTypes.h"

struct GFGAME_API FGFFerryCodes
{
	static const FName Ok;
	static const FName UnknownStop;
	static const FName NotAtStop;
	static const FName CantAfford;
	static const FName AlreadyHasPass;
	static const FName InFlight;
	static const FName NotAuthority;
};

struct GFGAME_API FGFFerryResult
{
	bool bOk = false;
	FName Code;
};

/** What a fare charge will do. bCharge false means the trip is free or this TxId already paid. */
struct GFGAME_API FGFFerryFarePlan
{
	FGFFerryResult Result;
	bool bCharge = false;
	TArray<FGFItemStack> Stacks;
};

/** Burn then mint for a lifetime pass. The mint runs only after the burn has applied. */
struct GFGAME_API FGFFerryPassPlan
{
	FGFFerryResult Result;
	bool bBurn = false;
	bool bMint = false;
	TArray<FGFItemStack> BurnStacks;
	TArray<FGFItemStack> MintStacks;
	FGuid BurnTx;
	FGuid MintTx;
};

/** A player the land-route spawner can see. bFlying riders do not activate it. */
struct GFGAME_API FGFFerryRoutePerson
{
	FString Key;
	FVector Location = FVector::ZeroVector;
	bool bFlying = false;
};

struct GFGAME_API FGFFerryRules
{
	static FName FareReason();
	static FName PassBurnReason();
	static FName PassGrantReason();

	/** 0 when the inventory holds Tuning.PassItem, otherwise FareCount. */
	static int32 Quote(const FGFInventory& Inventory, const FGFFerryTuning& Tuning);

	static FGFFerryResult CanBoard(
		bool bAuthority,
		bool bInFlight,
		const FGFFerryStop* Stop,
		const FVector& RiderLocation,
		const FGFInventory& Inventory,
		const FGFFerryTuning& Tuning);

	/** Stacks to burn. Empty when Quote is 0. */
	static TArray<FGFItemStack> ChargeFare(const FGFInventory& Inventory, const FGFFerryTuning& Tuning);

	static FGFFerryResult CanBuyPass(
		bool bAuthority,
		bool bInFlight,
		const FGFFerryStop* Stop,
		const FVector& RiderLocation,
		const FGFInventory& Inventory,
		const FGFFerryTuning& Tuning);

	/**
	 * bTxAlreadyApplied skips the afford check: replaying a paid TxId does not charge again.
	 * Boarding checks still apply only when the TxId is new.
	 */
	static FGFFerryFarePlan PlanFare(
		bool bAuthority,
		bool bInFlight,
		bool bTxAlreadyApplied,
		const FGFFerryStop* Stop,
		const FVector& RiderLocation,
		const FGFInventory& Inventory,
		const FGFFerryTuning& Tuning);

	/** PlanFare, then Ledger.Burn when the plan charges. Idempotent by TxId. */
	static FGFFerryResult ApplyFare(
		IGFLedgerAccounts& Accounts,
		FGFTransferLedger& Ledger,
		const FGuid& TxId,
		const FString& Account,
		bool bAuthority,
		bool bInFlight,
		const FGFFerryStop* Stop,
		const FVector& RiderLocation,
		const FGFFerryTuning& Tuning);

	static FGuid DeriveTxId(const FGuid& RequestId, uint32 Salt);

	static FGFFerryPassPlan PlanPass(
		bool bAuthority,
		bool bInFlight,
		bool bBurnAlreadyApplied,
		bool bMintAlreadyApplied,
		const FGuid& RequestId,
		const FGFFerryStop* Stop,
		const FVector& RiderLocation,
		const FGFInventory& Inventory,
		const FGFFerryTuning& Tuning);

	/**
	 * Burns PassPrice of PassMaterialItem, then mints one PassItem.
	 * The mint runs only when the burn applied (including a replay of an applied burn).
	 */
	static FGFFerryResult ApplyPass(
		IGFLedgerAccounts& Accounts,
		FGFTransferLedger& Ledger,
		const FGuid& RequestId,
		const FString& Account,
		bool bAuthority,
		bool bInFlight,
		const FGFFerryStop* Stop,
		const FVector& RiderLocation,
		const FGFFerryTuning& Tuning);

	/**
	 * Straight lerp from A to B plus a sine arc.
	 * Alpha 0 and 1 sit on the endpoints. Alpha 0.5 adds Altitude to Z.
	 */
	static FVector FlightPosition(const FVector& A, const FVector& B, float Alpha, float Altitude);

	/** Horizontal distance. BoardRadius is a disc around the stop, so height does not count. */
	static bool IsWithinBoardRadius(const FVector& RiderLocation, const FVector& StopLocation, float BoardRadius);

	/**
	 * Past the arrival stop, along the route, by BoardRadius / 2.
	 * Z keeps the rider's height above the departure stop.
	 */
	static FVector DropPoint(const FVector& From, const FVector& To, const FVector& RiderLocation, float BoardRadius);

	static const FGFFerryStop* FindStop(const TArray<FGFFerryStop>& Stops, FName Id);
	static const FGFFerryRoute* FindRouteFrom(const TArray<FGFFerryRoute>& Routes, FName StopId);
	static FName OtherStop(const FGFFerryRoute& Route, FName FromStopId);
};

struct GFGAME_API FGFFerryRouteSpawnRules
{
	/** How many uniform samples inside the box one spawn attempt may try. */
	static constexpr int32 SampleAttempts = 64;

	/**
	 * BaseDamage * (1 + MobDamagePerLevel * (MobLevel - 1)).
	 * BaseDamage is the dino's AttackDamage before this scale.
	 */
	static float ScaledAttackDamage(float BaseDamage, int32 MobLevel, float MobDamagePerLevel);

	/** Horizontal distance to the box. 0 when the point is inside. Z is ignored. */
	static float DistanceToBox(const FGFTerritoryHazard& Box, const FVector& Point);

	static bool ContainsHorizontal(const FGFTerritoryHazard& Box, const FVector& Point);

	/** True when a non-flying person is within Margin of the box (inside counts). */
	static bool IsRouteActive(const FGFTerritoryHazard& Box, float Margin, const TArray<FGFFerryRoutePerson>& People);

	static bool CanSpawn(int32 Alive, int32 MaxAlive, bool bActive);

	/**
	 * Outside the box by more than Margin, or idle for IdleDespawnSeconds with nobody near.
	 * "Near" is the same test as IsRouteActive.
	 */
	static bool ShouldDespawn(
		const FGFTerritoryHazard& Box,
		const FVector& MobLocation,
		float Margin,
		float AgeSeconds,
		float IdleDespawnSeconds,
		bool bAnyoneNear);

	/** True only when HazardAt(Point) equals RequiredHazardId. */
	static bool IsLegalSpawnPoint(const FVector& Point, FName RequiredHazardId, TFunctionRef<FName(const FVector&)> HazardAt);

	/**
	 * One point inside the box whose HazardAt equals RequiredHazardId.
	 * Owned land and any other hazard fail the query, so they are not returned.
	 */
	static bool PickSpawnPoint(
		const FGFTerritoryHazard& Box,
		FName RequiredHazardId,
		FRandomStream& Rng,
		TFunctionRef<FName(const FVector&)> HazardAt,
		FVector& OutPoint);

	static FName PickSpecies(const TArray<FName>& Species, FRandomStream& Rng);
};
