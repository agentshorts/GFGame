#pragma once

// Curve, optional neutral disc, and the zone a point falls in. Faction ids and anchor positions live in
// Config/DefaultGame.ini (UGFTerritorySubsystem::FactionLayout), not in code.

#include "CoreMinimal.h"
#include "GFTerritoryTypes.generated.h"

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

	/**
	 * 0 = no cap. A positive cap never shrinks land below the one-member radius (FGFSafeRegion's rule).
	 * Default 2800 = 1200 + 400 x 4, so land keeps growing up to a full 4-member faction.
	 * Tune with Curve in DefaultGame.ini under [/Script/GFGame.GFTerritorySubsystem].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory", meta = (ClampMin = "0"))
	float MaxRadius = 2800.f;
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
};

/** One faction anchor. Members is the head count. A set RadiusByMembers table replaces the shared curve. */
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

	/**
	 * Radius at each effective member count. Index 0 is 0 members.
	 * Empty means this faction uses the shared curve. A count past the last index uses the last entry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	TArray<float> RadiusByMembers;
};

/**
 * Axis-aligned hazard box. Z is ignored. Kind is a config name, not an enum.
 * BetweenA and BetweenB record which anchors the box sits between. The query does not read them.
 */
USTRUCT(BlueprintType)
struct GFGAME_API FGFTerritoryHazard
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FName Kind;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FVector Center = FVector::ZeroVector;

	/** Horizontal half-size. X along world X, Y along world Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FVector2D HalfExtent = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FName BetweenA;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FName BetweenB;
};

/** A hazard box hit. bHazard is false when the point is not inside a wild hazard. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFTerritoryHazardHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Territory")
	bool bHazard = false;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Territory")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Territory")
	FName Kind;
};

/**
 * How many members may grow one faction's land. This is AGFGameState::MaxPerFaction (0 = no clamp).
 * The session's MaxPlayers is not read: the live game session exists only on the server, so a client
 * read is 0 and would disagree with the server.
 */
struct GFGAME_API FGFTerritoryLimits
{
	/** AGFGameState::MaxPerFaction. 0 when there is no game state, or when the property is 0 (no clamp). */
	static int32 FactionSeatCap(const AGFGameState* GS);
};
