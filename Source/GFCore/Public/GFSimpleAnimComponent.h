#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFSimpleAnimComponent.generated.h"

class USkeletalMeshComponent;

/**
 * Idle / move on a skeletal mesh without an AnimBP. Each tick it reads the owner's horizontal speed and plays
 * MoveAnim looped above MoveSpeed (20 cm/s), otherwise IdleAnim looped. The clip changes only when that choice changes.
 * Single-node playback (SetAnimationMode AnimationSingleNode, PlayAnimation).
 *
 * Emotes and other one-shots: call PauseFor with the clip's length (UGFSocialComponent does this). Looping one-shots
 * are held for that long; a non-looping clip is also left alone until it stops, even without a pause. Then idle or
 * move starts again.
 */
UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFCORE_API UGFSimpleAnimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFSimpleAnimComponent();

	/** DT_AnimMap key. None skips that state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Anim")
	FName IdleAnim;

	/** DT_AnimMap key played while moving. None means "keep idling". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Anim")
	FName MoveAnim;

	/** Horizontal speed (cm/s) above which MoveAnim plays. */
	static constexpr float DefaultMoveSpeed = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Anim", meta = (ClampMin = "0"))
	float MoveSpeed = DefaultMoveSpeed;

	/**
	 * Stop replacing the mesh animation for Seconds.
	 * Call this right after another system plays a one-shot on the art mesh (PlayAnimation / single-node).
	 * A later call replaces the remaining time. Zero or less does nothing. When the time runs out, idle or move
	 * starts again even if the speed didn't change.
	 */
	UFUNCTION(BlueprintCallable, Category = "GF|Anim")
	void PauseFor(float Seconds);

	/** Copies the asset-map keys and restarts the choice on the next tick. */
	void SetAnims(FName InIdle, FName InMove);

	/** MoveAnim when Speed2D is above MoveSpeed and MoveAnim is set; otherwise IdleAnim. */
	static FName ChooseAnim(float Speed2D, FName InIdle, FName InMove, float InMoveSpeed);

	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	USkeletalMeshComponent* FindArtMesh() const;

	FName CurrentKey = NAME_None;
	float PauseRemaining = 0.f;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
	TSet<FName> MissingClips;
};
