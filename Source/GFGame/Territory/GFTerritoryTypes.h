#pragma once

// Faction territory values. Membership itself stays on AGFPlayerState / AGFGameState; this file is only the
// curve, the optional neutral disc, and the zone a point falls in.

#include "CoreMinimal.h"
#include "GFTerritoryTypes.generated.h"

class UWorld;
class AGFGameState;

/** What GetZoneAt reports. Faction land wins over the neutral disc; everything else is wild. */
UENUM(BlueprintType)
enum class EGFTerritoryZone : uint8
{
	Faction,
	Neutral,
	Wild
};

/** Zone at a point, plus the owning faction when Zone is Faction. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFTerritoryZoneHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Territory")
	EGFTerritoryZone Zone = EGFTerritoryZone::Wild;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Territory")
	FName FactionId;
};

/**
 * Radius for a faction with N members (N > 0) is BaseRadius + RadiusPerMember * N, then clamped.
 * The clamp is FGFSafeRegion::Radius, so the cap never shrinks the one-member radius.
 * MaxRadius <= 0 means no cap. N == 0 owns nothing (radius 0), not the base.
 */
USTRUCT(BlueprintType)
struct GFGAME_API FGFTerritoryCurve
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory", meta = (ClampMin = "0"))
	float BaseRadius = 1200.f;

	/** Added for every member, including the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	float RadiusPerMember = 400.f;

	/** 0 = no cap. A positive cap never shrinks land below the one-member radius (FGFSafeRegion's rule). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory", meta = (ClampMin = "0"))
	float MaxRadius = 3200.f;
};

/** Optional disc. Points inside it that no faction owns are Neutral. Off by default. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFNeutralZone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory", meta = (ClampMin = "0"))
	float Radius = 800.f;

	bool Contains(const FVector& Location) const
	{
		return bEnabled && Radius > 0.f && FVector::DistSquared2D(Center, Location) <= FMath::Square(Radius);
	}
};

/** One faction anchor. Members is the head count; the curve may clamp how much of that count grows the radius. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFTerritoryFaction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory", meta = (ClampMin = "0"))
	int32 Members = 0;
};

/** Faction ids this mode adds. Other ids already on a player state still work if an anchor uses them. */
struct GFGAME_API FGFTerritoryFactions
{
	static const FName Cinderwrights;
	static const FName Verdant;
	static const FName Rime;
};

/**
 * Who IsSafeFor is answering. Built from AGFPlayerState (faction, bIsHost) or from AGFDino
 * (wild, or tamed and then the owner's faction). bHost is recorded and does not grant land.
 */
struct GFGAME_API FGFTerritorySubject
{
	FName FactionId;
	bool bHost = false;
	bool bWildCreature = false;

	static FGFTerritorySubject Player(FName InFactionId, bool bInHost = false)
	{
		FGFTerritorySubject S;
		S.FactionId = InFactionId;
		S.bHost = bInHost;
		return S;
	}

	static FGFTerritorySubject TamedDino(FName OwnerFactionId, bool bOwnerIsHost = false)
	{
		FGFTerritorySubject S;
		S.FactionId = OwnerFactionId;
		S.bHost = bOwnerIsHost;
		return S;
	}

	static FGFTerritorySubject WildDino()
	{
		FGFTerritorySubject S;
		S.bWildCreature = true;
		return S;
	}
};

/**
 * How many members may grow one faction's land. Reads the session player cap and the faction seat cap.
 * Never a baked-in player total: 0 from a source means that source is not set.
 */
struct GFGAME_API FGFTerritoryLimits
{
	/** Tighter of the two caps. 0 if both sources are missing (the curve does not clamp). */
	static int32 CapMembers(int32 SessionMaxPlayers, int32 MaxPerFaction);

	/** AGameSession::MaxPlayers on the world's game mode. 0 when there is no session. */
	static int32 SessionMaxPlayers(const UWorld* World);

	/** AGFGameState::MaxPerFaction. 0 when there is no game state. */
	static int32 FactionSeatCap(const AGFGameState* GS);
};
