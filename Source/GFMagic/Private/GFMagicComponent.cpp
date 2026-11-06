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
	DOREPLIFETIME_CONDITION(UGFMagicComponent, TargetedFraction, COND_OwnerOnly);
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
	float Rate = Modifiers().IntegrityRate * BreakChainScale();
	if (bOvercharge)
	{
		if (const FGFMiningSpellRow* Row = FGFMagicCatalog::Spell(TEXT("Overcharge")))
		{
			Rate *= Row->OverchargeRate > 0.f ? Row->OverchargeRate : 1.f;
		}
	}
	return Rate;
}

float UGFMagicComponent::FieldCostPerSecond() const
{
	if (!bFieldHeld)
	{
		return 0.f;
	}
	const FGFMiningSpellRow* Row = FGFMagicCatalog::FieldSpell(Element);
	return Row ? FMath::Max(0.f, Row->CostPerSecond) : 0.f;
}

double UGFMagicComponent::MagicNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

float UGFMagicComponent::BreakChainScale() const
{
	const FGFMiningSpellRow* Crystal = FGFMagicCatalog::Spell(TEXT("Crystal"));
	const int32 Peak = Crystal && Crystal->ChainPeak > 0 ? Crystal->ChainPeak : 20;
	const float Bonus = Crystal && Crystal->ChainBonus > 0.f ? Crystal->ChainBonus : 0.5f;
	return FGFMagicRules::ChainScale(ChainCount, Peak, Bonus);
}

