#include "GFMagicComponent.h"
#include "GFMagicAbilities.h"
#include "GFMagicActors.h"
#include "GFMagicTables.h"
#include "GFMagicTags.h"
#include "GFBlockIntegritySubsystem.h"
#include "GFCrystalAttributeSet.h"
#include "GFInventoryComponent.h"
#include "GFLog.h"
#include "GFPlayerState.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

UGFMagicComponent::UGFMagicComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UGFMagicComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGFMagicComponent, bFieldHeld);
	DOREPLIFETIME(UGFMagicComponent, Element);
	DOREPLIFETIME(UGFMagicComponent, AimPoint);
	DOREPLIFETIME(UGFMagicComponent, ClawForm);
	DOREPLIFETIME_CONDITION(UGFMagicComponent, Haul, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFMagicComponent, OwnedUpgrades, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFMagicComponent, ClawCharges, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFMagicComponent, CrystalCharge, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFMagicComponent, GrantedDrops, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFMagicComponent, bOvercharge, COND_OwnerOnly);
}

bool UGFMagicComponent::IsAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

void UGFMagicComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsAuthority())
	{
		return;
	}
	if (const FGFMiningSpellRow* Claw = FGFMagicCatalog::Spell(TEXT("Clawcall")))
	{
		ClawCharges = Claw->MaxCharges;
	}
	CrystalCharge = FGFMagicCatalog::ChargeMax();
	if (const AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner()))
	{
		ClawForm = FGFMagicRules::ClawForm(PS->FactionId);
	}
	PushAttributes();
}

void UGFMagicComponent::BindAvatar(APawn* Pawn)
{
	AvatarPawn = Pawn;
	if (UAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		ASC->InitAbilityActorInfo(GetOwner(), Pawn);
		if (IsAuthority())
		{
			GrantAbilities();
			PushAttributes();
		}
	}
}

UAbilitySystemComponent* UGFMagicComponent::GetAbilitySystem() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
}

FVector UGFMagicComponent::CastOrigin() const
{
	if (const APawn* Pawn = AvatarPawn.Get())
	{
		return Pawn->GetActorLocation();
	}
	if (const APlayerState* PS = Cast<APlayerState>(GetOwner()))
	{
		if (const APawn* Pawn = PS->GetPawn())
		{
			return Pawn->GetActorLocation();
		}
	}
	return ManualOrigin;
}

FName UGFMagicComponent::ContributorKey() const
{
	if (const AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner()))
	{
		return FName(*PS->PlayerKey);
	}
	return NAME_None;
}

int32 UGFMagicComponent::HaulCount(FName ItemId) const
{
	int32 Count = 0;
	for (const FGFItemStack& Stack : Haul)
	{
		if (Stack.ItemId == ItemId)
		{
			Count += Stack.Count;
		}
	}
	return Count;
}

int32 UGFMagicComponent::BagTotal() const
{
	const AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner());
	if (!PS || !PS->Inventory)
	{
		return 0;
	}
	int32 Total = 0;
	for (const FGFItemStack& Stack : PS->Inventory->GetInventory().Items)
	{
		Total += Stack.Count;
	}
	return Total;
}

float UGFMagicComponent::FieldRate() const
{
	float Rate = Modifiers().IntegrityRate;
	if (bOvercharge)
	{
		if (const FGFMiningSpellRow* Row = FGFMagicCatalog::Spell(TEXT("Overcharge")))
		{
			Rate *= Row->OverchargeRate > 0.f ? Row->OverchargeRate : 1.f;
		}
	}
	return Rate;
}

float UGFMagicComponent::FieldRange() const
{
	const FGFMiningSpellRow* Row = FGFMagicCatalog::FieldSpell(Element);
	const float Base = Row ? Row->RangeCm : 800.f;
	return FMath::Max(0.f, Base + Modifiers().RangeBonusCm);
}

void UGFMagicComponent::PushAttributes()
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!ASC || !ASC->GetSet<UGFCrystalAttributeSet>())
	{
		return;
	}
	ASC->SetNumericAttributeBase(UGFCrystalAttributeSet::GetCrystalMaxAttribute(), FGFMagicCatalog::ChargeMax());
	ASC->SetNumericAttributeBase(UGFCrystalAttributeSet::GetCrystalRegenAttribute(), FGFMagicCatalog::ChargeRegen());
	ASC->SetNumericAttributeBase(UGFCrystalAttributeSet::GetCrystalChargeAttribute(), CrystalCharge);
}

