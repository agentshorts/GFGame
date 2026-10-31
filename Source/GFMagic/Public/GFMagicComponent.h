#pragma once

// Per-player magic: which element is up, the crystal pool, claw charges, and the upgrades they picked.
// The server decides. Clients ask through the RPCs. GAS abilities call the same authority functions.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFItemTypes.h"
#include "GFMagicTypes.h"
#include "GFMagicRules.h"
#include "GameplayEffect.h"
#include "GFMagicComponent.generated.h"

class UAbilitySystemComponent;
class AGFMagicFieldMarker;
class APawn;

struct FMagicDeferredCast
{
	TArray<FIntVector> Cells;
	EGFMagicElement Element = EGFMagicElement::Spark;
	bool bFlat = false;
	float FlatIntegrity = 0.f;
	float Rate = 1.f;
	bool bSoilChain = false;
	int32 ExtraDrop = 0;
	bool bJackpot = false;
	TArray<FString> Contributors;
};

UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFMAGIC_API UGFMagicComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFMagicComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	void BindAvatar(APawn* Pawn);
	UAbilitySystemComponent* GetAbilitySystem() const;

	UFUNCTION(BlueprintPure, Category = "GF|Magic")
	bool IsFieldHeld() const { return bFieldHeld; }

	UFUNCTION(BlueprintPure, Category = "GF|Magic")
	EGFMagicElement GetElement() const { return Element; }

	UFUNCTION(BlueprintPure, Category = "GF|Magic")
	int32 GetClawCharges() const { return ClawCharges; }

	UFUNCTION(BlueprintPure, Category = "GF|Magic")
	float GetCrystalCharge() const { return CrystalCharge; }

	UFUNCTION(BlueprintPure, Category = "GF|Magic")
	int32 GetGrantedDrops() const { return GrantedDrops; }

	UFUNCTION(BlueprintPure, Category = "GF|Magic")
	FName GetClawForm() const { return ClawForm; }

	UFUNCTION(BlueprintPure, Category = "GF|Magic")
	bool IsOvercharged() const { return bOvercharge; }

	const TArray<FName>& GetOwnedUpgrades() const { return OwnedUpgrades; }
	const TArray<FGFItemStack>& GetHaul() const { return Haul; }
	int32 HaulCount(FName ItemId) const;
	FGFMagicModifiers Modifiers() const { return FGFMagicRules::ModifiersFor(OwnedUpgrades); }

	/** Where the cast starts when this player has no pawn (tests). */
	void SetManualOrigin(const FVector& Origin) { ManualOrigin = Origin; }
	FVector CastOrigin() const;
	FVector GetAimPoint() const { return AimPoint; }
	FName ContributorKey() const;

	/** Local or authority. Sends an RPC from a client. */
	void SetFieldHeld(bool bHeld);
	void SetAim(const FVector& Origin, const FVector& Point);
	void RequestClaw();
	void RequestToggle();
	void RequestOvercharge();
	FName RequestBuy(FName UpgradeId);

	/** True when a granted ability was activated. */
	bool TryActivate(int32 InputId);
	void ReleaseAbility(int32 InputId);

	void AuthoritySetField(bool bHeld);
	void AuthoritySetAim(const FVector& Point);
	bool AuthorityClaw();
	bool AuthorityToggle();
	bool AuthorityOvercharge();
	FName AuthorityBuy(FName UpgradeId);

	/** Server step: crystal regen, claw recharge, overcharge, haul flush. */
	void TickMagic(float Dt);
	float FieldRate() const;
	float FieldRange() const;

	/** Bag first. Whatever does not fit waits in the haul and flushes when there is room. */
	void ReceiveDrop(FName ItemId, int32 Count, const FVector& From);

	void SyncMarker(const FVector& At);
	TArray<FMagicDeferredCast>& Deferred() { return Pending; }

	/** 0 means the bag is treated as full (tests). A large cap never walls a player. */
	UPROPERTY(EditAnywhere, Category = "GF|Magic")
	int32 PocketCap = 999999;

private:
	void GrantAbilities();
	void PushAttributes();
	void ApplyUpgradeEffect(FName UpgradeId);
	int32 BagTotal() const;
	bool IsAuthority() const;

	UFUNCTION(Server, Reliable)
	void ServerSetFieldHeld(bool bHeld);

	UFUNCTION(Server, Unreliable)
	void ServerSetAim(FVector_NetQuantize Point);

	UFUNCTION(Server, Reliable)
	void ServerClaw();

	UFUNCTION(Server, Reliable)
	void ServerToggle();

	UFUNCTION(Server, Reliable)
	void ServerOvercharge();

	UFUNCTION(Server, Reliable)
	void ServerBuy(FName UpgradeId);

	UFUNCTION()
	void OnRep_Haul();

	UPROPERTY(Replicated)
	bool bFieldHeld = false;

	UPROPERTY(Replicated)
	EGFMagicElement Element = EGFMagicElement::Spark;

	UPROPERTY(Replicated)
	FVector AimPoint = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FName ClawForm = TEXT("Crystal");

	UPROPERTY(Replicated, ReplicatedUsing = OnRep_Haul)
	TArray<FGFItemStack> Haul;

	UPROPERTY(Replicated)
	TArray<FName> OwnedUpgrades;

	UPROPERTY(Replicated)
	int32 ClawCharges = 2;

	UPROPERTY(Replicated)
	float CrystalCharge = 100.f;

	UPROPERTY(Replicated)
	int32 GrantedDrops = 0;

	UPROPERTY(Replicated)
	bool bOvercharge = false;

	UPROPERTY()
	TArray<TObjectPtr<UGameplayEffect>> UpgradeEffects;

	TArray<FMagicDeferredCast> Pending;
	TWeakObjectPtr<APawn> AvatarPawn;
	TWeakObjectPtr<AGFMagicFieldMarker> Marker;
	FVector ManualOrigin = FVector::ZeroVector;
	float ClawRecharge = 0.f;
	float OverchargeLeft = 0.f;
	bool bAbilitiesGranted = false;
	FName LastBuyCode = TEXT("None");

public:
	FName GetLastBuyCode() const { return LastBuyCode; }
};
