#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GFCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UGFInventoryComponent;
class AGFVoxelWorld;
struct FInputActionValue;

/**
 * Her character. Third-person by default (spring arm ~450 cm, pitch ~-20, FOV 90 per ASSET_BRIEFS),
 * V toggles first-person. LMB removes a block, RMB places the selected block, wheel cycles the hotbar.
 * Input assets are optional: if none are assigned, a default mapping is built in C++ at runtime.
 */
UCLASS()
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

	UFUNCTION(BlueprintCallable, Category = "GF|Camera")
	void SetFirstPerson(bool bEnable);

	UFUNCTION(BlueprintPure, Category = "GF|Camera")
	bool IsFirstPerson() const { return bFirstPerson; }

	/** Max reach for building, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Build")
	float BuildReach = 800.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Build")
	FName SelectedBlock;

	/** Asks the server (through AGFPlayerState) to place SelectedBlock where the camera points. The inventory lives on the player state. */
	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	bool TryPlaceBlock();

	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	bool TryRemoveBlock();

	UFUNCTION(BlueprintCallable, Category = "GF|Build")
	void CycleBlock(int32 Direction);

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
