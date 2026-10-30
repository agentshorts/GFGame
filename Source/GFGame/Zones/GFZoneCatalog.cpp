#include "GFZoneCatalog.h"

#include "GFDataTables.h"
#include "GFTerritoryMap.h"
#include "GFTerritoryTypes.h"
#include "GFZoneLayout.h"
#include "GFZoneTags.h"
#include "GameplayTagContainer.h"

#include <type_traits>

namespace
{
	FGameplayTag TagFromRow(const FGFZoneRow& Row)
	{
		if (Row.Tag.IsValid())
		{
			return Row.Tag;
		}
		return FGameplayTag::RequestGameplayTag(Row.Id, false);
	}

	FGFZoneHit HitFromRow(const FGFZoneRow& Row)
	{
		FGFZoneHit Hit;
		Hit.ZoneId = Row.Id;
		Hit.Tag = TagFromRow(Row);
		Hit.DangerTier = Row.DangerTier;
		Hit.bPvPAllowed = Row.bPvPAllowed;
		Hit.RegionId = Row.RegionId;
		Hit.AmbienceId = Row.AmbienceId;
		Hit.DisplayName = Row.DisplayName;
		Hit.Priority = Row.Priority;
		return Hit;
	}

	bool SplitFaction(FName ZoneId, FName& OutFaction)
	{
		const FString Text = ZoneId.ToString();
		if (!Text.StartsWith(TEXT("Faction.")))
		{
			return false;
		}
		OutFaction = FName(*Text.RightChop(8));
		return !OutFaction.IsNone();
	}

	FGFZoneHit FactionHit(FName FactionId)
	{
		FGFZoneHit Hit;
		Hit.ZoneId = FName(*FString::Printf(TEXT("Faction.%s"), *FactionId.ToString()));
		Hit.Tag = TAG_Zone_Faction.GetTag();
		Hit.DangerTier = 0;
		Hit.bPvPAllowed = false;
		Hit.AmbienceId = FName(TEXT("Ambience.Kindle"));
		Hit.DisplayName = FactionId.ToString();
		Hit.Priority = 100;
		return Hit;
	}

	template <typename RowType>
	void Read(const TCHAR* Csv, TArray<RowType>& Out)
	{
		Out.Reset();
		const UDataTable* Table = FGFDataTables::Get<RowType>(Csv);
		if (!Table)
		{
			return;
		}
		Table->ForeachRow<RowType>(Csv, [&Out](const FName& Name, const RowType& Row)
		{
			RowType Copy = Row;
			if constexpr (std::is_same_v<RowType, FGFZoneRow> || std::is_same_v<RowType, FGFZoneAmbienceRow> || std::is_same_v<RowType, FGFShardstoneRow>)
			{
				if (Copy.Id.IsNone())
				{
					Copy.Id = Name;
				}
			}
			Out.Add(Copy);
		});
	}

	float FactionOutside(const FGFTerritoryMap* Map, FName FactionId, const FVector& Location)
	{
		if (!Map)
		{
			return TNumericLimits<float>::Max();
		}
		const FGFTerritoryFaction* Faction = Map->FindFaction(FactionId);
		if (!Faction)
		{
			return TNumericLimits<float>::Max();
		}
		const float Radius = Map->RadiusOf(*Faction);
		return FMath::Max(0.f, FVector::Dist2D(Faction->Center, Location) - Radius);
	}
}

FGFZoneCatalog FGFZoneCatalog::Load()
{
	FGFZoneTags::Anchor();
	FGFZoneCatalog Catalog;
	Read<FGFZoneRow>(TEXT("DT_Zones"), Catalog.Zones);
	Read<FGFZoneAmbienceRow>(TEXT("DT_ZoneAmbience"), Catalog.Ambience);
	Read<FGFShardstoneRow>(TEXT("DT_Shardstones"), Catalog.Stones);
	for (FGFZoneRow& Row : Catalog.Zones)
	{
		if (!Row.Tag.IsValid())
		{
			Row.Tag = FGameplayTag::RequestGameplayTag(Row.Id, false);
		}
	}
	return Catalog;
}

const FGFZoneCatalog& FGFZoneCatalog::Get()
{
	static FGFZoneCatalog Cached;
	static bool bReady = false;
	if (!bReady)
	{
		Cached = Load();
		bReady = true;
	}
	return Cached;
}

