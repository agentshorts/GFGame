#pragma once

// A townsfolk in one town. An elder is the same actor with the elder role, or AGFElder.
// Server wanders inside the town disc. Interact raises OnInteract once per press.

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GFInteractable.h"
#include "GFTownsfolkRules.h"
#include "GFVillager.generated.h"

class UCapsuleComponent;
class UGFSimpleAnimComponent;
class UGFWanderComponent;
class UStaticMeshComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGFTownsfolkInteract, AActor* /*Townsfolk*/, APawn* /*Pawn*/);

UCLASS()
class GFGAME_API AGFVillager : public APawn, public IGFInteractable
{
	GENERATED_BODY()

public:
	AGFVillager();

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Townsfolk")
	FName TownId;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Townsfolk")
	EGFTownsfolkRole Role = EGFTownsfolkRole::Villager;

	UPROPERTY(ReplicatedUsing = OnRep_Behavior, BlueprintReadOnly, Category = "GF|Townsfolk")
	EGFTownsfolkBehavior Behavior = EGFTownsfolkBehavior::Idle;

	/** Server. One call per successful press. The actor is this townsfolk. */
	FOnGFTownsfolkInteract OnInteract;

	FString DisplayTitle() const;
	void InitTownsfolk(const FGFTownsfolkSpawnParams& Params);

	virtual FText GetInteractPrompt() const override;
	virtual bool CanInteract(APawn* Pawn) const override;
	virtual void Interact(APawn* Pawn) override;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_Behavior();

	void ApplyVisuals();
	void PlayTalkClip();

	UPROPERTY(VisibleAnywhere, Category = "GF|Townsfolk")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, Category = "GF|Townsfolk")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "GF|Townsfolk")
	TObjectPtr<UGFWanderComponent> Wander;

	UPROPERTY(VisibleAnywhere, Category = "GF|Townsfolk")
	TObjectPtr<UGFSimpleAnimComponent> SimpleAnim;

	UPROPERTY(Replicated)
	float ReachCm = 300.f;

	FGFTownsfolkMind Mind;
	FRandomStream Rng;
	float TalkSeconds = 4.f;
};
