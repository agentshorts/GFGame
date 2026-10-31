#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GFCrystalAttributeSet.generated.h"

// UE 5.8 no longer ships ATTRIBUTE_ACCESSORS. These four macros are the accessors.
#ifndef ATTRIBUTE_ACCESSORS
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)
#endif

/** Crystal pool. The component is the gameplay source of truth and mirrors these for GAS. */
UCLASS()
class GFMAGIC_API UGFCrystalAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UGFCrystalAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CrystalCharge, Category = "GF|Magic")
	FGameplayAttributeData CrystalCharge;
	ATTRIBUTE_ACCESSORS(UGFCrystalAttributeSet, CrystalCharge);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CrystalMax, Category = "GF|Magic")
	FGameplayAttributeData CrystalMax;
	ATTRIBUTE_ACCESSORS(UGFCrystalAttributeSet, CrystalMax);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CrystalRegen, Category = "GF|Magic")
	FGameplayAttributeData CrystalRegen;
	ATTRIBUTE_ACCESSORS(UGFCrystalAttributeSet, CrystalRegen);

protected:
	UFUNCTION()
	void OnRep_CrystalCharge(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_CrystalMax(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_CrystalRegen(const FGameplayAttributeData& OldValue);
};
