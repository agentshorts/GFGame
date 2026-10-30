#include "GFOverworldGround.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GFAssetMap.h"
#include "GFLog.h"
#include "GFOverworldBuild.h"
#include "GFTerritorySubsystem.h"
#include "GFZoneCatalog.h"
#include "GFZoneTypes.h"

namespace
{
	constexpr float EngineShapeCm = 100.f;
	constexpr float LowestTopZ = -0.5f;

	float LargestRadius(const TArray<float>& Radii)
	{
		float Best = 0.f;
		for (const float Radius : Radii)
		{
			Best = FMath::Max(Best, Radius);
		}
		return FMath::Max(0.f, Best);
	}

	bool FindTint(const TArray<FGFOverworldTint>& Table, FName Id, FLinearColor& Out)
	{
		bool bFound = false;
		for (const FGFOverworldTint& Tint : Table)
		{
			if (Tint.Id == Id)
			{
				Out = Tint.Color;
				bFound = true;
			}
		}
		return bFound;
	}

	FLinearColor FallbackFactionColour(FName Id)
	{
		// Render swatches from Content/Art, stored as sRGB byte / 255 (same channel convention as before).
		if (Id == FName(TEXT("Rime")))
		{
			return FLinearColor(0.623529f, 0.847059f, 0.960784f, 1.f); // #9FD8F5 pale ice blue
		}
		if (Id == FName(TEXT("Verdant")))
		{
			return FLinearColor(0.556863f, 0.831373f, 0.478431f, 1.f); // #8ED47A leaf green
		}
		if (Id == FName(TEXT("Cinderwrights")))
		{
			return GFOverworldCinderColour(); // #D9542B ember
		}
		return FLinearColor(0.60f, 0.60f, 0.60f, 1.f);
	}

	FLinearColor FallbackHazardColour(FName Id)
	{
		if (Id == FName(TEXT("LavaBridge")))
		{
			return FLinearColor(1.000000f, 0.760784f, 0.101961f, 1.f); // #FFC21A hot gold
		}
		if (Id == FName(TEXT("DeadlyForest")))
		{
			return FLinearColor(0.149020f, 0.250980f, 0.172549f, 1.f); // #26402C deep pine
		}
		return FLinearColor(0.40f, 0.40f, 0.40f, 1.f);
	}

	FLinearColor ColourFor(const TArray<FGFOverworldTint>& Table, FName Id, FName AltId, const FLinearColor& Fallback)
	{
		FLinearColor Colour;
		if (FindTint(Table, Id, Colour) || (!AltId.IsNone() && FindTint(Table, AltId, Colour)))
		{
			return Colour;
		}
		return Fallback;
	}

	void AddDefaultTints(TArray<FGFOverworldTint>& Factions, TArray<FGFOverworldTint>& Hazards)
	{
		auto Add = [](TArray<FGFOverworldTint>& Table, const TCHAR* Id, const FLinearColor& Colour)
		{
			FGFOverworldTint Tint;
			Tint.Id = Id;
			Tint.Color = Colour;
			Table.Add(Tint);
		};
		Add(Factions, TEXT("Rime"), FallbackFactionColour(TEXT("Rime")));
		Add(Factions, TEXT("Verdant"), FallbackFactionColour(TEXT("Verdant")));
		Add(Factions, TEXT("Cinderwrights"), FallbackFactionColour(TEXT("Cinderwrights")));
		Add(Hazards, TEXT("LavaBridge"), FallbackHazardColour(TEXT("LavaBridge")));
		Add(Hazards, TEXT("DeadlyForest"), FallbackHazardColour(TEXT("DeadlyForest")));
	}

	FVector ShapeCenter(const FVector2D& XY, float TopZ, float Thickness)
	{
		return FVector(XY.X, XY.Y, TopZ - Thickness * 0.5f);
	}

	void DrawFlat(AGFOverworldGround* Owner, EGFPlaceholderShape MeshKind, const FVector& WorldCenter, const FVector& Scale, const FLinearColor& Color, bool bBlock)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner);
		Mesh->SetupAttachment(Owner->GetRootComponent());
		if (UStaticMesh* Asset = FGFAssetMap::PlaceholderMesh(MeshKind))
		{
			Mesh->SetStaticMesh(Asset);
		}
		if (UMaterialInterface* Tint = FGFAssetMap::MakeTint(Mesh, Color))
		{
			Mesh->SetMaterial(0, Tint);
		}
		Mesh->SetRelativeLocation(Owner->GetActorTransform().InverseTransformPosition(WorldCenter));
		Mesh->SetRelativeScale3D(Scale);
		Mesh->SetMobility(EComponentMobility::Static);
		Mesh->SetCanEverAffectNavigation(false);
		if (bBlock)
		{
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		else
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		Owner->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
	}
}