void UGFMagicComponent::NoteBreak(double Now)
{
	const FGFMiningSpellRow* Crystal = FGFMagicCatalog::Spell(TEXT("Crystal"));
	const float Window = Crystal && Crystal->ChainWindow > 0.f ? Crystal->ChainWindow : 1.5f;
	if (Now - LastBreakTime <= static_cast<double>(Window))
	{
		++ChainCount;
	}
	else
	{
		ChainCount = 1;
	}
	LastBreakTime = Now;
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

void UGFMagicComponent::AuthoritySetTargetedFraction(const FGFTargetedCellFraction& Next)
{
	if (!IsAuthority())
	{
		return;
	}
	if (TargetedFraction.Matches(Next))
	{
		return;
	}
	const int32 NextSerial = TargetedFraction.Serial + 1;
	TargetedFraction = Next;
	TargetedFraction.Serial = NextSerial;
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UGFMagicComponent::SetCrackTolerance(float Tolerance)
{
	CrackTolerance = FMath::Clamp(Tolerance, 0.01f, 0.5f);
}

void UGFMagicComponent::PredictCrackStep(FName BlockId, EGFMagicElement InElement, float IntegrityPerSecond, float Rate, float Dt, bool bTown, const FIntVector& Cell, const FString& NodeKey)
{
	if (IsAuthority() || BlockId.IsNone())
	{
		return;
	}
	Crack.BeginTarget(bTown, Cell, NodeKey);
	Crack.PredictStep(FGFMiningFeelRules::FractionLoss(BlockId, InElement, IntegrityPerSecond, Rate, Dt));
	Crack.Reconcile(TargetedFraction, CrackTolerance);
}

void UGFMagicComponent::ReconcileCrack()
{
	if (!IsAuthority())
	{
		Crack.Reconcile(TargetedFraction, CrackTolerance);
	}
}

float UGFMagicComponent::ShownCrackFraction() const
{
	if (IsAuthority())
	{
		return TargetedFraction.bActive ? TargetedFraction.Fraction : 1.f;
	}
	return Crack.Shown();
}

void UGFMagicComponent::OnRep_TargetedFraction()
{
	Crack.Reconcile(TargetedFraction, CrackTolerance);
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
	if (!bHeld && bFieldHeld)
	{
		const FGFMiningSpellRow* Active = FGFMagicCatalog::FieldSpell(Element);
		if (Active && Active->Shape == TEXT("Melee") && !bMeleeHeavySent)
		{
			QueueSwing(false);
		}
		bMeleeHeavySent = false;
		RootHeld = 0.f;
	}
	else if (bHeld && !bFieldHeld)
	{
		bMeleeHeavySent = false;
		RootHeld = 0.f;
		MeleeChain = NAME_None;
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
		UE_LOG(LogGF, Verbose, TEXT("Magic: claw refused, charges %d crystal %.1f cost %.1f"), ClawCharges, CrystalCharge, Cost);
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
		else
		{
			UE_LOG(LogGF, Verbose, TEXT("Magic: claw has no integrity subsystem"));
		}
	}
	else
	{
		UE_LOG(LogGF, Verbose, TEXT("Magic: claw has no world"));
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
	const FGFMiningSpellRow* Crystal = FGFMagicCatalog::Spell(TEXT("Crystal"));
	const float Delay = Crystal && Crystal->RegenDelay > 0.f ? Crystal->RegenDelay : 0.5f;
	const float Window = Crystal && Crystal->ChainWindow > 0.f ? Crystal->ChainWindow : 1.5f;
	if (ChainCount > 0 && MagicNow() - LastBreakTime > static_cast<double>(Window))
	{
		ChainCount = 0;
	}
	if (bFieldHeld && FieldCostPerSecond() > 0.f)
	{
		CastIdle = 0.f;
	}
	else
	{
		CastIdle += Dt;
		if (CastIdle >= Delay)
		{
			const float MaxCharge = FGFMagicCatalog::ChargeMax();
			CrystalCharge = FMath::Min(MaxCharge, CrystalCharge + FGFMagicCatalog::ChargeRegen() * Dt);
		}
	}
	const FGFMiningSpellRow* Active = bFieldHeld ? FGFMagicCatalog::FieldSpell(Element) : nullptr;
	if (bFieldHeld && Active && Active->Shape == TEXT("Melee"))
	{
		RootHeld += Dt;
		const float Hold = Active->HeavyHoldSeconds > 0.f ? Active->HeavyHoldSeconds : 0.5f;
		if (!bMeleeHeavySent && RootHeld + KINDA_SMALL_NUMBER >= Hold)
		{
			bMeleeHeavySent = true;
			QueueSwing(true);
		}
	}
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
	if (const AGFPlayerState* Receiver = Cast<AGFPlayerState>(GetOwner()))
	{
		if (!Receiver->PlayerKey.IsEmpty())
		{
			OnDropReceived().Broadcast(GetWorld(), Receiver->PlayerKey, ItemId, Count, From);
		}
	}
}

FGFOnMagicDropReceived& UGFMagicComponent::OnDropReceived()
{
	static FGFOnMagicDropReceived Delegate;
	return Delegate;
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
		const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(Element);
		FVector Dir = (AimPoint - CastOrigin()).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			Dir = FVector(0.f, 0.f, -1.f);
		}
		float Half = Spell ? Spell->ConeHalfAngle : 15.f;
		if (bOvercharge)
		{
			if (const FGFMiningSpellRow* Over = FGFMagicCatalog::Spell(TEXT("Overcharge")))
			{
				Half += FMath::Max(0.f, Over->OverchargeHalfAngle);
			}
		}
		const float Intensity = (Spell && Spell->Shape == TEXT("Cone")) ? Spell->LightIntensity : 0.f;
		Live->SetBeam(Element, Dir, Half, FieldRange(), Intensity);
	}
}

void UGFMagicComponent::SpendCharge(float Amount)
{
	if (!IsAuthority() || Amount <= 0.f)
	{
		return;
	}
	CrystalCharge = FMath::Max(0.f, CrystalCharge - Amount);
	PushAttributes();
}

void UGFMagicComponent::OnRep_Haul()
{
}

bool UGFMagicComponent::QueueSwing(bool bHeavy)
{
	const FGFMagicModifiers Mods = Modifiers();
	const FName Form = Mods.MeleeForm.IsNone() ? FName(TEXT("Rootmaul")) : Mods.MeleeForm;
	const FName MoveId = FGFMagicShapes::NextMove(Form, MeleeChain, bHeavy);
	const FGFRootmaulMoveRow* Move = FGFMagicShapes::Move(Form, MoveId);
	if (!Move)
	{
		return false;
	}
	if (Move->ChargeCost > CrystalCharge)
	{
		return false;
	}
	if (Move->ChargeCost > 0.f)
	{
		CrystalCharge -= Move->ChargeCost;
		PushAttributes();
	}
	MeleeChain = MoveId;
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(EGFMagicElement::Root);
	Rhythm.Window = Spell && Spell->RhythmWindow > 0.f ? Spell->RhythmWindow : 0.15f;
	Rhythm.Step = Spell && Spell->RhythmStep > 0.f ? Spell->RhythmStep : 0.25f;
	Rhythm.MaxBonus = Spell && Spell->RhythmMax > 0.f ? Spell->RhythmMax : 0.75f;
	FVector Aim = (AimPoint - CastOrigin()).GetSafeNormal();
	if (Aim.IsNearlyZero())
	{
		Aim = FVector::ForwardVector;
	}
	FMagicSwing Swing;
	Swing.MoveId = MoveId;
	Swing.Width = Move->Width;
	Swing.Height = Move->Height;
	Swing.Depth = Move->Depth;
	Swing.Power = Move->Power;
	Swing.ChargeCost = Move->ChargeCost;
	Swing.LungeCm = Move->LungeCm;
	Swing.bPitch = Move->bPitch;
	Swing.bRooted = Move->bRooted;
	Swing.Rhythm = Rhythm.Note(MagicNow(), true);
	Swing.AimDir = Aim;
	Swing.Forward = Aim;
	if (const APawn* Pawn = AvatarPawn.Get())
	{
		if (!Move->bPitch)
		{
			const FVector Facing = Pawn->GetActorForwardVector();
			if (!Facing.IsNearlyZero())
			{
				Swing.Forward = Facing;
			}
		}
	}
	SwingQueue.Add(Swing);
	return true;
}

bool UGFMagicComponent::ExecuteConsole(const FString& Line)
{
	TArray<FString> Parts;
	Line.ParseIntoArrayWS(Parts);
	if (Parts.Num() == 0)
	{
		return false;
	}
	FString Name = Parts[0];
	const FString Prefix = TEXT("gf.Magic.");
	if (Name.StartsWith(Prefix))
	{
		Name = Name.RightChop(Prefix.Len());
	}
	if (Name == TEXT("Field"))
	{
		if (Parts.Num() > 1 && (Parts[1] == TEXT("0") || Parts[1].Equals(TEXT("off"), ESearchCase::IgnoreCase)))
		{
			AuthoritySetField(false);
			return true;
		}
		if (Parts.Num() > 1 && Parts[1].Equals(TEXT("Spark"), ESearchCase::IgnoreCase))
		{
			Element = EGFMagicElement::Spark;
		}
		else if (Parts.Num() > 1)
		{
			EGFMagicElement Want = EGFMagicElement::Spark;
			if (Parts[1].Equals(TEXT("Magma"), ESearchCase::IgnoreCase))
			{
				Want = EGFMagicElement::Magma;
			}
			else if (Parts[1].Equals(TEXT("Root"), ESearchCase::IgnoreCase))
			{
				Want = EGFMagicElement::Root;
			}
			else if (Parts[1].Equals(TEXT("Frost"), ESearchCase::IgnoreCase))
			{
				Want = EGFMagicElement::Frost;
			}
			if (Want != EGFMagicElement::Spark)
			{
				const AGFPlayerState* PS = Cast<AGFPlayerState>(GetOwner());
				if (PS && FGFMagicRules::FactionElement(PS->FactionId) == Want)
				{
					Element = Want;
				}
			}
		}
		AuthoritySetField(true);
		return true;
	}
	if (Name == TEXT("Claw"))
	{
		return AuthorityClaw();
	}
	if (Name == TEXT("Toggle"))
	{
		return AuthorityToggle();
	}
	if (Name == TEXT("Overcharge"))
	{
		return AuthorityOvercharge();
	}
	if (Name == TEXT("Buy") && Parts.Num() > 1)
	{
		AuthorityBuy(FName(*Parts[1]));
		return true;
	}
	return false;
}
