#include "GFCrystalAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"

UGFCrystalAttributeSet::UGFCrystalAttributeSet()
{
	CrystalCharge.SetBaseValue(100.f);
	CrystalCharge.SetCurrentValue(100.f);
	CrystalMax.SetBaseValue(100.f);
	CrystalMax.SetCurrentValue(100.f);
	CrystalRegen.SetBaseValue(25.f);
	CrystalRegen.SetCurrentValue(25.f);
}

void UGFCrystalAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UGFCrystalAttributeSet, CrystalCharge, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGFCrystalAttributeSet, CrystalMax, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UGFCrystalAttributeSet, CrystalRegen, COND_OwnerOnly, REPNOTIFY_Always);
}

void UGFCrystalAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetCrystalChargeAttribute())
	{
		const float Max = GetCrystalMax() > 0.f ? GetCrystalMax() : NewValue;
		NewValue = FMath::Clamp(NewValue, 0.f, Max);
	}
}

void UGFCrystalAttributeSet::OnRep_CrystalCharge(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGFCrystalAttributeSet, CrystalCharge, OldValue);
}

void UGFCrystalAttributeSet::OnRep_CrystalMax(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGFCrystalAttributeSet, CrystalMax, OldValue);
}

void UGFCrystalAttributeSet::OnRep_CrystalRegen(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UGFCrystalAttributeSet, CrystalRegen, OldValue);
}
