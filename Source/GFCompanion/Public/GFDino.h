#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GFDinoKill.h"
#include "GFDinoTypes.h"
#include "GFWanderComponent.h"
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
UCLASS(Config = Game)
class GFCOMPANION_API AGFDino : public ACharacter
{
	GENERATED_BODY()

public:
	AGFDino();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Dino")
	TObjectPtr<UGFTamingComponent> Taming;

	/** Wild idle wander. Tamed follow and attack do not use it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Dino")
	TObjectPtr<UGFWanderComponent> Wander;

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

	/** Used when the species is missing from HitPointsBySpecies. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Dino", meta = (ClampMin = "1"))
	float DefaultHitPoints = 60.f;

	/** Max hit points per species. An empty key uses DefaultHitPoints. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Dino")
	TMap<FName, float> HitPointsBySpecies;

	/**
	 * A rented dino keeps its faint and quit rules. Hits do not spend hit points and pay nothing.
	 * Wild and hired dinos leave this false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Dino")
	bool bRental = false;

	/** Sets the species and refreshes the placeholder. */
	UFUNCTION(BlueprintCallable, Category = "GF|Dino")
	void SetSpecies(FName InSpecies);

	UFUNCTION(BlueprintCallable, Category = "GF|Dino")
	void SetLeader(APawn* NewLeader);

	UFUNCTION(BlueprintCallable, Category = "GF|Dino")
	void SetAttackTarget(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "GF|Dino")
	EGFDinoState GetDinoState() const { return DinoState; }

	UFUNCTION(BlueprintPure, Category = "GF|Dino")
	EGFDinoLife GetLife() const { return Life; }

	UFUNCTION(BlueprintPure, Category = "GF|Dino")
	float GetHitPoints() const { return HitPoints; }

	UFUNCTION(BlueprintPure, Category = "GF|Dino")
	bool IsRental() const { return bRental; }

	void SetRental(bool bInRental) { bRental = bInRental; }

	/** Pins the skin roll. The same seed always pays the same table. */
	void SetDropSeed(int32 Seed);

	int32 GetDropSeed() const { return DropSeed; }

	/** HitPointsBySpecies, else DefaultHitPoints, else the kill-rule default. Reads the class config. */
	static float HitPointsFor(FName Species);

	AActor* GetAttackTarget() const { return AttackTarget.Get(); }
	APawn* GetLeader() const { return Leader.Get(); }

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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
	void RefreshVitals();
	void CopyVitals();
	void GrantKill(const FGFDinoKillResult& Result);
	static FString AttackerKeyFrom(AController* EventInstigator, AActor* DamageCauser);

	UFUNCTION()
	void HandleTameStateChanged(EGFTameState NewState);

	UPROPERTY(VisibleAnywhere, Category = "GF|Dino")
	EGFDinoState DinoState = EGFDinoState::Idle;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Dino")
	EGFDinoLife Life = EGFDinoLife::Alive;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Dino")
	float HitPoints = 60.f;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> Leader;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> AttackTarget;

	float LastAttackTime = -1000.f;
	int32 DropSeed = 1;
	bool bDropSeedPinned = false;
	bool bVitalsLocked = false;
	/** Set on the first payment so a repeated or replayed death cannot grant again. */
	bool bKillGranted = false;
	FGFDinoKillMachine Kill;
};

/** Called at the end of AGFDino::BeginPlay. The game module installs stair tuning and foot IK here. */
using FGFDinoReadyHook = void (*)(AGFDino*);

/** Pass null to clear. The last installer wins. */
GFCOMPANION_API void GFSetDinoReadyHook(FGFDinoReadyHook Hook);
