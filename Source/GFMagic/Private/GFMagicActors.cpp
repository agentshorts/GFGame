#include "GFMagicActors.h"
#include "GFAssetMap.h"
#include "GFMagicRules.h"
#include "Components/StaticMeshComponent.h"
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
}

void AGFMagicFieldMarker::SetElement(EGFMagicElement InElement)
{
	Element = InElement;
	ApplyMesh();
}

void AGFMagicFieldMarker::OnRep_Element()
{
	ApplyMesh();
}

void AGFMagicFieldMarker::ApplyMesh()
{
	PrepareMesh(Mesh, this, FGFMagicRules::ElementColor(Element), 1.6f);
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
