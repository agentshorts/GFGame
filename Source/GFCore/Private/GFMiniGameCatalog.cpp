#include "GFMiniGameCatalog.h"

namespace
{
	FGFMiniGameEntry MapMode(
		const TCHAR* Id,
		const TCHAR* Label,
		const TCHAR* Map,
		const TCHAR* ArtObject,
		const FLinearColor& Color,
		bool bExpectArt,
		int32 RingIndex,
		const TCHAR* PlacedClass)
	{
		FGFMiniGameEntry Entry;
		Entry.Id = Id;
		Entry.Label = Label;
		Entry.Map = Map;
		Entry.ArtObject = ArtObject;
		Entry.PlaceholderColor = Color;
		Entry.bExpectArt = bExpectArt;
		Entry.RingIndex = RingIndex;
		Entry.PlacedClass = PlacedClass ? PlacedClass : TEXT("");
		return Entry;
	}

	FGFMiniGameEntry WithSign(FGFMiniGameEntry Entry, const TCHAR* SignObject)
	{
		Entry.SignObject = SignObject;
		return Entry;
	}

	FGFMiniGameEntry SimMode(
		const TCHAR* Id,
		const TCHAR* Label,
		const TCHAR* ArtObject,
		const FLinearColor& Color,
		int32 RingIndex,
		int32 PadIndex,
		const TCHAR* DirectorClass)
	{
		FGFMiniGameEntry Entry;
		Entry.Id = Id;
		Entry.Label = Label;
		Entry.SimMode = Id;
		Entry.ArtObject = ArtObject ? ArtObject : TEXT("");
		Entry.PlaceholderColor = Color;
		Entry.bExpectArt = !Entry.ArtObject.IsEmpty();
		Entry.RingIndex = RingIndex;
		Entry.bHasPad = true;
		const float Stride = FGFMiniGameCatalog::PadHalfCm * 2.f + FGFMiniGameCatalog::PadGapCm;
		Entry.PadOffset = FVector(0.f, FGFMiniGameCatalog::FirstPadYCm + PadIndex * Stride, 0.f);
		Entry.PadHalfExtent = FVector2D(FGFMiniGameCatalog::PadHalfCm, FGFMiniGameCatalog::PadHalfCm);
		Entry.DirectorClass = DirectorClass;
		return Entry;
	}
}

const TArray<FGFMiniGameEntry>& FGFMiniGameCatalog::All()
{
	// Ring order, starting east and walking counter-clockwise. Labels are the signs.
	// Beat Hop is the rhythm map. Depths is the platformer sim. They do not share a name.
	static const TArray<FGFMiniGameEntry> Entries = {
		MapMode(TEXT("Taming"), TEXT("Valley"), TEXT("/Game/Maps/L_Valley"),
			TEXT("/Game/Art/Gates/SM_Gate_Valley.SM_Gate_Valley"),
			FLinearColor(0.45f, 0.62f, 0.40f), true, 0, nullptr),
		WithSign(MapMode(TEXT("Fashion"), TEXT("Runway"), TEXT("/Game/Maps/L_Runway"),
			TEXT("/Game/Art/Gates/SM_Gate_Runway.SM_Gate_Runway"),
			FLinearColor(0.85f, 0.45f, 0.62f), true, 1,
			TEXT("/Script/GFFashion.GFRunwayStage")),
			TEXT("/Game/Art/Gates/Signs/SM_Sign_Runway.SM_Sign_Runway")),
		WithSign(MapMode(TEXT("Moba"), TEXT("Crystal Clash"), TEXT("/Game/Maps/L_Moba"),
			TEXT("/Game/Art/Gates/SM_Gate_Arena.SM_Gate_Arena"),
			FLinearColor(0.75f, 0.22f, 0.18f), true, 2, nullptr),
			TEXT("/Game/Art/Gates/Signs/SM_Sign_Moba.SM_Sign_Moba")),
		WithSign(MapMode(TEXT("BeatHop"), TEXT("Beat Hop"), TEXT("/Game/Maps/L_Beat"),
			TEXT("/Game/Art/Gates/SM_Gate_Beat.SM_Gate_Beat"),
			FLinearColor(0.72f, 0.52f, 1.00f), true, 3,
			TEXT("/Script/GFRhythm.GFBeatHopCourse")),
			TEXT("/Game/Art/Gates/Signs/SM_Sign_BeatHop.SM_Sign_BeatHop")),
		SimMode(TEXT("Arena"), TEXT("Arena"), FGFMiniGameCatalog::SimGateObject,
			FLinearColor(0.80f, 0.65f, 0.30f), 4, 0,
			TEXT("/Script/GFArena.GFArenaDirector")),
		SimMode(TEXT("Depths"), TEXT("Depths"), FGFMiniGameCatalog::SimGateObject,
			FLinearColor(0.35f, 0.25f, 0.55f), 5, 1,
			TEXT("/Script/GFDepths.GFDepthsDirector")),
		SimMode(TEXT("Mine"), TEXT("Pebble Mine"), FGFMiniGameCatalog::SimGateObject,
			FLinearColor(0.55f, 0.52f, 0.45f), 6, 2,
			TEXT("/Script/GFMine.GFMineDirector")),
		SimMode(TEXT("Lab"), TEXT("Overtime"), FGFMiniGameCatalog::SimGateObject,
			FLinearColor(0.30f, 0.70f, 0.72f), 7, 3,
			TEXT("/Script/GFLab.GFLabDirector")),
	};
	return Entries;
}