void FGFZoneCatalog::Invalidate()
{
	// Get() caches forever. Tests that rewrite CSVs call FGFDataTables::ResetCache and then Load() directly.
}

const FGFZoneRow* FGFZoneCatalog::FindZone(FName Id) const
{
	return Zones.FindByPredicate([Id](const FGFZoneRow& Row) { return Row.Id == Id; });
}

const FGFZoneAmbienceRow* FGFZoneCatalog::FindAmbience(FName Id) const
{
	return Ambience.FindByPredicate([Id](const FGFZoneAmbienceRow& Row) { return Row.Id == Id; });
}

const FGFShardstoneRow* FGFZoneCatalog::FindStone(FName Id) const
{
	return Stones.FindByPredicate([Id](const FGFShardstoneRow& Row) { return Row.Id == Id; });
}

const FGFZoneRow* FGFZoneCatalog::FindBadlands() const
{
	return FindZone(FName(TEXT("Zone.Badlands")));
}

FGFZoneHit FGFZoneCatalog::ResolveRows(const TArray<FGFZoneRow>& Rows, const FVector& Location, const FGFTerritoryZoneHit* Territory)
{
	if (Territory && Territory->Zone == EGFTerritoryZone::Faction && !Territory->FactionId.IsNone())
	{
		return FactionHit(Territory->FactionId);
	}
	const FGFZoneRow* Best = nullptr;
	for (const FGFZoneRow& Row : Rows)
	{
		if (!Row.ContainsXY(Location))
		{
			continue;
		}
		if (!Best || Row.Priority > Best->Priority)
		{
			Best = &Row;
		}
	}
	if (Best)
	{
		return HitFromRow(*Best);
	}
	const FGFZoneRow* Badlands = Rows.FindByPredicate([](const FGFZoneRow& Row) { return Row.Id == FName(TEXT("Zone.Badlands")); });
	if (Badlands)
	{
		return HitFromRow(*Badlands);
	}
	FGFZoneHit Fallback;
	Fallback.ZoneId = FName(TEXT("Zone.Badlands"));
	Fallback.Tag = TAG_Zone_Badlands.GetTag();
	Fallback.DangerTier = 2;
	Fallback.bPvPAllowed = true;
	Fallback.DisplayName = TEXT("Badlands");
	Fallback.AmbienceId = FName(TEXT("Ambience.Badlands"));
	Fallback.Priority = 10;
	return Fallback;
}

FGFZoneHit FGFZoneCatalog::ResolveAt(const FVector& Location, const FGFTerritoryZoneHit* Territory) const
{
	return ResolveRows(Zones, Location, Territory);
}

FGFZoneHit FGFZoneCatalog::Describe(FName ZoneId, const FGFTerritoryMap* Map) const
{
	FName FactionId;
	if (SplitFaction(ZoneId, FactionId))
	{
		return FactionHit(FactionId);
	}
	if (const FGFZoneRow* Row = FindZone(ZoneId))
	{
		return HitFromRow(*Row);
	}
	return ResolveAt(FVector::ZeroVector, nullptr);
}

bool FGFZoneCatalog::IsKindleSide(const FGFZoneHit& Hit) const
{
	if (Hit.ZoneId == FName(TEXT("Zone.Kindle")))
	{
		return true;
	}
	return Hit.ZoneId.ToString().StartsWith(TEXT("Faction."));
}

FString FGFZoneCatalog::ToastFor(FName OldId, FName NewId, const FGFTerritoryMap* Map) const
{
	if (OldId.IsNone() || OldId == NewId)
	{
		return FString();
	}
	const bool bWas = IsKindleSide(Describe(OldId, Map));
	const bool bNow = IsKindleSide(Describe(NewId, Map));
	if (bWas && !bNow)
	{
		return GFZoneCopy::LeavingKindle();
	}
	if (!bWas && bNow)
	{
		return GFZoneCopy::WelcomeHome();
	}
	return FString();
}

