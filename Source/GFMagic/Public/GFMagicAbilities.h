#pragma once

// Thin GAS wrappers. The server does the work. A predicted copy on the client does not spend a charge twice.

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GFMagicAbilities.generated.h"

UCLASS()
class GFMAGIC_API UGFGA_Field : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGFGA_Field();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) override;
};

UCLASS()
class GFMAGIC_API UGFGA_Clawcall : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGFGA_Clawcall();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class GFMAGIC_API UGFGA_ToggleElement : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGFGA_ToggleElement();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class GFMAGIC_API UGFGA_Overcharge : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGFGA_Overcharge();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
