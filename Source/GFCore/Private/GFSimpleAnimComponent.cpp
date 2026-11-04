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
	// After gameplay events (an emote claimed earlier this frame) so expiries and restores land the same frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGFSimpleAnimComponent::ClaimClip(EGFAnimOwner Owner, FName Key, bool bLoop, float Seconds, float PlayRate)
{
	if (Claims.Claim(Owner, Key, bLoop, Seconds, PlayRate))
	{
		ApplyTopClaim();
	}
}

void UGFSimpleAnimComponent::ReleaseClaim(EGFAnimOwner Owner)
{
	const bool bWasTop = Claims.TopOwner() == Owner;
	if (Claims.Release(Owner) && bWasTop)
	{
		ApplyTopClaim();
	}
}

bool UGFSimpleAnimComponent::ReleaseClaimKey(EGFAnimOwner Owner, FName Key)
{
	const bool bWasTop = Claims.TopOwner() == Owner;
	if (!Claims.ReleaseKey(Owner, Key))
	{
		return false;
	}
	if (bWasTop)
	{
		ApplyTopClaim();
	}
	return true;
}

bool UGFSimpleAnimComponent::RenewClaim(EGFAnimOwner Owner, float Seconds)
{
	return Claims.Renew(Owner, Seconds);
}

void UGFSimpleAnimComponent::SetClaimPlayRate(EGFAnimOwner Owner, float PlayRate)
{
	if (!Claims.SetPlayRate(Owner, PlayRate) || Claims.TopOwner() != Owner)
	{
		return;
	}
	if (USkeletalMeshComponent* Mesh = FindArtMesh())
	{
		if (AppliedSerial != 0 && !FMath::IsNearlyEqual(Mesh->GetPlayRate(), PlayRate, 0.01f))
		{
			Mesh->SetPlayRate(PlayRate);
		}
	}
}

FName UGFSimpleAnimComponent::GetTopKey() const
{
	const FGFAnimClaim* Top = Claims.Top();
	return Top ? Top->Key : NAME_None;
}

void UGFSimpleAnimComponent::ApplyTopClaim()
{
	const FGFAnimClaim* Top = Claims.Top();
	if (!Top)
	{
		if (AppliedSerial != 0)
		{
			// Back to locomotion: forget the key so idle or move starts again even if the speed didn't change.
			AppliedSerial = 0;
			CurrentKey = NAME_None;
		}
		return;
	}
	if (Top->Serial == AppliedSerial)
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
	USkeletalMeshComponent* Mesh = FindArtMesh();
	if (!Mesh)
	{
		return; // tried again on the next tick
	}
	AppliedSerial = Top->Serial;
	CurrentKey = NAME_None;
	UAnimSequenceBase* Clip = FGFAnimMap::Load(Top->Key);
	if (!Clip)
	{
		if (!MissingClips.Contains(Top->Key))
		{
			MissingClips.Add(Top->Key);
			UE_LOG(LogGF, Warning, TEXT("GFSimpleAnim: no clip for %s"), *Top->Key.ToString());
		}
		return;
	}
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->PlayAnimation(Clip, Top->bLoop);
	Mesh->SetPlayRate(Top->PlayRate);
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
	// Timed claims run out on servers too, so nothing piles up there.
	Claims.Tick(DeltaSeconds);
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

	if (Claims.Top())
	{
		// Someone above locomotion owns the mesh. Plays it if it's new (a restore after a release, or a claim that
		// arrived before the mesh had an asset).
		ApplyTopClaim();
		return;
	}
	ApplyTopClaim();

	USkeletalMeshComponent* Mesh = FindArtMesh();
	if (!Mesh)
	{
		return;
	}
	// A non-looping clip left on the mesh (a released one-shot, or one played without a claim) plays out. Ours are loops.
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