void UGFMagicComponent::GrantAbilities()
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!ASC || !IsAuthority() || bAbilitiesGranted)
	{
		return;
	}
	auto Give = [this, ASC](TSubclassOf<UGameplayAbility> Class, int32 InputId)
	{
		if (Class && !ASC->FindAbilitySpecFromClass(Class))
		{
			ASC->GiveAbility(FGameplayAbilitySpec(Class, 1, InputId, this));
		}
	};
	Give(UGFGA_Field::StaticClass(), GFMagic::InputField);
	Give(UGFGA_Clawcall::StaticClass(), GFMagic::InputClaw);
	Give(UGFGA_ToggleElement::StaticClass(), GFMagic::InputToggle);
	Give(UGFGA_Overcharge::StaticClass(), GFMagic::InputOvercharge);
	bAbilitiesGranted = true;
}

bool UGFMagicComponent::TryActivate(int32 InputId)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!ASC)
	{
		return false;
	}
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.InputID == InputId && ASC->TryActivateAbility(Spec.Handle))
		{
			return true;
		}
	}
	return false;
}

void UGFMagicComponent::ReleaseAbility(int32 InputId)
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		ASC->AbilityLocalInputReleased(InputId);
	}
}

void UGFMagicComponent::SetFieldHeld(bool bHeld)
{
	if (IsAuthority())
	{
		AuthoritySetField(bHeld);
	}
	else
	{
		ServerSetFieldHeld(bHeld);
	}
}

void UGFMagicComponent::SetAim(const FVector& Origin, const FVector& Point)
{
	(void)Origin;
	if (IsAuthority())
	{
		AuthoritySetAim(Point);
	}
	else
	{
		ServerSetAim(Point);
	}
}

void UGFMagicComponent::RequestClaw()
{
	if (IsAuthority())
	{
		AuthorityClaw();
	}
	else
	{
		ServerClaw();
	}
}

void UGFMagicComponent::RequestToggle()
{
	if (IsAuthority())
	{
		AuthorityToggle();
	}
	else
	{
		ServerToggle();
	}
}

void UGFMagicComponent::RequestOvercharge()
{
	if (IsAuthority())
	{
		AuthorityOvercharge();
	}
	else
	{
		ServerOvercharge();
	}
}

FName UGFMagicComponent::RequestBuy(FName UpgradeId)
{
	if (IsAuthority())
	{
		return AuthorityBuy(UpgradeId);
	}
	ServerBuy(UpgradeId);
	return TEXT("Pending");
}

void UGFMagicComponent::ServerSetFieldHeld_Implementation(bool bHeld)
{
	AuthoritySetField(bHeld);
}

void UGFMagicComponent::ServerSetAim_Implementation(FVector_NetQuantize Point)
{
	AuthoritySetAim(Point);
}

void UGFMagicComponent::ServerClaw_Implementation()
{
	AuthorityClaw();
}

void UGFMagicComponent::ServerToggle_Implementation()
{
	AuthorityToggle();
}

void UGFMagicComponent::ServerOvercharge_Implementation()
{
	AuthorityOvercharge();
}

void UGFMagicComponent::ServerBuy_Implementation(FName UpgradeId)
{
	AuthorityBuy(UpgradeId);
}

