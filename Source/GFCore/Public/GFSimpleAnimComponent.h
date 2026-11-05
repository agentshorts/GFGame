#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFAnimOwnerStack.h"
#include "GFSimpleAnimComponent.generated.h"

class USkeletalMeshComponent;

/**
 * Idle / move on a skeletal mesh without an AnimBP. Each tick it reads the owner's horizontal speed and plays
 * MoveAnim looped above MoveSpeed (20 cm/s), otherwise IdleAnim looped. The clip changes only when that choice changes.
 * Single-node playback (SetAnimationMode AnimationSingleNode, PlayAnimation). MoveAnim plays at ground speed / the
 * row's NaturalSpeedCms (DT_AnimMap) when it has one, so Walk and Run don't skate; everything else plays at 1.0.
 *
 * Other clips (ferry seat, traversal, fist scope, emotes, mining, Beat Hop) go through ClaimClip / ReleaseClaim instead of
 * playing on the mesh themselves. Claims sit in a priority stack keyed by owner (ferry seat > mantling > wall run >
 * climbing > fist scope > emote > mining > Beat Hop > locomotion; GFAnimOwnerStack.h):
 * only the top claim plays, a lower claim waits underneath, and releasing the top restores the next one (or locomotion).
 * A non-looping clip left on the mesh with no claim still plays out before idle or move starts again.
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
	 * Owner wants Key on the art mesh. Plays it now when Owner is the top claim; otherwise it waits until the owners
	 * above release. Seconds > 0 lets the claim expire on its own (emotes, Beat Hop's per-tick lease); zero or less holds
	 * it until ReleaseClaim. Claiming again replaces Owner's clip and restarts it. Locomotion and None are ignored.
	 */
	void ClaimClip(EGFAnimOwner Owner, FName Key, bool bLoop, float Seconds = 0.f, float PlayRate = 1.f);

	/** Drops Owner's claim. When it was on top, the next owner's clip plays again (or idle / move on the next tick). */
	void ReleaseClaim(EGFAnimOwner Owner);

	/** Drops Owner's claim only while it plays Key. Returns true when it did. */
	bool ReleaseClaimKey(EGFAnimOwner Owner, FName Key);

	/** The key Owner is claiming, on top or buried, or None. */
	FName GetClaimKey(EGFAnimOwner Owner) const
	{
		const FGFAnimClaim* C = Claims.Find(Owner);
		return C ? C->Key : NAME_None;
	}

	/** Keeps a timed claim alive for Seconds more. Returns false when Owner holds nothing. */
	bool RenewClaim(EGFAnimOwner Owner, float Seconds);

	/** Changes Owner's play rate; the mesh follows only while Owner is on top. */
	void SetClaimPlayRate(EGFAnimOwner Owner, float PlayRate);

	/** Who drives the mesh right now. Locomotion when nobody has a claim. */
	EGFAnimOwner GetTopOwner() const { return Claims.TopOwner(); }

	/** The top claim's key, or None when locomotion drives the mesh. */
	FName GetTopKey() const;

	bool HasClaim(EGFAnimOwner Owner) const { return Claims.Has(Owner); }

	/** Copies the asset-map keys and restarts the choice on the next tick. */
	void SetAnims(FName InIdle, FName InMove);

	/** MoveAnim when Speed2D is above MoveSpeed and MoveAnim is set; otherwise IdleAnim. */
	static FName ChooseAnim(float Speed2D, FName InIdle, FName InMove, float InMoveSpeed);

	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The mesh this component animates: the owner's first visible skeletal mesh that has an asset. */
	USkeletalMeshComponent* FindArtMesh() const;

private:
	/** Plays the top claim on the art mesh when it changed since the last call. No claim hands back to locomotion. */
	void ApplyTopClaim();

	FGFAnimOwnerStack Claims;
	/** Serial of the claim last put on the mesh. 0: locomotion (or nothing) owns it. */
	uint32 AppliedSerial = 0;
	FName CurrentKey = NAME_None;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
	TSet<FName> MissingClips;
};
