#include "GFGiantDino.h"
#include "GFAssetMap.h"
#include "GFSimpleAnimComponent.h"
#include "GFPlayerState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

AGFGiantDino::AGFGiantDino()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);
	SetCanBeDamaged(true);

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	SetRootComponent(Capsule);
	Capsule->InitCapsuleSize(350.f, 600.f);
	// Hittable by traces (attacks), never blocks or pushes anyone physically: shoves are the only contact.
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	ScaleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ScaleRoot"));
	ScaleRoot->SetupAttachment(Capsule);
	ScaleRoot->SetRelativeLocation(FVector(0.f, 0.f, -600.f));

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(ScaleRoot);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Art = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Art"));
	Art->SetupAttachment(ScaleRoot);
	Art->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Art->SetVisibility(false);
	Art->SetHiddenInGame(true);
	Art->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	SimpleAnim = CreateDefaultSubobject<UGFSimpleAnimComponent>(TEXT("SimpleAnim"));
}

void AGFGiantDino::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFGiantDino, State);
	DOREPLIFETIME(AGFGiantDino, TargetKey);
	DOREPLIFETIME(AGFGiantDino, DisplayName);
	DOREPLIFETIME(AGFGiantDino, AssetKey);
	DOREPLIFETIME(AGFGiantDino, MeshScale);
}

void AGFGiantDino::SetLook(const FString& InName, FName InAssetKey, float InScale)
{
	DisplayName = InName;
	AssetKey = InAssetKey;
	MeshScale = InScale > 0.f ? InScale : 1.f;
	ApplyLook();
}

void AGFGiantDino::OnRep_Look()
{
	ApplyLook();
}

void AGFGiantDino::ApplyLook()
{
	if (!AssetKey.IsNone())
	{
		FGFAssetMap::ApplySkeletal(Art, Body, AssetKey);
	}
	ScaleRoot->SetRelativeScale3D(FVector(MeshScale));
}

float AGFGiantDino::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// No health: being hit only means "this player attacked me". Nothing is passed on to anyone.
	if (!HasAuthority())
	{
		return 0.f;
	}
	AController* By = EventInstigator;
	if (!By && DamageCauser)
	{
		By = DamageCauser->GetInstigatorController();
	}
	AGFPlayerState* PS = By ? By->GetPlayerState<AGFPlayerState>() : nullptr;
	if (PS)
	{
		NotifyAttackedBy(PS);
	}
	return 0.f;
}

void AGFGiantDino::NotifyAttackedBy(AGFPlayerState* By)
{
	if (HasAuthority() && By)
	{
		OnAttackedBy.Broadcast(this, By);
	}
}

void AGFGiantDino::MoveBody(const FVector& Location, const FVector* FaceToward)
{
	SetActorLocation(FVector(Location.X, Location.Y, GetActorLocation().Z));
	if (FaceToward)
	{
		const FVector To = (*FaceToward - GetActorLocation()).GetSafeNormal2D();
		if (!To.IsNearlyZero())
		{
			SetActorRotation(To.Rotation());
		}
	}
}