void UGFMagicComponent::AuthoritySetField(bool bHeld)
{
	if (!IsAuthority())
	{
		ServerSetFieldHeld(bHeld);
		return;
	}
	bFieldHeld = bHeld;
	if (!bHeld)
	{
		SyncMarker(AimPoint);
	}
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UGFMagicComponent::AuthoritySetAim(const FVector& Point)
{
	if (!IsAuthority())
	{
		ServerSetAim(Point);
		return;
	}
	AimPoint = FGFMagicRules::ClampRange(CastOrigin(), Point, FieldRange());
}

bool UGFMagicComponent::AuthorityClaw()
{
	if (!IsAuthority())
	{
		ServerClaw();
		return false;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::Spell(TEXT("Clawcall"));
	const float Cost = Spell ? Spell->ChargeCost : 20.f;
	if (ClawCharges <= 0 || CrystalCharge < Cost)
	{
		return false;
	}
	--ClawCharges;
	CrystalCharge = FMath::Max(0.f, CrystalCharge - Cost);
	ClawRecharge = 0.f;
	PushAttributes();
	if (const AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner()))
	{
		ClawForm = FGFMagicRules::ClawForm(PS->FactionId);
	}
	if (UWorld* World = GetWorld())
	{
		if (UGFBlockIntegritySubsystem* Sub = World->GetSubsystem<UGFBlockIntegritySubsystem>())
		{
			Sub->CastClaw(this);
		}
	}
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	return true;
}

bool UGFMagicComponent::AuthorityToggle()
{
	if (!IsAuthority())
	{
		ServerToggle();
		return false;
	}
	const AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner());
	const EGFMagicElement Faction = PS ? FGFMagicRules::FactionElement(PS->FactionId) : EGFMagicElement::Spark;
	if (PS)
	{
		ClawForm = FGFMagicRules::ClawForm(PS->FactionId);
	}
	if (Faction == EGFMagicElement::Spark)
	{
		return false;
	}
	Element = (Element == EGFMagicElement::Spark) ? Faction : EGFMagicElement::Spark;
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	return true;
}

bool UGFMagicComponent::AuthorityOvercharge()
{
	if (!IsAuthority())
	{
		ServerOvercharge();
		return false;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::Spell(TEXT("Overcharge"));
	const float Cost = Spell ? Spell->ChargeCost : 50.f;
	const float Seconds = Spell ? Spell->OverchargeSeconds : 8.f;
	if (CrystalCharge < Cost)
	{
		return false;
	}
	CrystalCharge -= Cost;
	bOvercharge = true;
	OverchargeLeft = Seconds;
	PushAttributes();
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	return true;
}

FName UGFMagicComponent::AuthorityBuy(FName UpgradeId)
{
	if (!IsAuthority())
	{
		ServerBuy(UpgradeId);
		return TEXT("Pending");
	}
	const FGFSpellUpgradeRow* Row = FGFMagicCatalog::Upgrade(UpgradeId);
	FMagicUpgradeState State;
	State.Owned = OwnedUpgrades;
	if (AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner()))
	{
		State.FactionId = PS->FactionId;
		State.Path = PS->ChosenPath;
		if (Row && PS->Inventory)
		{
			if (!Row->CostItemA.IsNone())
			{
				State.Bag.Add(Row->CostItemA, PS->Inventory->GetCount(Row->CostItemA));
			}
			if (!Row->CostItemB.IsNone())
			{
				State.Bag.Add(Row->CostItemB, PS->Inventory->GetCount(Row->CostItemB));
			}
		}
	}
	const FMagicUpgradeDecision Decision = FGFMagicRules::TryBuy(State, UpgradeId);
	LastBuyCode = Decision.Code;
	if (Decision.Code != TEXT("Ok") || !Row)
	{
		return Decision.Code;
	}
	if (AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner()))
	{
		if (PS->Inventory)
		{
			if (!Row->CostItemA.IsNone())
			{
				PS->Inventory->RemoveItem(Row->CostItemA, Row->CostCountA);
			}
			if (!Row->CostItemB.IsNone())
			{
				PS->Inventory->RemoveItem(Row->CostItemB, Row->CostCountB);
			}
		}
	}
	OwnedUpgrades = Decision.Owned;
	ApplyUpgradeEffect(UpgradeId);
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
	return TEXT("Ok");
}

void UGFMagicComponent::ApplyUpgradeEffect(FName UpgradeId)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	if (!ASC || !IsAuthority())
	{
		return;
	}
	UGameplayEffect* Effect = NewObject<UGameplayEffect>(this);
	Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;
	UTargetTagsGameplayEffectComponent& Tags = Effect->AddComponent<UTargetTagsGameplayEffectComponent>();
	FInheritedTagContainer Grant;
	Grant.AddTag(TAG_Upgrade_Mining);
	Tags.SetAndApplyTargetTagChanges(Grant);
	UpgradeEffects.Add(Effect);
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectToSelf(Effect, 1.f, Context);
	if (!Handle.IsValid())
	{
		UE_LOG(LogGF, Warning, TEXT("Magic: %s is owned, but its gameplay effect did not apply"), *UpgradeId.ToString());
	}
}

