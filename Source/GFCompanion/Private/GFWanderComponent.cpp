#include "GFWanderComponent.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UGFWanderComponent::UGFWanderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGFWanderComponent::SetHome(const FVector& InHome)
{
	Home = InHome;
	bHomeSet = true;
}

void UGFWanderComponent::SeedRng()
{
	if (bRngSeeded)
	{
		return;
	}
	const int32 Salt = GetOwner() ? static_cast<int32>(GetOwner()->GetUniqueID()) : 1;
	Rng.Initialize(Salt ^ 0x51F0);
	bRngSeeded = true;
}

FVector UGFWanderComponent::PickWanderPoint(const FVector& InHome, float Radius, FRandomStream& Rng)
{
	const float R = FMath::Max(0.f, Radius);
	if (R <= KINDA_SMALL_NUMBER)
	{
		return InHome;
	}
	const float Dist = R * FMath::Sqrt(Rng.FRand());
	const float Ang = Rng.FRand() * 2.f * PI;
	return FVector(InHome.X + FMath::Cos(Ang) * Dist, InHome.Y + FMath::Sin(Ang) * Dist, InHome.Z);
}

float UGFWanderComponent::SpeedFor(FName Species)
{
	const UGFWanderComponent* Defaults = GetDefault<UGFWanderComponent>();
	const float Fallback = (Defaults && Defaults->DefaultWalkSpeed > 0.f) ? Defaults->DefaultWalkSpeed : 250.f;
	if (Defaults)
	{
		if (const float* Found = Defaults->WalkSpeedBySpecies.Find(Species))
		{
			return *Found > 0.f ? *Found : Fallback;
		}
	}
	return Fallback;
}

void UGFWanderComponent::ApplyWalkSpeed()
{
	AGFDino* Dino = Cast<AGFDino>(GetOwner());
	if (!Dino || !Dino->Taming)
	{
		return;
	}
	if (UCharacterMovementComponent* Move = Dino->GetCharacterMovement())
	{
		Move->MaxWalkSpeed = SpeedFor(Dino->Taming->Species);
	}
}

void UGFWanderComponent::TickWander(float DeltaSeconds)
{
	AGFDino* Dino = Cast<AGFDino>(GetOwner());
	if (!Dino || !Dino->HasAuthority())
	{
		return;
	}
	ApplyWalkSpeed();

	const bool bWild = Dino->Taming && Dino->Taming->GetTameState() == EGFTameState::Wild;
	const bool bIdle = Dino->GetDinoState() == EGFDinoState::Idle;
	const bool bResting = Dino->GetDinoState() == EGFDinoState::Rest;
	const bool bNoTarget = Dino->GetAttackTarget() == nullptr;
	if (!bWild || !bIdle || bResting || !bNoTarget)
	{
		return;
	}

	if (!bHomeSet)
	{
		SetHome(Dino->GetActorLocation());
	}
	SeedRng();

	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (!bHasTarget || Now >= NextPickAt)
	{
		WanderTarget = PickWanderPoint(Home, WanderRadius, Rng);
		bHasTarget = true;
		float MinWait = WanderEveryMin;
		float MaxWait = WanderEveryMax;
		if (MinWait > MaxWait)
		{
			Swap(MinWait, MaxWait);
		}
		const float Wait = (MaxWait > MinWait) ? Rng.FRandRange(MinWait, MaxWait) : MinWait;
		NextPickAt = Now + FMath::Max(0.f, Wait);
	}

	if (FVector::Dist2D(Dino->GetActorLocation(), WanderTarget) > StopDistance)
	{
		const FVector Dir = (WanderTarget - Dino->GetActorLocation()).GetSafeNormal2D();
		if (!Dir.IsNearlyZero())
		{
			Dino->AddMovementInput(Dir, 1.f);
		}
	}
}
