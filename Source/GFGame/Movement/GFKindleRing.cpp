#include "GFKindleRing.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "Components/SphereComponent.h"

AGFKindleRing::AGFKindleRing()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("KindleSphere"));
	SetRootComponent(Sphere);
	Sphere->InitSphereRadius(400.f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionObjectType(ECC_WorldDynamic);
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Sphere->SetGenerateOverlapEvents(true);
}

void AGFKindleRing::ApplyRadius(float RadiusCm)
{
	if (Sphere)
	{
		Sphere->SetSphereRadius(FMath::Max(0.f, RadiusCm));
	}
}

float AGFKindleRing::GetRadius() const
{
	return Sphere ? Sphere->GetScaledSphereRadius() : 0.f;
}

void AGFKindleRing::BeginPlay()
{
	Super::BeginPlay();
	TArray<AActor*> Inside;
	GetOverlappingActors(Inside, AGFCharacter::StaticClass());
	for (AActor* Actor : Inside)
	{
		RefreshPawn(Actor);
	}
}

void AGFKindleRing::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);
	RefreshPawn(OtherActor);
}

void AGFKindleRing::NotifyActorEndOverlap(AActor* OtherActor)
{
	Super::NotifyActorEndOverlap(OtherActor);
	RefreshPawn(OtherActor);
}

void AGFKindleRing::RefreshPawn(AActor* Other) const
{
	const AGFCharacter* Character = Cast<AGFCharacter>(Other);
	if (!Character)
	{
		return;
	}
	if (UGFCharacterMovementComponent* Move = Character->GetGFMovement())
	{
		Move->RefreshKindleOverlaps();
	}
}
