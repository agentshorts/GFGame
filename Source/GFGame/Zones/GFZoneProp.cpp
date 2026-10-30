#include "GFZoneProp.h"

#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AGFZoneProp::AGFZoneProp()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
}

void AGFZoneProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFZoneProp, Visual);
}

void AGFZoneProp::Configure(EGFPlaceholderShape Shape, const FLinearColor& Color, const FVector& Scale, float Yaw, bool bSitOnGround)
{
	Visual.Shape = Shape;
	Visual.Color = Color;
	Visual.Scale = Scale;
	Visual.Yaw = Yaw;
	Visual.bSitOnGround = bSitOnGround;
	ApplyVisual();
}

void AGFZoneProp::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisual();
}

void AGFZoneProp::OnRep_Visual()
{
	ApplyVisual();
}

void AGFZoneProp::ApplyVisual()
{
	if (!Mesh)
	{
		return;
	}
	if (UStaticMesh* Asset = FGFAssetMap::PlaceholderMesh(Visual.Shape))
	{
		Mesh->SetStaticMesh(Asset);
	}
	if (UMaterialInterface* Tint = FGFAssetMap::MakeTint(Mesh, Visual.Color))
	{
		Mesh->SetMaterial(0, Tint);
	}
	Mesh->SetRelativeScale3D(Visual.Scale);
	const float Lift = Visual.bSitOnGround ? Visual.Scale.Z * 50.f : 0.f;
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, Lift));
	Mesh->SetRelativeRotation(FRotator(0.f, Visual.Yaw, 0.f));
}
