#include "GFOverworldBuild.h"
#include "GFZoneTypes.h"

namespace
{
	// Ferry stops stay on solid ground even if a faction radius shrinks past them.
	const FVector2D GFFerryStops[] = {
		FVector2D(1200.f, 600.f),
		FVector2D(6000.f, 600.f),
	};

	float PeakRadius(const FGFTerritoryCurve& Curve, const FGFTerritoryFaction& Faction)
	{
		FGFTerritoryMap Map;
		Map.Curve = Curve;
		Map.MaxMembers = 0;
		FGFTerritoryFaction Copy = Faction;
		Copy.Members = Faction.RadiusByMembers.Num() > 0 ? Faction.RadiusByMembers.Num() - 1 : 4;
		return Map.RadiusOf(Copy);
	}

	bool InDisc(const FVector& Center, float Radius, const FVector2D& XY)
	{
		if (Radius <= 0.f)
		{
			return false;
		}
		const float DX = XY.X - Center.X;
		const float DY = XY.Y - Center.Y;
		return DX * DX + DY * DY <= Radius * Radius;
	}

	bool InLiveDisc(const FGFTerritoryMap& Map, const FGFTerritoryFaction& Faction, const FVector& World)
	{
		return InDisc(Faction.Center, Map.RadiusOf(Faction), FVector2D(World.X, World.Y));
	}

	bool InBox(const FGFTerritoryHazard& Hazard, const FVector2D& XY)
	{
		const float DX = FMath::Abs(XY.X - Hazard.Center.X);
		const float DY = FMath::Abs(XY.Y - Hazard.Center.Y);
		return DX <= FMath::Abs(Hazard.HalfExtent.X) && DY <= FMath::Abs(Hazard.HalfExtent.Y);
	}

	bool SameId(FName Id, const TCHAR* Text)
	{
		return Id == FName(Text);
	}

	bool IsLava(const FGFTerritoryHazard& Hazard)
	{
		return SameId(Hazard.Kind, TEXT("LavaBridge")) || SameId(Hazard.Id, TEXT("LavaBridge"));
	}

	bool IsForest(const FGFTerritoryHazard& Hazard)
	{
		return SameId(Hazard.Kind, TEXT("DeadlyForest")) || SameId(Hazard.Id, TEXT("DeadlyForest"));
	}

	enum class ETheme : uint8
	{
		Wild,
		Rime,
		Verdant,
		Cinder,
		Farm,
		Lava,
		Forest
	};

	ETheme ThemeAt(const FGFOverworldLayout& Layout, const FVector2D& XY, const FGFTerritoryHazard** OutHazard)
	{
		if (OutHazard)
		{
			*OutHazard = nullptr;
		}
		const FGFTerritoryFaction* Rime = nullptr;
		const FGFTerritoryFaction* Verdant = nullptr;
		bool bRime = false;
		bool bVerdant = false;
		const FGFTerritoryFaction* Nearest = nullptr;
		float NearestDistSq = 0.f;
		for (const FGFTerritoryFaction& Faction : Layout.Factions)
		{
			const float Radius = PeakRadius(Layout.Curve, Faction);
			if (!InDisc(Faction.Center, Radius, XY))
			{
				continue;
			}
			if (Faction.Id == GFOverworldFaction::Rime())
			{
				Rime = &Faction;
				bRime = true;
			}
			else if (Faction.Id == GFOverworldFaction::Verdant())
			{
				Verdant = &Faction;
				bVerdant = true;
			}
			const float DX = XY.X - Faction.Center.X;
			const float DY = XY.Y - Faction.Center.Y;
			const float DistSq = DX * DX + DY * DY;
			if (!Nearest || DistSq < NearestDistSq)
			{
				Nearest = &Faction;
				NearestDistSq = DistSq;
			}
		}
		if (bRime && bVerdant && Rime && Verdant)
		{
			return ETheme::Farm;
		}
		if (Nearest)
		{
			if (Nearest->Id == GFOverworldFaction::Rime())
			{
				return ETheme::Rime;
			}
			if (Nearest->Id == GFOverworldFaction::Verdant())
			{
				return ETheme::Verdant;
			}
			if (Nearest->Id == GFOverworldFaction::Cinderwrights())
			{
				return ETheme::Cinder;
			}
			return ETheme::Wild;
		}
		for (const FGFTerritoryHazard& Hazard : Layout.Hazards)
		{
			if (!InBox(Hazard, XY))
			{
				continue;
			}
			if (OutHazard)
			{
				*OutHazard = &Hazard;
			}
			if (IsLava(Hazard))
			{
				return ETheme::Lava;
			}
			if (IsForest(Hazard))
			{
				return ETheme::Forest;
			}
			return ETheme::Wild;
		}
		return ETheme::Wild;
	}

