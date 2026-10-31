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
	/** StartFlight did not begin. A fare that was burned for this request is minted back. */
	static const FName FlightFailed;
	/** Both stops exist and neither route joins them. */
	static const FName NoRoute;
};

/**
 * The ferry is a route craft. It parks on the perch of a stop and does not follow a player.
 * ParkedGoal is the perch, whatever the player does.
 */
struct GFGAME_API FGFFerryFollow
{
	static bool WantsToFollow() { return false; }
	static FVector ParkedGoal(const FVector& Perch) { return Perch; }
};

/**
 * Ripwing ferry clip timing and socket offsets. Frame numbers are at 30 fps.
 * The parked idle stays Ripwing.Perch_Idle. Transit uses the ferry clips, not Glide_Loop.
 */
struct GFGAME_API FGFFerryArt
{
	static constexpr float FramesPerSecond = 30.f;
	static constexpr int32 TakeoffFrames = 40;
	static constexpr int32 LeavePerchFrame = 10;
	static constexpr int32 GrabBasketFrame = 22;
	static constexpr int32 LandFrames = 48;
	static constexpr int32 BasketDownFrame = 16;
	static constexpr int32 ReleaseFrame = 22;
	static constexpr float ParkYawDegrees = -90.f;
	/** Control-point offset past the chord midpoint, toward world +Y. The apex sits halfway, 800 cm off the chord. */
	static constexpr float RouteBulgeCm = 1600.f;

	static FVector PerchPointLocal() { return FVector(-95.f, 0.f, 262.5f); }
	static FVector BasketSpotLocal() { return FVector(0.f, 0.f, 12.5f); }
	static FVector GripLocal() { return FVector(0.f, 0.f, 193.75f); }
	static FVector SeatLocal() { return FVector(-25.f, 0.f, 12.5f); }

	static FName PerchAnim() { return TEXT("Ripwing.Perch_Idle"); }
	static FName TakeoffAnim() { return TEXT("Ripwing.Ferry_Takeoff"); }
	static FName CarryAnim() { return TEXT("Ripwing.Ferry_Carry_Fly"); }
	static FName LandAnim() { return TEXT("Ripwing.Ferry_Land"); }
	static FName SeatAnim() { return TEXT("Player.Emote_Sit"); }

	/** Takeoff until the takeoff clip ends, landing for the last land clip, carry in between. */
	static FName FlightPhase(float Elapsed, float Duration)
	{
		const float TakeoffEnd = FrameSeconds(TakeoffFrames);
		const float LandLen = FrameSeconds(LandFrames);
		const float LandStart = FMath::Max(TakeoffEnd, Duration - LandLen);
		if (Elapsed >= LandStart)
		{
			return LandAnim();
		}
		if (Elapsed >= FMath::Min(TakeoffEnd, LandStart))
		{
			return CarryAnim();
		}
		return TakeoffAnim();
	}

	/** Seconds of carry between the takeoff clip and the landing clip. */
	static float CarrySeconds(float Duration)
	{
		return FMath::Max(0.f, Duration - FrameSeconds(TakeoffFrames) - FrameSeconds(LandFrames));
	}

	static FName GripSocket() { return TEXT("Grip"); }
	static FName SeatSocket() { return TEXT("Seat"); }
	static FName FerryGripSocket() { return TEXT("FerryGrip"); }

	static float FrameSeconds(int32 Frame) { return static_cast<float>(Frame) / FramesPerSecond; }
	static FRotator ParkRotation() { return FRotator(0.f, ParkYawDegrees, 0.f); }
	static FTransform PerchTransform(const FVector& StopLocation) { return FTransform(ParkRotation(), StopLocation); }
	static FVector PerchWorld(const FVector& StopLocation) { return PerchTransform(StopLocation).TransformPosition(PerchPointLocal()); }
	static FVector BasketSpotWorld(const FVector& StopLocation) { return PerchTransform(StopLocation).TransformPosition(BasketSpotLocal()); }
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
	/** Highest in-party companion level. 1 when that player has nobody in the party. */
	int32 PartyLevel = 1;
};

struct GFGAME_API FGFFerryRules
{
	static FName FareReason();
	static FName FareRefundReason();
	static FName PassBurnReason();
	static FName PassGrantReason();
	static FName PassRollbackReason();

	/** The fare TxId is the request id. Invalid requests stay invalid so a caller cannot fall back to a random id. */
	static FGuid FareTxId(const FGuid& RequestId);
	static FGuid FareRefundTx(const FGuid& RequestId);
	static FGuid PassRollbackTx(const FGuid& RequestId);

	/** 0 when the inventory holds Tuning.PassItem, otherwise FareCount. */
	static int32 Quote(const FGFInventory& Inventory, const FGFFerryTuning& Tuning);

	/** A tier-1 building block. Stone and other materials are not blocks. Fabric is a block, but not tier 1. */
	static bool IsBasicBlock(const FGFItemRow& Row);

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
		bool bRolledBack,
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
	 * Quadratic Bezier from A to B. Alpha 0 and 1 are the endpoints.
	 * The control point is the chord midpoint, 1600 cm toward world +Y, with Z = midpoint Z + 2 * Altitude,
	 * so alpha 0.5 is Altitude above the chord and 800 cm off it. Both directions bulge toward +Y.
	 */
	static FVector FlightControl(const FVector& A, const FVector& B, float Altitude);
	static FVector FlightPosition(const FVector& A, const FVector& B, float Alpha, float Altitude);
	static FVector FlightTangent(const FVector& A, const FVector& B, float Alpha, float Altitude);

	/** Horizontal distance. BoardRadius is a disc around the stop, so height does not count. */
	static bool IsWithinBoardRadius(const FVector& RiderLocation, const FVector& StopLocation, float BoardRadius);

	/**
	 * Past the arrival stop, along the route, by BoardRadius / 2.
	 * Z keeps the rider's height above the departure stop.
	 */
	static FVector DropPoint(const FVector& From, const FVector& To, const FVector& RiderLocation, float BoardRadius);

	/** Geometry for the spawn rules. The id is the box id; the territory hazard id stays on the mob box. */
	static FGFTerritoryHazard BoxBounds(const FGFFerryMobBox& Box);

	static const FGFFerryStop* FindStop(const TArray<FGFFerryStop>& Stops, FName Id);
	/** First route that touches StopId. A stop on several routes still resolves to that first one. */
	static const FGFFerryRoute* FindRouteFrom(const TArray<FGFFerryRoute>& Routes, FName StopId);
	/** The route whose ends are StopA and StopB, either order. Null when they are the same or unlinked. */
	static const FGFFerryRoute* FindRouteBetween(const TArray<FGFFerryRoute>& Routes, FName StopA, FName StopB);
	static FName OtherStop(const FGFFerryRoute& Route, FName FromStopId);
	/** Other stops that share a route with FromStopId, in stop-array order. */
	static void CollectDestinations(const TArray<FGFFerryStop>& Stops, const TArray<FGFFerryRoute>& Routes, FName FromStopId, TArray<FName>& OutStopIds);
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

	/** Highest PartyLevel among non-flying people within Margin. 1 when nobody qualifies. */
	static int32 RoutePartyLevel(const FGFTerritoryHazard& Box, float Margin, const TArray<FGFFerryRoutePerson>& People);

	/** PartyLevel + LevelBonus, and at least 1. */
	static int32 MobLevelForParty(int32 PartyLevel, int32 LevelBonus);

	/**
	 * Outside the box by more than Margin, or idle for IdleDespawnSeconds with nobody near.
	 * bAnyoneNear uses IsRouteActive. The leash passes LeashMargin; idle passes DespawnRange.
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
