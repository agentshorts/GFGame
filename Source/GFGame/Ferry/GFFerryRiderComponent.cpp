#include "GFFerryRiderComponent.h"
#include "GFAnimMap.h"
#include "GFFerryDirector.h"
#include "GFFerryRules.h"
#include "GFProtectedActor.h"
#include "GFSimpleAnimComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

namespace GFFerryRiderPrivate
{
	// Longer than a trip. EndSeat releases it. A looping sit is not left alone by UGFSimpleAnimComponent.
	constexpr float SeatHoldSeconds = 1.0e6f;
}

UGFFerryRiderComponent::UGFFerryRiderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UGFFerryRiderComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGFFerryRiderComponent, bInFlight);
	DOREPLIFETIME(UGFFerryRiderComponent, LastCode);
	DOREPLIFETIME(UGFFerryRiderComponent, SeatAnim);
}

void UGFFerryRiderComponent::RequestTrip(FName FromStop)
{
	ServerRequestTrip(FromStop, FGuid::NewGuid());
}

void UGFFerryRiderComponent::BuyPass(FName AtStop)
{
	ServerBuyPass(AtStop, FGuid::NewGuid());
}

void UGFFerryRiderComponent::ServerRequestTrip_Implementation(FName FromStop, FGuid RequestId)
{
	AGFFerryDirector* Director = nullptr;
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AGFFerryDirector> It(World); It; ++It)
		{
			Director = *It;
			break;
		}
	}
	if (!Director)
	{
		LastCode = FGFFerryCodes::UnknownStop;
		return;
	}
	LastCode = Director->RequestTrip(Cast<APawn>(GetOwner()), FromStop, RequestId).Code;
}

void UGFFerryRiderComponent::ServerBuyPass_Implementation(FName AtStop, FGuid RequestId)
{
	AGFFerryDirector* Director = nullptr;
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AGFFerryDirector> It(World); It; ++It)
		{
			Director = *It;
			break;
		}
	}
	if (!Director)
	{
		LastCode = FGFFerryCodes::UnknownStop;
		return;
	}
	LastCode = Director->BuyPass(Cast<APawn>(GetOwner()), AtStop, RequestId).Code;
}

void UGFFerryRiderComponent::BeginFlightProtection()
{
	AActor* Owner = GetOwner();
	bInFlight = true;
	if (!Owner || bFlightProtected)
	{
		return;
	}
	bDamageWasEnabled = Owner->CanBeDamaged();
	bAddedProtectionTag = !FGFProtectedActor::Is(Owner);
	if (bAddedProtectionTag)
	{
		Owner->Tags.Add(FGFProtectedActor::Tag);
	}
	Owner->SetCanBeDamaged(false);
	bFlightProtected = true;
}

void UGFFerryRiderComponent::EndFlightProtection()
{
	AActor* Owner = GetOwner();
	if (Owner && bFlightProtected)
	{
		if (bAddedProtectionTag)
		{
			Owner->Tags.Remove(FGFProtectedActor::Tag);
		}
		Owner->SetCanBeDamaged(bDamageWasEnabled);
	}
	bFlightProtected = false;
	bAddedProtectionTag = false;
	bInFlight = false;
}

void UGFFerryRiderComponent::BeginSeat()
{
	SeatAnim = FGFFerryArt::SeatAnim();
	ApplySeatPose();
}

void UGFFerryRiderComponent::EndSeat()
{
	SeatAnim = NAME_None;
	ApplySeatPose();
}

void UGFFerryRiderComponent::OnRep_SeatAnim()
{
	ApplySeatPose();
}

void UGFFerryRiderComponent::ApplySeatPose()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	UGFSimpleAnimComponent* Simple = Character->FindComponentByClass<UGFSimpleAnimComponent>();
	if (SeatAnim.IsNone())
	{
		if (Simple)
		{
			// PauseFor(0) does nothing. A tiny hold expires on the next tick and locomotion starts again.
			Simple->PauseFor(KINDA_SMALL_NUMBER);
		}
		return;
	}

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (UAnimSequenceBase* Clip = Mesh ? FGFAnimMap::Load(SeatAnim) : nullptr)
	{
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->PlayAnimation(Clip, true);
	}
	if (Simple)
	{
		Simple->PauseFor(GFFerryRiderPrivate::SeatHoldSeconds);
	}
}
