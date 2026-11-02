#include "GFMagicActors.h"
#include "GFAssetMap.h"
#include "GFMagicRules.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"

namespace
{
	void PrepareMesh(UStaticMeshComponent* Mesh, AActor* Owner, const FLinearColor& Color, float Scale)
	{
		if (!Mesh)
		{
			return;
		}
		Mesh->SetStaticMesh(FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Sphere));
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetRelativeScale3D(FVector(Scale));
		if (UMaterialInterface* Tint = FGFAssetMap::MakeTint(Owner, Color))
		{
			Mesh->SetMaterial(0, Tint);
		}
	}
}

AGFMagicDropRibbon::AGFMagicDropRibbon()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
}

void AGFMagicDropRibbon::BeginPlay()
{
	Super::BeginPlay();
	ApplyMesh();
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
	}
}

void AGFMagicDropRibbon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFMagicDropRibbon, From);
	DOREPLIFETIME(AGFMagicDropRibbon, FallbackTo);
	DOREPLIFETIME(AGFMagicDropRibbon, Target);
	DOREPLIFETIME(AGFMagicDropRibbon, ItemId);
}

void AGFMagicDropRibbon::Setup(const FVector& InFrom, APawn* InTarget, const FVector& InFallbackTo, FName InItemId)
{
	From = InFrom;
	Target = InTarget;
	FallbackTo = InFallbackTo;
	ItemId = InItemId;
	SetActorLocation(From);
	ApplyMesh();
}

void AGFMagicDropRibbon::ApplyMesh()
{
	PrepareMesh(Mesh, this, FLinearColor(0.95f, 0.78f, 0.35f, 1.f), 0.22f);
}

void AGFMagicDropRibbon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	Age += DeltaSeconds;
	const float Alpha = FMath::Clamp(Age / LifeSeconds, 0.f, 1.f);
	const FVector Dest = Target ? Target->GetActorLocation() : FallbackTo;
	SetActorLocation(FMath::Lerp(From, Dest, Alpha));
	if (Age >= LifeSeconds)
	{
		Destroy();
	}
}

AGFMagicFieldMarker::AGFMagicFieldMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Light = CreateDefaultSubobject<USpotLightComponent>(TEXT("Beam"));
	Light->SetupAttachment(Mesh);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetCastShadows(false);
	Light->SetIntensity(0.f);
	Light->SetVisibility(false);
}

void AGFMagicFieldMarker::BeginPlay()
{
	Super::BeginPlay();
	ApplyMesh();
}

void AGFMagicFieldMarker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFMagicFieldMarker, Element);
	DOREPLIFETIME(AGFMagicFieldMarker, BeamDir);
	DOREPLIFETIME(AGFMagicFieldMarker, BeamHalfAngle);
	DOREPLIFETIME(AGFMagicFieldMarker, BeamRangeCm);
	DOREPLIFETIME(AGFMagicFieldMarker, BeamIntensity);
}

void AGFMagicFieldMarker::SetBeam(EGFMagicElement InElement, const FVector& Dir, float HalfAngleDeg, float RangeCm, float Intensity)
{
	Element = InElement;
	BeamDir = Dir.GetSafeNormal().IsNearlyZero() ? FVector(0.f, 0.f, -1.f) : Dir.GetSafeNormal();
	BeamHalfAngle = FMath::Max(1.f, HalfAngleDeg);
	BeamRangeCm = FMath::Max(100.f, RangeCm);
	BeamIntensity = FMath::Max(0.f, Intensity);
	ApplyMesh();
}

void AGFMagicFieldMarker::OnRep_Beam()
{
	ApplyMesh();
}

void AGFMagicFieldMarker::ApplyMesh()
{
	if (!Mesh)
	{
		return;
	}
	const bool bCone = Element == EGFMagicElement::Spark || BeamIntensity > 0.f;
	const FLinearColor Color = FGFMagicRules::ElementColor(Element);
	Mesh->SetStaticMesh(FGFAssetMap::PlaceholderMesh(bCone ? EGFPlaceholderShape::Cone : EGFPlaceholderShape::Sphere));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UMaterialInterface* Tint = FGFAssetMap::MakeTint(this, Color))
	{
		Mesh->SetMaterial(0, Tint);
	}
	const FVector Dir = BeamDir.GetSafeNormal().IsNearlyZero() ? FVector(0.f, 0.f, -1.f) : BeamDir.GetSafeNormal();
	if (bCone)
	{
		const float HalfRad = FMath::DegreesToRadians(FMath::Clamp(BeamHalfAngle, 1.f, 89.f));
		const float Length = FMath::Max(100.f, BeamRangeCm);
		const float Radius = Length * FMath::Tan(HalfRad);
		Mesh->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, Length / 100.f));
		Mesh->SetRelativeRotation(FRotationMatrix::MakeFromZ(Dir).Rotator());
	}
	else
	{
		Mesh->SetRelativeScale3D(FVector(1.6f));
		Mesh->SetRelativeRotation(FRotator::ZeroRotator);
	}
	if (Light)
	{
		const bool bLit = bCone && BeamIntensity > 0.f;
		Light->SetVisibility(bLit);
		Light->SetIntensity(bLit ? BeamIntensity : 0.f);
		Light->SetOuterConeAngle(FMath::Clamp(BeamHalfAngle, 1.f, 80.f));
		Light->SetInnerConeAngle(FMath::Clamp(BeamHalfAngle * 0.5f, 0.f, 80.f));
		Light->SetAttenuationRadius(FMath::Max(100.f, BeamRangeCm));
		Light->SetCastShadows(false);
		Light->SetRelativeRotation(FRotationMatrix::MakeFromX(Dir).Rotator());
		Light->SetLightColor(Color);
	}
}

AGFMagicClaw::AGFMagicClaw()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
}

void AGFMagicClaw::BeginPlay()
{
	Super::BeginPlay();
	ApplyMesh();
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
	}
}

void AGFMagicClaw::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFMagicClaw, From);
	DOREPLIFETIME(AGFMagicClaw, To);
	DOREPLIFETIME(AGFMagicClaw, Form);
	DOREPLIFETIME(AGFMagicClaw, Color);
}

void AGFMagicClaw::Setup(const FVector& InFrom, const FVector& InTo, FName InForm, const FLinearColor& InColor)
{
	From = InFrom;
	To = InTo;
	Form = InForm;
	Color = InColor;
	SetActorLocation(From);
	ApplyMesh();
}

void AGFMagicClaw::OnRep_Color()
{
	ApplyMesh();
}

void AGFMagicClaw::ApplyMesh()
{
	PrepareMesh(Mesh, this, Color, 0.55f);
}

void AGFMagicClaw::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	Age += DeltaSeconds;
	const float Alpha = FMath::Clamp(Age / LifeSeconds, 0.f, 1.f);
	SetActorLocation(FMath::Lerp(From, To, Alpha));
	if (Age >= LifeSeconds)
	{
		Destroy();
	}
}
