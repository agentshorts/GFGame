#include "GFWorldLayoutDirector.h"

#include "GFAssetMap.h"
#include "GFHubDressing.h"
#include "GFLog.h"
#include "GFOverworldGround.h"
#include "GFGrumbleDigSite.h"
#include "GFRegionGate.h"
#include "GFShardstone.h"
#include "GFZoneCatalog.h"
#include "GFZoneLayout.h"
#include "GFZoneProp.h"
#include "GFZoneTypes.h"
#include "Engine/World.h"

namespace
{
	const FLinearColor KindleGold(0.95f, 0.78f, 0.35f, 1.f);
	const FLinearColor RimStone(0.35f, 0.32f, 0.30f, 1.f);

	AGFZoneProp* SpawnProp(UWorld* World, const FVector& Location, EGFPlaceholderShape Shape, const FLinearColor& Color, const FVector& Scale, float Yaw, bool bSitOnGround, bool bAlwaysRelevant, float CullCm)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		AGFZoneProp* Prop = World->SpawnActor<AGFZoneProp>(AGFZoneProp::StaticClass(), FTransform(FRotator::ZeroRotator, Location), Params);
		if (!Prop)
		{
			return nullptr;
		}
		Prop->bAlwaysRelevant = bAlwaysRelevant;
		if (!bAlwaysRelevant && CullCm > 0.f)
		{
			Prop->SetNetCullDistanceSquared(CullCm * CullCm);
		}
		Prop->Configure(Shape, Color, Scale, Yaw, bSitOnGround);
		Prop->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
		return Prop;
	}
}

AGFWorldLayoutDirector::AGFWorldLayoutDirector()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AGFWorldLayoutDirector::BeginPlay()
{
	Super::BeginPlay();
	if (bLaidOut || !HasAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || !AGFOverworldGround::IsVoxelOverworldMap(World))
	{
		return;
	}
	bLaidOut = true;

	const FGFZoneCatalog& Catalog = FGFZoneCatalog::Get();
	// The origin is the Rime stand. The column shares Stone.Kindle, off that sightline.
	FVector ColumnAt = GFHubDressing::FallbackOrigin();
	if (const FGFShardstoneRow* KindleStone = Catalog.FindStone(FName(TEXT("Stone.Kindle"))))
	{
		ColumnAt = GFHubDressing::ColumnOrigin(KindleStone->Location);
	}
	SpawnProp(World, ColumnAt, EGFPlaceholderShape::Cube, KindleGold, GFHubDressing::SpireScale(), 0.f, true, true, 0.f);
	SpawnProp(World, ColumnAt + FVector(0.f, 0.f, GFHubDressing::CrownHeightCm()), EGFPlaceholderShape::Sphere, KindleGold, GFHubDressing::CrownScale(), 0.f, false, true, 0.f);
	UE_LOG(LogGF, Log, TEXT("Zones: Kindle column at (%.0f, %.0f), ground radius %.0f cm."),
		ColumnAt.X, ColumnAt.Y, GFHubDressing::ColumnRadiusCm());

	for (const FGFRegionSlot& Slot : FGFZoneLayout::Regions())
	{
		const FVector At = FGFZoneLayout::BearingPoint(Slot.BearingDeg, FGFZoneConstants::PorchRingCm);
		SpawnProp(World, At, EGFPlaceholderShape::Cube, Slot.Color, FVector(2.f, 2.f, 60.f), Slot.BearingDeg, true, true, 0.f);
		SpawnProp(World, At + FVector(0.f, 0.f, 6000.f), EGFPlaceholderShape::Sphere, Slot.Color, FVector(5.f, 5.f, 5.f), 0.f, false, true, 0.f);

		const FVector TowardHub = -At.GetSafeNormal2D();
		const FVector ArchAt = At + FVector(TowardHub.X, TowardHub.Y, 0.f) * FGFZoneConstants::PorchArchInsetCm;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		AGFRegionGate* Gate = World->SpawnActor<AGFRegionGate>(AGFRegionGate::StaticClass(), FTransform(FRotator(0.f, Slot.BearingDeg, 0.f), ArchAt), Params);
		if (Gate)
		{
			Gate->bAlwaysRelevant = true;
			Gate->Setup(Slot);
			Gate->FinishSpawning(FTransform(FRotator(0.f, Slot.BearingDeg, 0.f), ArchAt));
		}
	}

	constexpr int32 RimSegments = 72;
	for (int32 Index = 0; Index < RimSegments; ++Index)
	{
		const float Bearing = (360.f / static_cast<float>(RimSegments)) * static_cast<float>(Index);
		const FVector At = FGFZoneLayout::BearingPoint(Bearing, FGFZoneConstants::RimRadiusCm);
		SpawnProp(World, At, EGFPlaceholderShape::Cube, RimStone, FVector(30.f, 120.f, 200.f), Bearing, true, true, 0.f);
	}

	for (const FGFShardstoneRow& Stone : Catalog.Stones)
	{
		FLinearColor Color = KindleGold;
		if (const FGFZoneRow* Zone = Catalog.FindZone(Stone.ZoneId))
		{
			if (const FGFRegionSlot* Slot = FGFZoneLayout::FindRegion(Zone->RegionId))
			{
				Color = Slot->Color;
			}
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		AGFShardstone* Marker = World->SpawnActor<AGFShardstone>(AGFShardstone::StaticClass(), FTransform(FVector(Stone.Location.X, Stone.Location.Y, 0.f)), Params);
		if (Marker)
		{
			Marker->bAlwaysRelevant = true;
			Marker->Setup(Stone, Color);
			Marker->FinishSpawning(FTransform(FVector(Stone.Location.X, Stone.Location.Y, 0.f)));
		}
	}

	const TArray<FGFPOIPoint> Points = Catalog.ScatterPOI(FGFZoneConstants::PoiSeed);
	for (const FGFPOIPoint& Point : Points)
	{
		EGFPlaceholderShape Shape = EGFPlaceholderShape::Sphere;
		FLinearColor Color(0.72f, 0.62f, 0.42f, 1.f);
		FVector Scale(4.f, 4.f, 2.f);
		if (Point.Kind == FName(TEXT("Crystal")))
		{
			Shape = EGFPlaceholderShape::Cone;
			Color = FLinearColor(0.45f, 0.85f, 0.95f, 1.f);
			Scale = FVector(3.f, 3.f, 6.f);
		}
		else if (Point.Kind == FName(TEXT("Hull")))
		{
			Shape = EGFPlaceholderShape::Cube;
			Color = FLinearColor(0.45f, 0.28f, 0.18f, 1.f);
			Scale = FVector(8.f, 3.f, 2.f);
		}
		SpawnProp(World, Point.Location, Shape, Color, Scale, 0.f, true, false, 25000.f);
	}

	EnsureGrumbleDigSite();

	UE_LOG(LogGF, Log, TEXT("Zones: hub layout spawned %d points of interest, %d stones, %d porches."),
		Points.Num(), Catalog.Stones.Num(), FGFZoneLayout::Regions().Num());
}

void AGFWorldLayoutDirector::EnsureGrumbleDigSite()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const FGFRegionSlot* Slot = FGFZoneLayout::FindByGate(TEXT("GrumbleDig"));
	if (!Slot || !Slot->bEnabled)
	{
		return;
	}
	const FVector At = FGFZoneLayout::BearingPoint(Slot->BearingDeg, FGFZoneConstants::PorchRingCm);
	AGFGrumbleDigSite::Ensure(World, At);
}
