#pragma once

// Loads DT_Zones, DT_ZoneAmbience, and DT_Shardstones. ResolveAt ignores Z.
// Faction land is not a CSV row: a faction hit from the territory map beats every row.

#include "CoreMinimal.h"
#include "GFZoneTypes.h"

class FGFTerritoryMap;
struct FGFTerritoryZoneHit;

struct GFGAME_API FGFPOIPoint
{
	FVector Location = FVector::ZeroVector;
	FName Kind;
};

struct GFGAME_API FGFZoneCatalog
{
	TArray<FGFZoneRow> Zones;
	TArray<FGFZoneAmbienceRow> Ambience;
	TArray<FGFShardstoneRow> Stones;

	static FGFZoneCatalog Load();
	/** Process-wide catalog. Reloads if the CSV cache was cleared. */
	static const FGFZoneCatalog& Get();
	static void Invalidate();

	const FGFZoneRow* FindZone(FName Id) const;
	const FGFZoneAmbienceRow* FindAmbience(FName Id) const;
	const FGFShardstoneRow* FindStone(FName Id) const;
	const FGFZoneRow* FindBadlands() const;

	FGFZoneHit ResolveAt(const FVector& Location, const FGFTerritoryZoneHit* Territory) const;
	FGFZoneHit Describe(FName ZoneId, const FGFTerritoryMap* Map) const;
	bool IsKindleSide(const FGFZoneHit& Hit) const;
	FString ToastFor(FName OldId, FName NewId, const FGFTerritoryMap* Map) const;

	/** Stay in CurrentId until the point is more than 2 m outside that shape. */
	FName Advance(FName CurrentId, const FVector& Location, const FGFTerritoryZoneHit* Territory, const FGFTerritoryMap* Map) const;

	int32 DangerTierAt(const FVector& Location, const FGFTerritoryZoneHit* Territory) const;
	bool IsPvPAllowedAt(const FVector& Location, const FGFTerritoryZoneHit* Territory) const;

	bool IsUnlocked(const FGFShardstoneRow& Stone, const TArray<FName>& Saved) const;
	bool IsNearAnyUnlocked(const FVector& Location, const TArray<FName>& Saved) const;
	TArray<FGFPorchDisc> PorchDiscs() const;

	/** Seeded scatter. Same seed, same points. None inside the Kindle Ring or a porch. */
	TArray<FGFPOIPoint> ScatterPOI(int32 Seed) const;

	/** Rows to resolve, for the priority tests. Faction is applied by ResolveAt, not here. */
	static FGFZoneHit ResolveRows(const TArray<FGFZoneRow>& Rows, const FVector& Location, const FGFTerritoryZoneHit* Territory);
};
