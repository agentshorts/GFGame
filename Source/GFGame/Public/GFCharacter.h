#pragma once

#include "CoreMinimal.h"
#include "Aim/GFAimSim.h"
#include "GameFramework/Character.h"
#include "InputCoreTypes.h"
#include "GFCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UGFInventoryComponent;
class UGFSimpleAnimComponent;
class UGFFootIKComponent;
class UGFFerryRiderComponent;
class AGFVoxelWorld;
class UGFCharacterMovementComponent;
class UGFMagicComponent;
class USoundBase;
struct FInputActionValue;

/**
 * The player character. Third-person over the shoulder by default: spring arm 300 cm, socket (0, 60, 20),
 * pitch -20, FOV 90, camera lag 12. V toggles a plain first-person view. Right mouse places a block while a
 * block is selected, and aims otherwise. Holding left mouse (or the right trigger) with fists or a block
 * raises the sparkfield. A short tap still punches, or picks up a spell-proof build. Shoulder swap is middle
 * mouse. Q still swings the grapple. C calls the claw, R swaps Spark and the faction element, T overcharges.
 * Shift sprints, Ctrl dashes, E yanks. Space can hop three times before landing. MaxWalkSpeed stays the base run.
 * X talks to the nearest villager or elder within reach. The server checks that reach again.
 * Input assets are optional: if none are assigned, a default mapping is built in C++ at runtime.
 */
