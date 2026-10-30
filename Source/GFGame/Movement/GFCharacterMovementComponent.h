#pragma once

// Server-authoritative stamina on the player character.
// Sprint and dash ride FSavedMove_Character custom flags (FLAG_Custom_0, FLAG_Custom_1) so the server
// applies the same presses the client predicted. The pool is stepped inside PerformMovement, which is
// what a correction replays. A replicated stamina value pulls the owner back when the two diverge.
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
	virtual void PerformMovement(float DeltaTime) override;
	virtual float GetMaxSpeed() const override;
	virtual float GetGravityZ() const override;

	/** Copies BaseRunSpeed onto MaxWalkSpeed. Does not include the sprint multiplier. */
	void ApplyBaseRunSpeed();

	void SetSprintHeld(bool bHeld);
	void NotifyDashPressed();

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

	const UGFMovementTuning& GetTuning() const;

	void RefreshKindleOverlaps();

	/** Saved-move replay. Not for gameplay code. */
	void SetDashFromReplay(bool bDash) { bDashThisMove = bDash; }
	void RestoreSim(const FGFStaminaState& InSim) { Sim = InSim; }
	void SetReplayCamera(const FVector& Forward);
	bool WantsSprint() const { return bWantsToSprint; }
	bool WasDashRecorded() const { return bDashRecorded; }
	void ClearDashRecorded() { bDashRecorded = false; }
	const FGFStaminaState& GetSimAtMoveStart() const { return SimAtMoveStart; }
	const FVector& GetLastCameraForward() const { return LastCameraForward; }

protected:
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

	UFUNCTION()
	void OnRep_AuthoritativeStamina();

	UFUNCTION()
	void OnRep_StaminaGate();

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

private:
	void RefreshStaminaGate();
	void ApplyStaminaGate();
	FVector LiveCameraForward() const;

	FGFStaminaState Sim;
	FGFStaminaState SimAtMoveStart;
	FVector LastCameraForward = FVector::ForwardVector;
	FVector ReplayCameraForward = FVector::ForwardVector;
	bool bUseReplayCamera = false;
	bool bWantsToSprint = false;
	bool bDashThisMove = false;
	bool bDashRecorded = false;
	bool bLocalInsideKindle = false;
	bool bStaminaEnabled = true;
	bool bGotAuthoritativeStamina = false;
};
