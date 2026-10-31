#pragma once

// Player state that carries the crystal pool and the mining component. The hub game mode uses this.
// Tests that spawn AGFPlayerState directly stay on the base class.

#include "CoreMinimal.h"
#include "GFPlayerState.h"
#include "GFMagicPlayerState.generated.h"

class UAbilitySystemComponent;
class UGFCrystalAttributeSet;
class UGFMagicComponent;

UCLASS()
class GFMAGIC_API AGFMagicPlayerState : public AGFPlayerState
{
	GENERATED_BODY()

public:
	AGFMagicPlayerState();

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Magic")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Magic")
	TObjectPtr<UGFCrystalAttributeSet> Crystal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Magic")
	TObjectPtr<UGFMagicComponent> Magic;
};