void UGFMagicComponent::TickMagic(float Dt)
{
	if (!IsAuthority() || Dt <= 0.f)
	{
		return;
	}
	if (const AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner()))
	{
		ClawForm = FGFMagicRules::ClawForm(PS->FactionId);
		if (Element != EGFMagicElement::Spark)
		{
			const EGFMagicElement Faction = FGFMagicRules::FactionElement(PS->FactionId);
			if (Faction != EGFMagicElement::Spark)
			{
				Element = Faction;
			}
		}
	}
	const float MaxCharge = FGFMagicCatalog::ChargeMax();
	CrystalCharge = FMath::Min(MaxCharge, CrystalCharge + FGFMagicCatalog::ChargeRegen() * Dt);
	const FGFMiningSpellRow* Claw = FGFMagicCatalog::Spell(TEXT("Clawcall"));
	const int32 MaxCharges = Claw ? Claw->MaxCharges : 2;
	const float Recharge = Claw ? Claw->RechargeSeconds : 6.f;
	if (ClawCharges < MaxCharges)
	{
		ClawRecharge += Dt;
		if (Recharge <= 0.f || ClawRecharge >= Recharge)
		{
			ClawRecharge = 0.f;
			++ClawCharges;
		}
	}
	if (bOvercharge)
	{
		OverchargeLeft -= Dt;
		if (OverchargeLeft <= 0.f)
		{
			bOvercharge = false;
			OverchargeLeft = 0.f;
		}
	}
	if (Haul.Num() > 0)
	{
		if (AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner()))
		{
			if (PS->Inventory)
			{
				TArray<FGFItemStack> Left;
				for (const FGFItemStack& Stack : Haul)
				{
					const int32 Room = FMath::Max(0, PocketCap - BagTotal());
					const int32 Moved = FMath::Min(Stack.Count, Room);
					if (Moved > 0)
					{
						PS->Inventory->AddItem(Stack.ItemId, Moved);
					}
					if (Stack.Count - Moved > 0)
					{
						Left.Add(FGFItemStack(Stack.ItemId, Stack.Count - Moved));
					}
				}
				Haul = MoveTemp(Left);
			}
		}
	}
	PushAttributes();
}

void UGFMagicComponent::ReceiveDrop(FName ItemId, int32 Count, const FVector& From)
{
	if (!IsAuthority() || ItemId.IsNone() || Count <= 0)
	{
		return;
	}
	GrantedDrops += Count;
	int32 ToBag = 0;
	if (AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner()))
	{
		if (PS->Inventory)
		{
			ToBag = FMath::Min(Count, FMath::Max(0, PocketCap - BagTotal()));
			if (ToBag > 0)
			{
				PS->Inventory->AddItem(ItemId, ToBag);
			}
		}
	}
	const int32 ToHaul = Count - ToBag;
	if (ToHaul > 0)
	{
		if (FGFItemStack* Existing = Haul.FindByPredicate([ItemId](const FGFItemStack& Stack) { return Stack.ItemId == ItemId; }))
		{
			Existing->Count += ToHaul;
		}
		else
		{
			Haul.Add(FGFItemStack(ItemId, ToHaul));
		}
	}
	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFMagicDropRibbon* Ribbon = World->SpawnActor<AGFMagicDropRibbon>(AGFMagicDropRibbon::StaticClass(), From, FRotator::ZeroRotator, Params);
		APawn* Pawn = Cast<APawn>(AvatarPawn.Get());
		if (!Pawn)
		{
			if (const APlayerState* PS = Cast<APlayerState>(GetOwner()))
			{
				Pawn = PS->GetPawn();
			}
		}
		if (Ribbon)
		{
			Ribbon->Setup(From, Pawn, CastOrigin(), ItemId);
		}
	}
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UGFMagicComponent::SyncMarker(const FVector& At)
{
	if (!IsAuthority())
	{
		return;
	}
	if (!bFieldHeld)
	{
		if (AGFMagicFieldMarker* Live = Marker.Get())
		{
			Live->Destroy();
		}
		Marker = nullptr;
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	AGFMagicFieldMarker* Live = Marker.Get();
	if (!Live)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Live = World->SpawnActor<AGFMagicFieldMarker>(AGFMagicFieldMarker::StaticClass(), At, FRotator::ZeroRotator, Params);
		Marker = Live;
	}
	if (Live)
	{
		Live->SetActorLocation(At);
		Live->SetElement(Element);
	}
}

void UGFMagicComponent::OnRep_Haul()
{
}
