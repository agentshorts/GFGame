#pragma once

// Shardhop rules. No world required: the controller feeds a view and applies the result.
// Hop from within 6 m of any unlocked stone, to an unlocked stone, after a 3 s channel.

#include "CoreMinimal.h"
#include "GFZoneTypes.h"

class AGameStateBase;
class APlayerState;
struct FGFZoneCatalog;

enum class EGFShardhopResult : uint8
{
	Started,
	Running,
	Completed,
	Cancelled,
	NoStone,
	Locked,
	TooFar,
	Villain,
	RecentPvP,
	Busy
};

struct GFGAME_API FGFShardhopChannel
{
	bool bActive = false;
	FName DestId;
	double StartedAt = 0.0;
	int32 DamageSerialAtStart = 0;
};

/** One look at the traveler. The caller fills this from the pawn and player state. */
struct GFGAME_API FGFShardhopView
{
	FVector Location = FVector::ZeroVector;
	bool bVillainMode = false;
	double NowSeconds = 0.0;
	/** Negative means this player has not taken PvP-zone damage. */
	double LastPvPDamageSeconds = -100000.0;
	int32 DamageSerial = 0;
	TArray<FName> Unlocked;
};

struct GFGAME_API FGFShardhop
{
	static EGFShardhopResult TryStart(const FGFZoneCatalog& Catalog, const FGFShardhopView& View, FName DestId, FGFShardhopChannel& Channel);
	static EGFShardhopResult Tick(const FGFZoneCatalog& Catalog, const FGFShardhopView& View, FGFShardhopChannel& Channel);

	static FVector ArrivalPoint(const FGFShardstoneRow& Stone, int32 Slot);
	static int32 SlotOf(const AGameStateBase* GameState, const APlayerState* Player);

	/** Tamed, owned by this player, and within 20 m of where the hop started. */
	static bool ShouldBringCompanion(bool bTamed, const FString& OwnerKey, const FString& PlayerKey, float DistanceCm);

	/**
	 * Unlocked stone you are not already standing on, closest to the view yaw.
	 * None when every unlocked stone is within the hop radius.
	 */
	static FName PickDestination(const FGFZoneCatalog& Catalog, const TArray<FName>& Saved, const FVector& From, float ViewYawDeg);

	static FString DenyText(EGFShardhopResult Result);
};
