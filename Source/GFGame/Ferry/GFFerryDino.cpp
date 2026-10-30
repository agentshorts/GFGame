#include "GFFerryDino.h"
#include "GFAssetMap.h"
#include "GFProtectedActor.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	const FName BodyKey(TEXT("Dino.Ripwing"));
	const FName PerchAnim(TEXT("Ripwing.Perch_Idle"));
	const FName GlideAnim(TEXT("Ripwing.Glide_Loop"));
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
	SetCanBeDamaged(false);
	Tags.Add(FGFProtectedActor::Tag);
	BodyAssetKey = BodyKey;
	AnimKey = PerchAnim;
}

float AGFFerryDino::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// The protection deal: a hit reports nothing and changes nothing.
	return 0.f;
}

void AGFFerryDino::SetTransit(bool bInTransit)
{
	bTransit = bInTransit;
	AnimKey = bTransit ? GlideAnim : PerchAnim;
}

void AGFFerryDino::BeginPlay()
{
	Super::BeginPlay();
	ApplyBody();
}

void AGFFerryDino::ApplyBody()
{
	if (Body && !BodyAssetKey.IsNone())
	{
		FGFAssetMap::Apply(Body, BodyAssetKey);
	}
}
