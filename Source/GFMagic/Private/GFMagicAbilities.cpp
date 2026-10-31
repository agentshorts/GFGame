#include "GFMagicAbilities.h"
#include "GFMagicComponent.h"

namespace
{
	UGFMagicComponent* MagicOf(const UGameplayAbility* Ability, const FGameplayAbilityActorInfo* Info)
	{
		if (Ability)
		{
			if (UGFMagicComponent* Magic = Cast<UGFMagicComponent>(Ability->GetCurrentSourceObject()))
			{
				return Magic;
			}
		}
		const AActor* Owner = (Info && Info->OwnerActor.IsValid()) ? Info->OwnerActor.Get() : nullptr;
		return Owner ? Owner->FindComponentByClass<UGFMagicComponent>() : nullptr;
	}
}

UGFGA_Field::UGFGA_Field()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UGFGA_Field::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (HasAuthority(&ActivationInfo))
	{
		if (UGFMagicComponent* Magic = MagicOf(this, ActorInfo))
		{
			Magic->AuthoritySetField(true);
		}
	}
}

void UGFGA_Field::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	if (HasAuthority(&ActivationInfo))
	{
		if (UGFMagicComponent* Magic = MagicOf(this, ActorInfo))
		{
			Magic->AuthoritySetField(false);
		}
	}
	if (IsActive())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

UGFGA_Clawcall::UGFGA_Clawcall()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UGFGA_Clawcall::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (HasAuthority(&ActivationInfo))
	{
		if (UGFMagicComponent* Magic = MagicOf(this, ActorInfo))
		{
			Magic->AuthorityClaw();
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UGFGA_ToggleElement::UGFGA_ToggleElement()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UGFGA_ToggleElement::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (HasAuthority(&ActivationInfo))
	{
		if (UGFMagicComponent* Magic = MagicOf(this, ActorInfo))
		{
			Magic->AuthorityToggle();
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UGFGA_Overcharge::UGFGA_Overcharge()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UGFGA_Overcharge::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (HasAuthority(&ActivationInfo))
	{
		if (UGFMagicComponent* Magic = MagicOf(this, ActorInfo))
		{
			Magic->AuthorityOvercharge();
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
