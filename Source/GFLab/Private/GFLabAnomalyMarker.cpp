#include "GFLabAnomalyMarker.h"
#include "GFAssetMap.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AGFLabAnomalyMarker::AGFLabAnomalyMarker()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
}

void AGFLabAnomalyMarker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFLabAnomalyMarker, Kind);
}

void AGFLabAnomalyMarker::Setup(FName InKind)
{
	Kind = InKind;
	ApplyLook();
}

void AGFLabAnomalyMarker::OnRep_Kind()
{
	ApplyLook();
}

void AGFLabAnomalyMarker::ApplyLook()
{
	if (!Body)
	{
		return;
	}
	FLinearColor Tint(1.f, 0.2f, 0.8f);
	if (Kind == TEXT("RoguePrinterSwarm"))
	{
		Tint = FLinearColor(1.f, 0.42f, 0.08f);
	}
	else if (Kind == TEXT("ChairEatingFloor"))
	{
		Tint = FLinearColor(0.45f, 0.82f, 0.22f);
	}
	else if (Kind == TEXT("FlickeringServerRoom"))
	{
		Tint = FLinearColor(0.30f, 0.72f, 1.f);
	}
	if (UStaticMesh* Mesh = FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube))
	{
		Body->SetStaticMesh(Mesh);
	}
	Body->SetRelativeScale3D(FVector(0.8f));
	if (UMaterialInterface* Mat = FGFAssetMap::MakeTint(this, Tint))
	{
		Body->SetMaterial(0, Mat);
	}
}

void AGFLabAnomalyMarker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Kind != TEXT("FlickeringServerRoom") || !Body)
	{
		return;
	}
	// Local pulse. Each machine ticks its own copy; the rules don't care if the blinks line up.
	Flicker += DeltaSeconds;
	const bool bOn = FMath::Fmod(Flicker, 0.4f) < 0.25f;
	Body->SetVisibility(bOn, true);
	Body->SetRelativeScale3D(FVector(bOn ? 0.8f : 0.45f));
}