	FVector2D CellCenter(const FGFOverworldLayout& Layout, const FIntVector& Cell)
	{
		return FVector2D(
			Layout.Origin.X + (static_cast<float>(Cell.X) + 0.5f) * FGFOverworldLayout::BlockCm,
			Layout.Origin.Y + (static_cast<float>(Cell.Y) + 0.5f) * FGFOverworldLayout::BlockCm);
	}

	bool FarFrom(const FVector& Center, const FVector2D& XY, float MinCm)
	{
		const float DX = XY.X - Center.X;
		const float DY = XY.Y - Center.Y;
		return DX * DX + DY * DY > MinCm * MinCm;
	}

	bool IsTrunkColumn(const FGFOverworldLayout& Layout, const FIntVector& Cell, ETheme Theme, const FVector2D& XY, const FGFTerritoryHazard* Hazard)
	{
		if (Theme == ETheme::Verdant && Cell.X % 9 == 4 && Cell.Y % 9 == 4)
		{
			for (const FGFTerritoryFaction& Faction : Layout.Factions)
			{
				if (Faction.Id == GFOverworldFaction::Verdant())
				{
					return FarFrom(Faction.Center, XY, 500.f);
				}
			}
		}
		if (Theme == ETheme::Forest && Hazard && Cell.X % 6 == 1 && Cell.Y % 6 == 1)
		{
			return FarFrom(Hazard->Center, XY, 200.f);
		}
		return false;
	}
}

bool FGFOverworldLayout::IsInside(const FIntVector& Cell) const
{
	return Cell.X >= 0 && Cell.Y >= 0 && Cell.Z >= 0
		&& Cell.X < GridSize.X && Cell.Y < GridSize.Y && Cell.Z < GridSize.Z;
}

FIntVector FGFOverworldLayout::WorldToCell(const FVector& World) const
{
	const FVector Local = (World - Origin) / BlockCm;
	return FIntVector(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y), FMath::FloorToInt(Local.Z));
}

FIntVector FGFOverworldLayout::SurfaceCell(const FVector& World) const
{
	FIntVector Cell = WorldToCell(FVector(World.X, World.Y, Origin.Z));
	Cell.Z = SurfaceZ;
	return Cell;
}

FName FGFOverworldLayout::BlockAt(const FIntVector& Cell) const
{
	if (!IsInside(Cell))
	{
		return NAME_None;
	}
	const FVector2D XY = CellCenter(*this, Cell);
	const FGFTerritoryHazard* Hazard = nullptr;
	const ETheme Theme = ThemeAt(*this, XY, &Hazard);
	const bool bTrunk = IsTrunkColumn(*this, Cell, Theme, XY, Hazard);

	FName Surface = GFOverworldBlock::Dirt();
	FName Fill = GFOverworldBlock::Stone();
	switch (Theme)
	{
	case ETheme::Rime:
		Surface = GFOverworldBlock::Ice();
		Fill = GFOverworldBlock::Stone();
		break;
	case ETheme::Verdant:
		Surface = GFOverworldBlock::Grass();
		Fill = GFOverworldBlock::Dirt();
		break;
	case ETheme::Cinder:
		Surface = GFOverworldBlock::Basalt();
		Fill = GFOverworldBlock::Basalt();
		break;
	case ETheme::Farm:
		Surface = GFOverworldBlock::Farmland();
		Fill = GFOverworldBlock::Dirt();
		break;
	case ETheme::Lava:
		Surface = GFOverworldBlock::Lava();
		Fill = GFOverworldBlock::Basalt();
		break;
	case ETheme::Forest:
		Surface = GFOverworldBlock::Leaves();
		Fill = GFOverworldBlock::Dirt();
		break;
	case ETheme::Wild:
	default:
		if (bKindleGrass)
		{
			Surface = GFOverworldBlock::Grass();
			Fill = GFOverworldBlock::Dirt();
		}
		else
		{
			Surface = ((Cell.X * 3 + Cell.Y) % 7) == 0 ? GFOverworldBlock::Stone() : GFOverworldBlock::Dirt();
			Fill = GFOverworldBlock::Stone();
		}
		break;
	}

	if (Cell.Z < SurfaceZ)
	{
		return Fill;
	}
	if (Cell.Z == SurfaceZ)
	{
		return Surface;
	}
	if (!bTrunk)
	{
		return NAME_None;
	}
	if (Cell.Z == SurfaceZ + 1 || Cell.Z == SurfaceZ + 2)
	{
		return GFOverworldBlock::Log();
	}
	if (Cell.Z == SurfaceZ + 3)
	{
		return GFOverworldBlock::Leaves();
	}
	return NAME_None;
}

