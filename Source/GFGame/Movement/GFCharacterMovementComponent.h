#pragma once

// Server-authoritative stamina on the player character.
// Sprint, dash, swing, and yank ride FSavedMove_Character custom flags
// (FLAG_Custom_0 through FLAG_Custom_3) so the server applies the same presses the client predicted.
// The pool and the traversal step run inside PerformMovement, which is what a correction replays.
// A replicated stamina value pulls the owner back when the two diverge.
// MaxWalkSpeed stays the tuned base run speed. Sprint only changes GetMaxSpeed.

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GFStaminaSim.h"
#include "GFCharacterMovementComponent.generated.h"

class UGFMovementTuning;

UCLASS()
class GFGAME_API UGFCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGFCharacterMovementComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void PerformMovement(float DeltaTime) override;
	virtual float GetMaxSpeed() const override;
	virtual float GetGravityZ() const override;

	/** Copies BaseRunSpeed onto MaxWalkSpeed. Does not include the sprint multiplier. */
	void ApplyBaseRunSpeed();

	/** Copies jump speed and the airtime jump count from the tuning asset. */
	void ApplyJumpTuning();

	void SetSprintHeld(bool bHeld);
	void NotifyDashPressed();
	void SetGrappleSwingHeld(bool bHeld);
	void NotifyGrappleYank();

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	float GetStamina() const { return Sim.Stamina; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	float GetMaxStamina() const;

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsStaminaEnabled() const { return bStaminaEnabled; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsSprinting() const { return Sim.bSprinting; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsDashing() const { return Sim.bDashing; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsGripping() const { return Sim.bGripping; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsWallRunning() const { return Sim.bWallRunning; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsSwiveling() const { return Sim.bSwiveling; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsSwinging() const { return Sim.bSwinging; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsYanking() const { return Sim.bYanking; }

	/** Ballistic ground point. False when the marker should stay hidden or the trace misses. */
	bool GetLandingMarker(FVector& OutPoint) const;

	const UGFMovementTuning& GetTuning() const;

	void RefreshKindleOverlaps();

	/** Saved-move replay. Not for gameplay code. */
	void SetDashFromReplay(bool bDash) { bDashThisMove = bDash; }
	void RestoreSim(const FGFStaminaState& InSim) { Sim = InSim; }
	void SetReplayCamera(const FVector& Forward);
	void SetReplayTraversal(bool bJump, bool bYank, bool bSwing, const FGFTraversalContact& Contact);
	bool WantsSprint() const { return bWantsToSprint; }
	bool WantsGrappleSwing() const { return bWantsGrappleSwing; }
	bool WasDashRecorded() const { return bDashRecorded; }
	bool WasYankRecorded() const { return bYankRecorded; }
	bool WasJumpUsed() const { return bJumpUsedThisMove; }
	void ClearDashRecorded() { bDashRecorded = false; }
	void ClearYankRecorded() { bYankRecorded = false; }
	const FGFStaminaState& GetSimAtMoveStart() const { return SimAtMoveStart; }
	const FVector& GetLastCameraForward() const { return LastCameraForward; }
	const FGFTraversalContact& GetContactAtMoveStart() const { return ContactAtMoveStart; }

protected:
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta) override;

	UFUNCTION()
	void OnRep_AuthoritativeStamina();

	UFUNCTION()
	void OnRep_StaminaGate();

	UFUNCTION()
	void OnRep_FlipTime();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GF|Movement")
	TObjectPtr<UGFMovementTuning> TuningAsset;

	UPROPERTY(ReplicatedUsing = OnRep_AuthoritativeStamina)
	float AuthoritativeStamina = 200.f;

	/**
	 * 0 until the server has said. 1 on, 2 off.
	 * A bool would not replicate when the server value matches the default, so an opted-in mini-game
	 * (server on, client map-guess off) would stay off. Zero always differs from the server's choice.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_StaminaGate)
	uint8 ReplicatedStaminaGate = 0;

	UPROPERTY(Replicated)
	bool bInsideKindleRing = false;

	/** Jump-3 flip seconds left. Simulated proxies play this. The owner predicts Sim.FlipTimeLeft instead. */
	UPROPERTY(ReplicatedUsing = OnRep_FlipTime)
	float ReplicatedFlipTimeLeft = 0.f;

private:
	void RefreshStaminaGate();
	void ApplyStaminaGate();
	FVector LiveCameraForward() const;
	void GatherLiveContact(FGFTraversalContact& Contact);
	void ApplyFlipPose(float TimeLeft);

	FGFStaminaState Sim;
	FGFStaminaState SimAtMoveStart;
	FGFTraversalContact ContactAtMoveStart;
	FGFTraversalContact ReplayContact;
	FVector LastCameraForward = FVector::ForwardVector;
	FVector ReplayCameraForward = FVector::ForwardVector;
	bool bUseReplayCamera = false;
	bool bUseReplayTraversal = false;
	bool bWantsToSprint = false;
	bool bWantsGrappleSwing = false;
	bool bDashThisMove = false;
	bool bDashRecorded = false;
	bool bYankThisMove = false;
	bool bYankRecorded = false;
	bool bJumpUsedThisMove = false;
	bool bReplayJump = false;
	bool bReplayYank = false;
	bool bReplaySwing = false;
	bool bPendingDashHit = false;
	float StepDeltaSeconds = 0.f;
	FVector LocationAtStepStart = FVector::ZeroVector;
	float PendingSpeedBefore = 0.f;
	float PendingSpeedAfter = 0.f;
	float DashPlanarSpeed = 0.f;
	FVector PendingImpactNormal = FVector::UpVector;
	bool bLocalInsideKindle = false;
	bool bStaminaEnabled = true;
	bool bGotAuthoritativeStamina = false;
	float ProxyFlipTimeLeft = 0.f;
	FRotator MeshRestRotation = FRotator::ZeroRotator;
	bool bMeshRestCaptured = false;
};