bool FGFOverworldShape::CoversXY(const FVector2D& Point) const
{
	if (Kind == EGFOverworldShapeKind::FactionDisc)
	{
		const FVector2D At(Center.X, Center.Y);
		return (At - Point).Size() <= DiscRadius;
	}
	return FMath::Abs(Point.X - Center.X) <= HalfExtent.X && FMath::Abs(Point.Y - Center.Y) <= HalfExtent.Y;
}

AGFOverworldGround::AGFOverworldGround()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		Margin = 2000.f;
		Thickness = 100.f;
		SurfaceZ = -2.f;
		OverlayLift = 1.f;
		SlabColor = FLinearColor(0.45f, 0.40f, 0.34f, 1.f);
		AddDefaultTints(FactionColours, HazardColours);
	}
}

FGFOverworldParams AGFOverworldGround::CurrentParams() const
{
	FGFOverworldParams Params;
	Params.Margin = Margin;
	Params.Thickness = Thickness;
	Params.SurfaceZ = SurfaceZ;
	Params.OverlayLift = OverlayLift;
	Params.SlabColor = SlabColor;
	Params.FactionColours = FactionColours;
	Params.HazardColours = HazardColours;
	return Params;
}

TArray<FGFOverworldShape> AGFOverworldGround::BuildShapes(
	const TArray<FGFTerritoryFaction>& Factions,
	const TArray<FGFTerritoryHazard>& Hazards,
	const FGFOverworldParams& Params)
{
	const float Thickness = FMath::Max(1.f, Params.Thickness);
	const float Margin = FMath::Max(0.f, Params.Margin);
	const float SlabTop = FMath::Min(Params.SurfaceZ, LowestTopZ);
	const float OverlayTop = FMath::Min(SlabTop + FMath::Max(0.f, Params.OverlayLift), LowestTopZ);
	FLinearColor SlabColour = Params.SlabColor;
	if (SlabColour.A <= 0.f)
	{
		SlabColour = FLinearColor(0.45f, 0.40f, 0.34f, 1.f);
	}

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
		const float Radius = LargestRadius(Faction.RadiusByMembers);
		Include(Faction.Center.X - Radius, Faction.Center.Y - Radius);
		Include(Faction.Center.X + Radius, Faction.Center.Y + Radius);
	}
	for (const FGFTerritoryHazard& Hazard : Hazards)
	{
		const float HalfX = FMath::Max(0.f, Hazard.HalfExtent.X);
		const float HalfY = FMath::Max(0.f, Hazard.HalfExtent.Y);
		Include(Hazard.Center.X - HalfX, Hazard.Center.Y - HalfY);
		Include(Hazard.Center.X + HalfX, Hazard.Center.Y + HalfY);
	}
	if (!bAny)
	{
		Include(0.f, 0.f);
	}
	MinX -= Margin;
	MinY -= Margin;
	MaxX += Margin;
	MaxY += Margin;

	TArray<FGFOverworldShape> Shapes;

	FGFOverworldShape Slab;
	Slab.Kind = EGFOverworldShapeKind::Slab;
	Slab.HalfExtent = FVector2D((MaxX - MinX) * 0.5f, (MaxY - MinY) * 0.5f);
	Slab.TopZ = SlabTop;
	Slab.Center = ShapeCenter(FVector2D((MinX + MaxX) * 0.5f, (MinY + MaxY) * 0.5f), SlabTop, Thickness);
	Slab.Scale = FVector((Slab.HalfExtent.X * 2.f) / EngineShapeCm, (Slab.HalfExtent.Y * 2.f) / EngineShapeCm, Thickness / EngineShapeCm);
	Slab.Color = SlabColour;
	Shapes.Add(Slab);

	for (const FGFTerritoryFaction& Faction : Factions)
	{
		const float Radius = LargestRadius(Faction.RadiusByMembers);
		if (Radius <= 0.f)
		{
			continue;
		}
		FGFOverworldShape Disc;
		Disc.Kind = EGFOverworldShapeKind::FactionDisc;
		Disc.Id = Faction.Id;
		Disc.DiscRadius = Radius;
		Disc.TopZ = OverlayTop;
		Disc.Center = ShapeCenter(FVector2D(Faction.Center.X, Faction.Center.Y), OverlayTop, Thickness);
		const float Diameter = Radius * 2.f;
		Disc.Scale = FVector(Diameter / EngineShapeCm, Diameter / EngineShapeCm, Thickness / EngineShapeCm);
		Disc.Color = ColourFor(Params.FactionColours, Faction.Id, NAME_None, FallbackFactionColour(Faction.Id));
		Shapes.Add(Disc);
	}

	for (const FGFTerritoryHazard& Hazard : Hazards)
	{
		FGFOverworldShape Box;
		Box.Kind = EGFOverworldShapeKind::HazardBox;
		Box.Id = Hazard.Id;
		Box.HalfExtent = FVector2D(FMath::Max(0.f, Hazard.HalfExtent.X), FMath::Max(0.f, Hazard.HalfExtent.Y));
		Box.TopZ = OverlayTop;
		Box.Center = ShapeCenter(FVector2D(Hazard.Center.X, Hazard.Center.Y), OverlayTop, Thickness);
		Box.Scale = FVector((Box.HalfExtent.X * 2.f) / EngineShapeCm, (Box.HalfExtent.Y * 2.f) / EngineShapeCm, Thickness / EngineShapeCm);
		const FName Alt = Hazard.Kind == Hazard.Id ? NAME_None : Hazard.Kind;
		Box.Color = ColourFor(Params.HazardColours, Hazard.Id, Alt, FallbackHazardColour(Hazard.Id.IsNone() ? Hazard.Kind : Hazard.Id));
		Shapes.Add(Box);
	}

	return Shapes;
}

