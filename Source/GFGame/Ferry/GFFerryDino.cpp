#include "GFFerryDino.h"
#include "GFAnimMap.h"
#include "GFAssetMap.h"
#include "GFFerryRules.h"
#include "GFProtectedActor.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace GFFerryDinoPrivate
{
	const FName BodyKey(TEXT("Dino.Ripwing"));
	const TCHAR* SkelPath = TEXT("/Game/Art/Dinos/Ripwing/SK_Dino_Ripwing.SK_Dino_Ripwing");
	const TCHAR* PerchPath = TEXT("/Game/Art/Props/Ferry/SM_FerryPerch.SM_FerryPerch");
	const TCHAR* BasketPath = TEXT("/Game/Art/Props/Ferry/SM_FerryBasket.SM_FerryBasket");
	const TCHAR* MatPath = TEXT("/Game/Art/Dinos/Ripwing/Materials/MI_Dino_Ripwing_Ferry.MI_Dino_Ripwing_Ferry");
}

AGFFerryDino::AGFFerryDino()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	Bird = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Bird"));
	Bird->SetupAttachment(Root);
	Bird->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bird->SetCanEverAffectNavigation(false);
	Bird->SetMobility(EComponentMobility::Movable);
	Perch = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Perch"));
	Perch->SetupAttachment(Root);
	Perch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Perch->SetCanEverAffectNavigation(false);
	Perch->SetMobility(EComponentMobility::Movable);
	SetCanBeDamaged(false);
	Tags.Add(FGFProtectedActor::Tag);
	BodyAssetKey = GFFerryDinoPrivate::BodyKey;
	AnimKey = FGFFerryArt::PerchAnim();
}

float AGFFerryDino::TakeDamage(float /*DamageAmount*/, FDamageEvent const& /*DamageEvent*/, AController* /*EventInstigator*/, AActor* /*DamageCauser*/)
{
	return 0.f;
}

void AGFFerryDino::SetTransit(bool bInTransit)
{
	bTransit = bInTransit;
	if (Perch)
	{
		Perch->SetVisibility(!bInTransit);
	}
	if (!bInTransit)
	{
		SetAnim(FGFFerryArt::PerchAnim());
	}
}

void AGFFerryDino::ParkAt(const FVector& StopLocation)
{
	const FTransform Park = FGFFerryArt::PerchTransform(StopLocation);
	const FVector PerchWorld = Park.TransformPosition(FGFFerryArt::PerchPointLocal());
	SetActorLocationAndRotation(PerchWorld, Park.GetRotation());
	bTransit = false;
	if (Perch)
	{
		Perch->SetVisibility(true);
		Perch->SetRelativeLocation(GetActorTransform().InverseTransformPosition(StopLocation));
		Perch->SetRelativeRotation(FRotator::ZeroRotator);
	}
	AnimKey = NAME_None;
	SetAnim(FGFFerryArt::PerchAnim());
}

void AGFFerryDino::BeginTransit(const FVector& PerchWorld)
{
	SetActorLocationAndRotation(PerchWorld, FGFFerryArt::ParkRotation());
	bTransit = true;
	if (Perch)
	{
		Perch->SetVisibility(false);
	}
	AnimKey = NAME_None;
	SetAnim(FGFFerryArt::TakeoffAnim());
}

void AGFFerryDino::SetAnim(FName Key)
{
	if (Key.IsNone() || AnimKey == Key)
	{
		return;
	}
	AnimKey = Key;
	if (!Bird)
	{
		return;
	}
	UAnimSequenceBase* Clip = FGFAnimMap::Load(Key);
	if (!Clip)
	{
		return;
	}
	const FGFAnimMapRow* Row = FGFAnimMap::Find(Key);
	Bird->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Bird->PlayAnimation(Clip, Row && Row->bLoop);
}

void AGFFerryDino::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFFerryDino, Basket);
}

void AGFFerryDino::OnRep_Basket()
{
	PrepareBasket(Basket);
}

void AGFFerryDino::PrepareBasket(UStaticMeshComponent* Comp)
{
	if (!Comp)
	{
		return;
	}
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	if (!Comp->GetStaticMesh())
	{
		if (UStaticMesh* Mesh = LoadStaticQuiet(GFFerryDinoPrivate::BasketPath))
		{
			Comp->SetStaticMesh(Mesh);
		}
	}
}

UStaticMeshComponent* AGFFerryDino::SpawnBasket()
{
	if (Basket)
	{
		PrepareBasket(Basket);
		return Basket;
	}
	Basket = NewObject<UStaticMeshComponent>(this, TEXT("Basket"));
	if (!Basket)
	{
		return nullptr;
	}
	Basket->SetIsReplicated(true);
	PrepareBasket(Basket);
	Basket->RegisterComponent();
	AddInstanceComponent(Basket);
	AddReplicatedSubObject(Basket);
	return Basket;
}

void AGFFerryDino::BeginPlay()
{
	Super::BeginPlay();
	ApplyBody();
}

UStaticMesh* AGFFerryDino::LoadStaticQuiet(const TCHAR* Path)
{
	return Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
}

void AGFFerryDino::ApplyBody()
{
	// Static placeholder only. ApplySkeletal would copy Dino.Ripwing's capsule offset and Idle/Run
	// onto this actor. The ferry mesh is placed on the perch point, and the ferry clips are separate.
	if (Body && !BodyAssetKey.IsNone())
	{
		FGFAssetMap::Apply(Body, BodyAssetKey);
	}
	if (Bird)
	{
		if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, GFFerryDinoPrivate::SkelPath))
		{
			Bird->SetSkeletalMeshAsset(Mesh);
			Body->SetVisibility(false);
		}
		if (UMaterialInterface* Mat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, GFFerryDinoPrivate::MatPath, nullptr, LOAD_NoWarn | LOAD_Quiet)))
		{
			Bird->SetMaterial(0, Mat);
		}
	}
	if (Perch)
	{
		if (UStaticMesh* Mesh = LoadStaticQuiet(GFFerryDinoPrivate::PerchPath))
		{
			Perch->SetStaticMesh(Mesh);
		}
	}
}