FGFOverworldLayout FGFOverworldBuild::Make(
	const FGFTerritoryCurve& Curve,
	const TArray<FGFTerritoryFaction>& Factions,
	const TArray<FGFTerritoryHazard>& Hazards,
	float MarginCm)
{
	const float Margin = FMath::Max(0.f, MarginCm);
	bool bAny = false;
	float MinX = 0.f;
	float MinY = 0.f;
	float MaxX = 0.f;
	float MaxY = 0.f;
	auto Include = [&](float X, float Y)
	{
		if (!bAny)
		{
			MinX = MaxX = X;
			MinY = MaxY = Y;
			bAny = true;
			return;
		}
		MinX = FMath::Min(MinX, X);
		MinY = FMath::Min(MinY, Y);
		MaxX = FMath::Max(MaxX, X);
		MaxY = FMath::Max(MaxY, Y);
	};

	for (const FGFTerritoryFaction& Faction : Factions)
	{
		const float Radius = PeakRadius(Curve, Faction);
		Include(Faction.Center.X - Radius, Faction.Center.Y - Radius);
		Include(Faction.Center.X + Radius, Faction.Center.Y + Radius);
	}
	for (const FGFTerritoryHazard& Hazard : Hazards)
	{
		const float HalfX = FMath::Abs(Hazard.HalfExtent.X);
		const float HalfY = FMath::Abs(Hazard.HalfExtent.Y);
		Include(Hazard.Center.X - HalfX, Hazard.Center.Y - HalfY);
		Include(Hazard.Center.X + HalfX, Hazard.Center.Y + HalfY);
	}
	for (const FVector2D& Stop : GFFerryStops)
	{
		Include(Stop.X, Stop.Y);
	}
	if (!bAny)
	{
		Include(0.f, 0.f);
	}
	MinX -= Margin;
	MinY -= Margin;
	MaxX += Margin;
	MaxY += Margin;

	FGFOverworldLayout Layout;
	Layout.Curve = Curve;
	Layout.Factions = Factions;
	Layout.Hazards = Hazards;
	Layout.Origin = FVector(MinX, MinY, -static_cast<float>(FGFOverworldLayout::FillLayers) * FGFOverworldLayout::BlockCm);
	Layout.SurfaceZ = FGFOverworldLayout::FillLayers - 1;
	const float Width = FMath::Max(FGFOverworldLayout::BlockCm, MaxX - MinX);
	const float Height = FMath::Max(FGFOverworldLayout::BlockCm, MaxY - MinY);
	Layout.GridSize = FIntVector(
		FMath::Max(1, FMath::CeilToInt(Width / FGFOverworldLayout::BlockCm)),
		FMath::Max(1, FMath::CeilToInt(Height / FGFOverworldLayout::BlockCm)),
		FGFOverworldLayout::GridZ);
	return Layout;
}

FGFOverworldLayout FGFOverworldBuild::MakeKindleRing(
	const FGFTerritoryCurve& Curve,
	const TArray<FGFTerritoryFaction>& Factions,
	const TArray<FGFTerritoryHazard>& Hazards,
	float MarginCm)
{
	FGFOverworldLayout Layout = Make(Curve, Factions, Hazards, MarginCm);
	const float Half = FGFZoneConstants::KindleRadiusCm + FGFZoneConstants::StampMarginCm;
	Layout.Origin = FVector(-Half, -Half, -static_cast<float>(FGFOverworldLayout::FillLayers) * FGFOverworldLayout::BlockCm);
	Layout.SurfaceZ = FGFOverworldLayout::FillLayers - 1;
	Layout.GridSize = FIntVector(FGFZoneConstants::KindleStampBlocks, FGFZoneConstants::KindleStampBlocks, FGFOverworldLayout::GridZ);
	Layout.bKindleGrass = true;
	return Layout;
}

EGFOverworldEdit FGFOverworldBuild::Classify(const FGFTerritoryMap& Map, FName FactionId, const FVector& World)
{
	const FGFTerritoryFaction* Own = FactionId.IsNone() ? nullptr : Map.FindFaction(FactionId);
	const FGFTerritoryFaction* Rime = Map.FindFaction(GFOverworldFaction::Rime());
	const FGFTerritoryFaction* Verdant = Map.FindFaction(GFOverworldFaction::Verdant());
	const bool bRime = Rime && InLiveDisc(Map, *Rime, World);
	const bool bVerdant = Verdant && InLiveDisc(Map, *Verdant, World);
	const bool bSharedFaction = FactionId == GFOverworldFaction::Rime() || FactionId == GFOverworldFaction::Verdant();
	if (bRime && bVerdant && bSharedFaction)
	{
		return EGFOverworldEdit::Shared;
	}
	if (Own && InLiveDisc(Map, *Own, World))
	{
		return EGFOverworldEdit::Own;
	}
	for (const FGFTerritoryFaction& Faction : Map.Factions)
	{
		if (InLiveDisc(Map, Faction, World))
		{
			return EGFOverworldEdit::Foreign;
		}
	}
	return EGFOverworldEdit::Deny;
}
