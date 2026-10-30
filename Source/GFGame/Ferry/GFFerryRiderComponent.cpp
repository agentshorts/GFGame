#include "GFFerryRiderComponent.h"
#include "GFAnimMap.h"
#include "GFFerryDirector.h"
#include "GFFerryRules.h"
#include "GFProtectedActor.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

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
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimSequenceBase* Clip = Mesh ? FGFAnimMap::Load(SeatAnim) : nullptr;
	if (!Mesh || !Clip)
	{
		return;
	}
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->PlayAnimation(Clip, true);
}

void UGFFerryRiderComponent::EndSeat()
{
	SeatAnim = NAME_None;
}
