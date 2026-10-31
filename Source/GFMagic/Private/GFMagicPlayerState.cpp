#include "GFMagicPlayerState.h"
#include "GFCrystalAttributeSet.h"
#include "GFMagicComponent.h"
#include "AbilitySystemComponent.h"

AGFMagicPlayerState::AGFMagicPlayerState()
{
	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	Crystal = CreateDefaultSubobject<UGFCrystalAttributeSet>(TEXT("Crystal"));
	Magic = CreateDefaultSubobject<UGFMagicComponent>(TEXT("Magic"));
}

void AGFMagicPlayerState::BeginPlay()
{
	Super::BeginPlay();
	if (AbilitySystem && Crystal && !AbilitySystem->GetSet<UGFCrystalAttributeSet>())
	{
		AbilitySystem->AddSpawnedAttribute(Crystal);
	}
	if (Magic && GetPawn())
	{
		Magic->BindAvatar(GetPawn());
	}
}
