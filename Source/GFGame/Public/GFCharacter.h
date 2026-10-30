#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GFCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UGFInventoryComponent;
class UGFSimpleAnimComponent;
class UGFFerryRiderComponent;
class AGFVoxelWorld;
struct FInputActionValue;

/**
 * The player character. Third-person by default (spring arm ~450 cm, pitch ~-20, FOV 90 per ASSET_BRIEFS),
 * V toggles first-person. LMB removes a block, RMB places the selected block, wheel cycles the hotbar.
 * Input assets are optional: if none are assigned, a default mapping is built in C++ at runtime.
 */
UCLASS(Config = Game)
class GFGAME_API AGFCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AGFCharacter();

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

	UFUNCTION(BlueprintCallable, Category = "GF|Camera")
	void SetFirstPerson(bool bEnable);

	UFUNCTION(BlueprintPure, Category = "GF|Camera")
	bool IsFirstPerson() const { return bFirstPerson; }

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

	/** Asks the server (through AGFPlayerState) to place SelectedBlock where the camera points. The inventory lives on the player state. */
	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	bool TryPlaceBlock();

	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	bool TryRemoveBlock();

	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	void CycleBlock(int32 Direction);

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

protected:
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void BuildDefaultInput();
	AGFVoxelWorld* FindVoxelWorld() const;
	bool TraceBlock(FHitResult& OutHit) const;

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnToggleView(const FInputActionValue& Value);
	void OnPlace(const FInputActionValue& Value);
	void OnRemove(const FInputActionValue& Value);
	void OnHotbar(const FInputActionValue& Value);

	bool bFirstPerson = false;
};