bool AGFOverworldGround::IsVoxelOverworldMap(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	if (World->GetMapName().Contains(TEXT("L_Hub")))
	{
		return true;
	}
	const UPackage* Package = World->GetOutermost();
	return Package && Package->GetName().Contains(TEXT("L_Hub"));
}

bool AGFOverworldGround::ShouldDrawShape(EGFOverworldShapeKind Kind, bool bVoxelOverworldMap)
{
	if (bVoxelOverworldMap)
	{
		return false;
	}
	return Kind != EGFOverworldShapeKind::HazardBox;
}

void AGFOverworldGround::BeginPlay()
{
	Super::BeginPlay();
	if (bGroundBuilt)
	{
		return;
	}
	bGroundBuilt = true;

	const UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(this);
	if (!Territory)
	{
		Territory = GetDefault<UGFTerritorySubsystem>();
	}
	if (!Territory)
	{
		UE_LOG(LogGF, Warning, TEXT("World: overworld ground has no territory layout"));
		return;
	}

	const bool bHub = IsVoxelOverworldMap(GetWorld());
	if (bHub)
	{
		const float ThicknessCm = FMath::Max(1.f, Thickness);
		const float SlabTop = SurfaceZ;
		const FVector SlabCenter(0.f, 0.f, SlabTop - ThicknessCm * 0.5f);
		const float SlabScale = (FGFZoneConstants::RimRadiusCm * 2.f) / EngineShapeCm;
		DrawFlat(this, EGFPlaceholderShape::Cube, SlabCenter, FVector(SlabScale, SlabScale, ThicknessCm / EngineShapeCm), SlabColor, true);
		const float DiscTop = SurfaceZ + OverlayLift;
		const FVector DiscScaleBase(0.f, 0.f, ThicknessCm / EngineShapeCm);
		for (const FGFPorchDisc& Disc : FGFZoneCatalog::Get().PorchDiscs())
		{
			const float Diameter = FMath::Max(EngineShapeCm, Disc.RadiusCm * 2.f);
			FVector Scale = DiscScaleBase;
			Scale.X = Diameter / EngineShapeCm;
			Scale.Y = Diameter / EngineShapeCm;
			const FVector Center(Disc.Center.X, Disc.Center.Y, DiscTop - ThicknessCm * 0.5f);
			DrawFlat(this, EGFPlaceholderShape::Cylinder, Center, Scale, Disc.Color, false);
		}
	}
	const TArray<FGFOverworldShape> Shapes = BuildShapes(Territory->FactionLayout, Territory->Hazards, CurrentParams());
	int32 Drawn = 0;
	for (const FGFOverworldShape& Shape : Shapes)
	{
		if (!ShouldDrawShape(Shape.Kind, bHub))
		{
			continue;
		}
		++Drawn;
		const EGFPlaceholderShape MeshKind = Shape.Kind == EGFOverworldShapeKind::FactionDisc
			? EGFPlaceholderShape::Cylinder
			: EGFPlaceholderShape::Cube;
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetupAttachment(GetRootComponent());
		if (UStaticMesh* Asset = FGFAssetMap::PlaceholderMesh(MeshKind))
		{
			Mesh->SetStaticMesh(Asset);
		}
		else
		{
			UE_LOG(LogGF, Warning, TEXT("World: overworld shape %s has no engine mesh"), *Shape.Id.ToString());
		}
		if (UMaterialInterface* Tint = FGFAssetMap::MakeTint(Mesh, Shape.Color))
		{
			Mesh->SetMaterial(0, Tint);
		}
		// Shape centers are world positions. The actor is spawned at the origin; this still
		// lands correctly if that actor is moved, as long as its scale stays 1.
		// Transform is set before Static mobility so the location is not locked.
		Mesh->SetRelativeLocation(GetActorTransform().InverseTransformPosition(Shape.Center));
		Mesh->SetRelativeScale3D(Shape.Scale);
		Mesh->SetMobility(EComponentMobility::Static);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCanEverAffectNavigation(false);
		AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
	}

	UE_LOG(LogGF, Log, TEXT("World: overworld ground drew %d of %d shapes on this machine."), Drawn, Shapes.Num());
}
