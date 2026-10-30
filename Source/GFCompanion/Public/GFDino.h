#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GFDinoTypes.h"
#include "GFDino.generated.h"

class UGFTamingComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UGFSimpleAnimComponent;

/**
 * Dino base class. Art is the skeletal mesh from DT_AssetMap ("Dino.<Species>") when that row has one.
 * Body stays the coloured placeholder sphere and is what shows when the art is missing.
 * Server-driven: wild, it attacks whoever its taming machine is hostile at; tamed, it follows its owner.
 * Behaviour is a C++ switch for now; a StateTree (Idle/Follow/Fight/Rest) replaces it later.
 */
UCLASS()
class GFCOMPANION_API AGFDino : public ACharacter
{
	GENERATED_BODY()

public:
	AGFDino();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Dino")
	TObjectPtr<UGFTamingComponent> Taming;

	/** Placeholder sphere. Hidden while Art is showing. Kept so callers that read Body still can. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Dino")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Skeletal art from the asset map. Hidden when the mesh is missing and Body shows instead. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Dino")
	TObjectPtr<USkeletalMeshComponent> Art;

	/** Idle / move on Art. Clips come from the species row. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Dino")
	TObjectPtr<UGFSimpleAnimComponent> SimpleAnim;

	/** Follow when farther than this from the leader (cm). PLAN.md: followers stay 3-8 m away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Dino")
	float FollowStartDistance = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Dino")
	float FollowStopDistance = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Dino")
	float AttackRange = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Dino")
	float AttackDamage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Dino")
	float AttackCooldown = 1.2f;

	/** Sets the species and refreshes the placeholder. */
	UFUNCTION(BlueprintCallable, Category = "GF|Dino")
	void SetSpecies(FName InSpecies);

	UFUNCTION(BlueprintCallable, Category = "GF|Dino")
	void SetLeader(APawn* NewLeader);

	UFUNCTION(BlueprintCallable, Category = "GF|Dino")
	void SetAttackTarget(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "GF|Dino")
	EGFDinoState GetDinoState() const { return DinoState; }

	AActor* GetAttackTarget() const { return AttackTarget.Get(); }
	APawn* GetLeader() const { return Leader.Get(); }

	/** Asset-map key for a species ("Dino.Raptor"). */
	static FName AssetKey(FName Species);

	/** Pure transition function so tests can drive it without a world. */
	static EGFDinoState NextState(EGFDinoState Current, bool bTamed, bool bHasTarget, float DistToTarget, float AttackRange,
		bool bHasLeader, float DistToLeader, float FollowStart, float FollowStop);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	void ApplyPlaceholder();
	void MoveToward(const FVector& Location);

	UFUNCTION()
	void HandleTameStateChanged(EGFTameState NewState);

	UPROPERTY(VisibleAnywhere, Category = "GF|Dino")
	EGFDinoState DinoState = EGFDinoState::Idle;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> Leader;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> AttackTarget;

	float LastAttackTime = -1000.f;
};