const FGFMiniGameEntry* FGFMiniGameCatalog::Find(FName Id)
{
	for (const FGFMiniGameEntry& Entry : All())
	{
		if (Entry.Id == Id)
		{
			return &Entry;
		}
	}
	return nullptr;
}

FName FGFMiniGameCatalog::HubMap()
{
	return TEXT("/Game/Maps/L_Hub");
}

bool FGFMiniGameCatalog::WorldIs(const FString& WorldPackage, FName Map)
{
	if (Map.IsNone() || WorldPackage.IsEmpty())
	{
		return false;
	}
	const FString Want = Map.ToString();
	if (WorldPackage.Equals(Want, ESearchCase::IgnoreCase))
	{
		return true;
	}
	FString Short = Want;
	int32 Slash = INDEX_NONE;
	if (Want.FindLastChar(TEXT('/'), Slash))
	{
		Short = Want.Mid(Slash + 1);
	}
	if (Short.IsEmpty())
	{
		return false;
	}
	// /Game/Maps/L_Hub and the PIE form /Game/Maps/UEDPIE_0_L_Hub.
	return WorldPackage.EndsWith(TEXT("/") + Short, ESearchCase::IgnoreCase)
		|| WorldPackage.EndsWith(TEXT("_") + Short, ESearchCase::IgnoreCase);
}

FVector FGFMiniGameCatalog::GateLocation(const FVector& RimeCenter, int32 RingIndex, int32 RingCount)
{
	if (RingCount <= 0)
	{
		return RimeCenter;
	}
	const float Angle = (static_cast<float>(RingIndex) / static_cast<float>(RingCount)) * 2.f * PI;
	return RimeCenter + FVector(FMath::Cos(Angle) * GateRingRadiusCm, FMath::Sin(Angle) * GateRingRadiusCm, 0.f);
}

FRotator FGFMiniGameCatalog::GateFacing(const FVector& GateLocation, const FVector& RimeCenter)
{
	return (RimeCenter - GateLocation).GetSafeNormal2D().Rotation();
}

FVector FGFMiniGameCatalog::PadOrigin(const FGFMiniGameEntry& Entry, const FVector& RimeCenter)
{
	return Entry.bHasPad ? RimeCenter + Entry.PadOffset : RimeCenter;
}

FVector FGFMiniGameCatalog::SimStand(const FGFMiniGameEntry& Entry, const FVector& RimeCenter)
{
	return PadOrigin(Entry, RimeCenter) + FVector(SimStandForwardCm, 0.f, SimStandUpCm);
}

FVector FGFMiniGameCatalog::DefaultSpawn(FName SimMode)
{
	const FGFMiniGameEntry* Entry = Find(SimMode);
	if (!Entry || !Entry->bHasPad)
	{
		return FVector::ZeroVector;
	}
	return PadOrigin(*Entry, FVector::ZeroVector);
}

FString FGFMiniGameCatalog::ArtPackage(const FGFMiniGameEntry& Entry)
{
	FString Path = Entry.ArtObject;
	int32 Dot = INDEX_NONE;
	if (Path.FindChar(TEXT('.'), Dot))
	{
		Path.LeftInline(Dot);
	}
	return Path;
}