UCLASS(Config = Game)
class GFGAME_API AGFCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AGFCharacter(const FObjectInitializer& ObjectInitializer);

	UGFCharacterMovementComponent* GetGFMovement() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Camera")
	TObjectPtr<UCameraComponent> ThirdPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** Server RPCs for a ferry trip and a lifetime pass. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	TObjectPtr<UGFFerryRiderComponent> FerryRider;

	/** Idle / move on the skeletal mesh. Clips come from the Player.Base asset-map row. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Anim")
	TObjectPtr<UGFSimpleAnimComponent> SimpleAnim;

	/** Control Rig foot plant. Binds once the skeletal mesh is on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Anim")
	TObjectPtr<UGFFootIKComponent> FootIK;

	UFUNCTION(BlueprintCallable, Category = "GF|Camera")
	void SetFirstPerson(bool bEnable);

	UFUNCTION(BlueprintPure, Category = "GF|Camera")
	bool IsFirstPerson() const { return bFirstPerson; }

	/** Middle mouse. Q belongs to the grapple. */
	static FKey DefaultShoulderSwapKey();

	const FGFCrosshairSettings& GetCrosshairSettings() const { return Crosshair; }
	const FGFAimUserSettings& GetAimUserSettings() const { return AimUser; }
	void SetCrosshairSettings(const FGFCrosshairSettings& In);
	void SetAimUserSettings(const FGFAimUserSettings& In);
	void SetAimPanelOpen(bool bOpen) { bAimPanelOpen = bOpen; }

	EGFAimHeldKind GetAimHeldKind() const { return AimState.Held; }
	bool IsAdsIntent() const { return AimState.bAdsIntent; }
	const FGFAimState& GetAimState() const { return AimState; }
	float GetAimLookScale() const { return AimState.LookScale; }
	bool WantsScopeOverlay() const { return GFWantsScopeOverlay(AimState); }
	bool WantsFistVignette() const { return AimState.bFistScope; }
	bool IsCrosshairSpreadActive() const;
	/** The nine hotbar slots (AGFPlayerState::GetHotbarSlots). None is an empty slot. Fists alone without a player state. */
	TArray<FName> GetHotbarIds() const;
	/** Number keys and the wheel land here. Out-of-range slots wrap. An empty slot means bare hands. */
	void SelectHotbarSlot(int32 Slot);
	int32 GetSelectedHotbarSlot() const { return SelectedHotbarSlot; }

	/** Future weapons and abilities call this. Wheel-cycling a block clears the override. */
	void EquipAimHeld(EGFAimHeldKind Kind, EGFWeaponProfile Profile);
	void RequestAimWeaponSwap();
	void RequestAimAbility();

	/** Max reach for building, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Build")
	float BuildReach = 800.f;

	/**
	 * Tallest ledge the player walks up without jumping, cm, applied to CharacterMovement MaxStepHeight.
	 * Slightly over one block (GFBlockSize, 100 cm) so one-block voxel steps, such as Big Grumble's stairwells,
	 * are walkable. Tune in Blueprint defaults or [/Script/GFGame.GFCharacter] in DefaultGame.ini.
	 * Capsule limit: UCharacterMovementComponent::StepUp gives up when the hit is above the top of the capsule's
	 * cylinder (HalfHeight - Radius above its centre), i.e. 2*HalfHeight - Radius above the feet: 136 cm for the
	 * 40/88 capsule. Keep this under that if the capsule shrinks.
	 */
	UPROPERTY(EditDefaultsOnly, Config, BlueprintReadOnly, Category = "GF|Movement", meta = (ClampMin = "0", UIMin = "0"))
	float PlayerMaxStepHeight = 105.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Build")
	FName SelectedBlock;

	/** Highlighted hotbar slot, 0..8. Local, like SelectedBlock. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Build")
	int32 SelectedHotbarSlot = 0;

	/** Asks the server (through AGFPlayerState) to place SelectedBlock where the camera points. The inventory lives on the player state. */
	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	bool TryPlaceBlock();

	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	bool TryRemoveBlock();

	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	void CycleBlock(int32 Direction);

	/** Nearest IGFInteractable within the townsfolk reach, or null. */
	AActor* FindNearestInteractable() const;

	/** Local press. Sends ServerInteract when this pawn is not the authority. */
	void RequestInteract();

	/** Same as the press, for a chosen target. The server drops it when the pawn is out of reach. */
	void RequestInteractWith(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	/** Builds the default mapping when no input assets are assigned. */
	void BuildDefaultInput();

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// Optional input assets (thin BP / data). Null = built in C++.
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> LookAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> JumpAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> PlaceAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> RemoveAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> ToggleViewAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> HotbarAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> SprintAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> DashAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> GrappleSwingAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> GrappleYankAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> ShoulderSwapAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> ClawAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> ToggleElementAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> OverchargeAction;
	UPROPERTY(EditAnywhere, Category = "GF|Input") TObjectPtr<UInputAction> InteractAction;

	UFUNCTION(Server, Reliable)
	void ServerApplyAimDamage(AActor* Target, float Damage, bool bFists);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostInitializeComponents() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	AGFVoxelWorld* FindVoxelWorld() const;
	UGFMagicComponent* FindMagic() const;
	void SendMagicAim();
	bool AimedCellNeedsHands() const;
	bool TraceCameraAim(FHitResult& OutHit, FVector& OutAimPoint) const;
	bool TraceBlock(FHitResult& OutHit) const;
	FVector AimMuzzleLocation() const;
	void ApplyAimShot(const FGFAimStepOut& Shot);

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnToggleView(const FInputActionValue& Value);
	void OnPlace(const FInputActionValue& Value);
	void OnPlaceReleased(const FInputActionValue& Value);
	void OnRemove(const FInputActionValue& Value);
	void OnRemoveReleased(const FInputActionValue& Value);
	void OnClaw(const FInputActionValue& Value);
	void OnToggleElement(const FInputActionValue& Value);
	void OnOvercharge(const FInputActionValue& Value);
	void OnHotbar(const FInputActionValue& Value);
	/** Keeps SelectedBlock on the selected slot when the slot's contents change (an item runs out, a new one arrives). */
	void RefreshHotbarSelection();
	/** What the selected slot held when SelectedBlock was last set from it. */
	FName HotbarSlotSeen;
	void OnSprint(const FInputActionValue& Value);
	void OnSprintStopped(const FInputActionValue& Value);
	void OnDash(const FInputActionValue& Value);
	void OnGrappleSwing(const FInputActionValue& Value);
	void OnGrappleSwingStopped(const FInputActionValue& Value);
	void OnGrappleYank(const FInputActionValue& Value);
	void OnShoulderSwap(const FInputActionValue& Value);
	void OnInteractPressed(const FInputActionValue& Value);

	void RefreshAimFromProfile();
	void CommitAimLook();
	void SyncAimContext();
	void PushAim(EGFAimEvent Event);
	void ApplyAimView();
	void ApplyShoulderOffset();
	void UpdateCameraFade();
	void UpdateFistPose();
	/** Plays a mining clip (GFMiningAnim keys) on the local body; false when the ferry seat or fist scope owns it or the clip is missing. */
	bool PlayMiningClip(FName Key, bool bLoop);
	/** Tap to mine: Mine_Dig or Mine_Punch from where the block trace lands. */
	void PlayMineTap();
	/** LMB hold: Mine_Field_Start once past the tap time, then Mine_Field_Loop until release. */
	void UpdateMiningField(float DeltaSeconds);
	/** Release: Mine_Field_End when the field pose is up. */
	void EndMiningField();
	void PlayFistScopeCue();
	bool IsAimingDownASnout() const;

	bool bFirstPerson = false;
	bool bHeldOverride = false;
	bool bAimPanelOpen = false;
	bool bAimProfileReady = false;
	bool bFistPose = false;
	/** True only after Fist_Scope actually replaced locomotion, so a missing clip does not pause the walk. */
	bool bFistClipHeld = false;
	bool bMagicHeld = false;
	enum class EGFMiningPose : uint8 { None, FieldStart, FieldLoop };
	EGFMiningPose MiningPose = EGFMiningPose::None;
	/** Time left on Mine_Field_Start before the loop takes over. */
	float MiningPoseSeconds = 0.f;
	/** The field clip was tried for this press (so a blocked or missing clip is not retried every frame). */
	bool bMiningFieldTried = false;
	float MagicPressSeconds = 0.f;
	float MagicAimElapsed = 0.f;
	bool bFistScopeSoundSearched = false;
	int32 AimProfileRevision = INDEX_NONE;
	TWeakObjectPtr<USoundBase> FistScopeSound;
	float ArmZoom = GFAim::ArmLength;

	FGFCrosshairSettings Crosshair;
	FGFAimUserSettings AimUser;
	FGFAimState AimState;
	FGFAimTuning AimTuning;
};
