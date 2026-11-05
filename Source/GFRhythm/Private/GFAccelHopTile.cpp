#include "GFAccelHopTile.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

AGFAccelHopTile::AGFAccelHopTile()
{
	PrimaryActorTick.bCanEverTick = false;
	// Placed with the map on every machine, so it never replicates; the server RPC still names it (stable name).
	bReplicates = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetMobility(EComponentMobility::Static);
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetRenderCustomDepth(true);
	Mesh->SetCustomDepthStencilValue(FGFAccelHopRules::OutlineStencil);
}

void AGFAccelHopTile::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyKind();
}

void AGFAccelHopTile::SetKind(EGFHopTileKind InKind)
{
	Kind = InKind;
	ApplyKind();
}

void AGFAccelHopTile::ApplyKind()
{
	if (!Mesh)
	{
		return;
	}
	Mesh->SetRenderCustomDepth(true);
	Mesh->SetCustomDepthStencilValue(FGFAccelHopRules::OutlineStencil);
	const FString Path = FGFAccelHopRules::MeshPathFor(Kind);
	UStaticMesh* Asset = FGFAccelHopRules::AssetExists(Path) ? LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	if (Asset && Mesh->GetStaticMesh() != Asset)
	{
		// Construction and editor placement only: a static mesh cannot change once play has begun.
		Mesh->SetStaticMesh(Asset);
	}
}

AGFAccelHopTile* AGFAccelHopTile::FromFloor(const UPrimitiveComponent* Floor)
{
	return Floor ? Cast<AGFAccelHopTile>(Floor->GetOwner()) : nullptr;
}

void AGFAccelHopTile::SetRoad(AGFAccelHopRoad* InRoad, int32 InIndex)
{
	Road = InRoad;
	RoadIndex = InRoad ? InIndex : INDEX_NONE;
}

UMaterialInstanceDynamic* AGFAccelHopTile::LitMaterial()
{
	if (LitMID || !Mesh || !Mesh->GetStaticMesh() || !Mesh->GetMaterial(0))
	{
		return LitMID;
	}
	UMaterialInterface* Base = Mesh->GetMaterial(0);
	float Value = 0.f;
	if (Base->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(TEXT("EmissiveStrength"))), Value))
	{
		BaseEmissiveStrength = Value;
	}
	if (Base->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(TEXT("RimLightWidth"))), Value))
	{
		BaseRimLightWidth = Value;
	}
	LitMID = Mesh->CreateDynamicMaterialInstance(0, Base);
	return LitMID;
}

void AGFAccelHopTile::SetLitVisual(bool bLit, float Pulse, const FGFAccelHopLitTuning& Tuning)
{
	const float Clamped = bLit ? FMath::Clamp(Pulse, 0.f, 1.f) : 0.f;
	if (bLit == bLitVisual && FMath::IsNearlyEqual(Clamped, LitPulse, 0.002f))
	{
		return;
	}
	bLitVisual = bLit;
	LitPulse = Clamped;
	if (!bLit && !LitMID)
	{
		return; // never lit, nothing to put back
	}
	UMaterialInstanceDynamic* MID = LitMaterial();
	if (!MID)
	{
		return;
	}
	MID->SetScalarParameterValue(TEXT("EmissiveStrength"), bLit ? FMath::Max(BaseEmissiveStrength, Tuning.LitEmissiveStrength) : BaseEmissiveStrength);
	MID->SetScalarParameterValue(TEXT("RimLightWidth"), bLit ? FMath::Max(BaseRimLightWidth, Tuning.LitRimLightWidth) : BaseRimLightWidth);
	MID->SetScalarParameterValue(TEXT("BeatPulse"), Clamped);
}
