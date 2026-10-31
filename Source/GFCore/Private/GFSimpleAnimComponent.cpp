#include "GFSimpleAnimComponent.h"
#include "GFAnimMap.h"
#include "GFLog.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

UGFSimpleAnimComponent::UGFSimpleAnimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After gameplay events (an emote played earlier this frame) so PauseFor is already set.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGFSimpleAnimComponent::PauseFor(float Seconds)
{
	if (Seconds <= 0.f)
	{
		return;
	}
	PauseRemaining = Seconds;
	// The one-shot replaced the clip. Forget the locomotion key so it restarts when the pause ends.
	CurrentKey = NAME_None;
}

void UGFSimpleAnimComponent::SetAnims(FName InIdle, FName InMove)
{
	if (IdleAnim == InIdle && MoveAnim == InMove)
	{
		return;
	}
	IdleAnim = InIdle;
	MoveAnim = InMove;
	CurrentKey = NAME_None;
	MissingClips.Reset();
}

FName UGFSimpleAnimComponent::ChooseAnim(float Speed2D, FName InIdle, FName InMove, float InMoveSpeed)
{
	if (!InMove.IsNone() && Speed2D > InMoveSpeed)
	{
		return InMove;
	}
	return InIdle;
}

USkeletalMeshComponent* UGFSimpleAnimComponent::FindArtMesh() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(Owner);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh && !Mesh->bHiddenInGame && Mesh->GetSkeletalMeshAsset())
		{
			return Mesh;
		}
	}
	return nullptr;
}

void UGFSimpleAnimComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (const UWorld* World = GetWorld())
	{
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
	}

	const FVector Now = Owner->GetActorLocation();
	float Speed = Owner->GetVelocity().Size2D();
	// Actors moved with SetActorLocation (the giant) keep a zero velocity. A step of at least 1 cm still counts.
	if (Speed <= MoveSpeed && bHasLastLocation)
	{
		const float Step = (Now - LastLocation).Size2D();
		const float StepSpeed = Step / FMath::Max(DeltaSeconds, KINDA_SMALL_NUMBER);
		if (Step > 1.f && StepSpeed > MoveSpeed)
		{
			Speed = StepSpeed;
		}
	}
	LastLocation = Now;
	bHasLastLocation = true;

	if (PauseRemaining > 0.f)
	{
		PauseRemaining = FMath::Max(0.f, PauseRemaining - DeltaSeconds);
		if (PauseRemaining > 0.f)
		{
			return;
		}
	}

	USkeletalMeshComponent* Mesh = FindArtMesh();
	if (!Mesh)
	{
		return;
	}
	// A non-looping clip someone else started (or that PauseFor is shorter than) plays out. Ours are always loops.
	if (Mesh->IsPlaying() && !Mesh->AnimationData.bSavedLooping && Mesh->AnimationData.AnimToPlay)
	{
		const float Len = Mesh->AnimationData.AnimToPlay->GetPlayLength();
		if (Len <= 0.f || Mesh->GetPosition() < Len - 0.05f)
		{
			CurrentKey = NAME_None;
			return;
		}
	}

	const FName Want = ChooseAnim(Speed, IdleAnim, MoveAnim, MoveSpeed);
	if (Want == CurrentKey)
	{
		// Locomotion loops with a natural speed (Walk 87, Run 450 cm/s) follow the ground speed so the feet don't skate.
		if (!Want.IsNone() && Want == MoveAnim)
		{
			const float Rate = FGFAnimMap::PlayRateFor(Want, Speed);
			if (!FMath::IsNearlyEqual(Mesh->GetPlayRate(), Rate, 0.01f))
			{
				Mesh->SetPlayRate(Rate);
			}
		}
		return;
	}
	CurrentKey = Want;
	if (Want.IsNone())
	{
		return;
	}
	UAnimSequenceBase* Clip = FGFAnimMap::Load(Want);
	if (!Clip)
	{
		if (!MissingClips.Contains(Want))
		{
			MissingClips.Add(Want);
			UE_LOG(LogGF, Warning, TEXT("GFSimpleAnim: no clip for %s"), *Want.ToString());
		}
		return;
	}
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->PlayAnimation(Clip, true);
	Mesh->SetPlayRate(Want == MoveAnim ? FGFAnimMap::PlayRateFor(Want, Speed) : 1.f);
}
