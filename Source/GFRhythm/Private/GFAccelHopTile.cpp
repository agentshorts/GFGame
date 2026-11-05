#include "GFAccelHopTile.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"

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