FName FGFZoneCatalog::Advance(FName CurrentId, const FVector& Location, const FGFTerritoryZoneHit* Territory, const FGFTerritoryMap* Map) const
{
	const FGFZoneHit Resolved = ResolveAt(Location, Territory);
	if (CurrentId.IsNone() || CurrentId == Resolved.ZoneId)
	{
		return Resolved.ZoneId;
	}
	float Outside = TNumericLimits<float>::Max();
	FName FactionId;
	if (SplitFaction(CurrentId, FactionId))
	{
		Outside = FactionOutside(Map, FactionId, Location);
	}
	else if (const FGFZoneRow* Row = FindZone(CurrentId))
	{
		Outside = Row->DistanceOutside(Location);
	}
	else
	{
		return Resolved.ZoneId;
	}
	// Still inside the current shape: a higher-priority zone (faction inside the ring) wins at once.
	// Just outside: stay until the point is more than 2 m past the edge.
	if (Outside > 0.f && Outside <= FGFZoneConstants::HysteresisCm)
	{
		return CurrentId;
	}
	return Resolved.ZoneId;
}

int32 FGFZoneCatalog::DangerTierAt(const FVector& Location, const FGFTerritoryZoneHit* Territory) const
{
	return ResolveAt(Location, Territory).DangerTier;
}

bool FGFZoneCatalog::IsPvPAllowedAt(const FVector& Location, const FGFTerritoryZoneHit* Territory) const
{
	return ResolveAt(Location, Territory).bPvPAllowed;
}

bool FGFZoneCatalog::IsUnlocked(const FGFShardstoneRow& Stone, const TArray<FName>& Saved) const
{
	return Stone.bStartsUnlocked || Saved.Contains(Stone.Id);
}

bool FGFZoneCatalog::IsNearAnyUnlocked(const FVector& Location, const TArray<FName>& Saved) const
{
	for (const FGFShardstoneRow& Stone : Stones)
	{
		if (!IsUnlocked(Stone, Saved))
		{
			continue;
		}
		if (FVector::Dist2D(Stone.Location, Location) <= FGFZoneConstants::HopRadiusCm)
		{
			return true;
		}
	}
	return false;
}

TArray<FGFPorchDisc> FGFZoneCatalog::PorchDiscs() const
{
	TArray<FGFPorchDisc> Out;
	for (const FGFZoneRow& Row : Zones)
	{
		const FString Id = Row.Id.ToString();
		if (!Id.StartsWith(TEXT("Zone.Porch.")) || Id.EndsWith(TEXT(".Core")))
		{
			continue;
		}
		FGFPorchDisc Disc;
		Disc.Id = Row.Id;
		Disc.Center = FVector2D(Row.Center.X, Row.Center.Y);
		Disc.RadiusCm = Row.Radius;
		if (const FGFRegionSlot* Slot = FGFZoneLayout::FindRegion(Row.RegionId))
		{
			Disc.Color = Slot->Color;
		}
		Out.Add(Disc);
	}
	return Out;
}

TArray<FGFPOIPoint> FGFZoneCatalog::ScatterPOI(int32 Seed) const
{
	FRandomStream Rng(Seed);
	const float Step = 13000.f;
	const float Jitter = 400.f;
	static const FName Kinds[] = { FName(TEXT("Crystal")), FName(TEXT("Hull")), FName(TEXT("Nest")) };
	TArray<FGFPOIPoint> Out;
	for (float X = -FGFZoneConstants::RimRadiusCm; X <= FGFZoneConstants::RimRadiusCm; X += Step)
	{
		for (float Y = -FGFZoneConstants::RimRadiusCm; Y <= FGFZoneConstants::RimRadiusCm; Y += Step)
		{
			const FVector Point(X + Rng.FRandRange(-Jitter, Jitter), Y + Rng.FRandRange(-Jitter, Jitter), 0.f);
			const int32 Kind = Rng.RandRange(0, 2);
			const float Rim = FVector2D(Point.X, Point.Y).Size();
			if (Rim > FGFZoneConstants::RimRadiusCm || Rim <= FGFZoneConstants::KindleRadiusCm)
			{
				continue;
			}
			bool bPorch = false;
			for (const FGFPorchDisc& Disc : PorchDiscs())
			{
				if (FVector2D::Distance(Disc.Center, FVector2D(Point.X, Point.Y)) <= Disc.RadiusCm)
				{
					bPorch = true;
					break;
				}
			}
			if (bPorch)
			{
				continue;
			}
			FGFPOIPoint Poi;
			Poi.Location = Point;
			Poi.Kind = Kinds[Kind];
			Out.Add(Poi);
		}
	}
	return Out;
}
