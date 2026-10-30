#pragma once

// The ferry body. An AActor on purpose: no taming component, no leader, and no owner.
// A stop copy stays parked. A transit copy is the one that carries a rider.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFFerryDino.generated.h"

class UStaticMeshComponent;

UCLASS()
class GFGAME_API AGFFerryDino : public AActor
{
	GENERATED_BODY()

public:
	AGFFerryDino();

	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Which stop this parked copy belongs to. None on a transit copy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	FName StopId;

	/** Placeholder mesh key. The ferry uses the flyer look, not a tameable species row. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	FName BodyAssetKey;

	/** Anim-map key for the current pose. Parked copies use the perch. Transit copies glide. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	FName AnimKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	bool bTransit = false;

	void SetStopId(FName InStopId) { StopId = InStopId; }

	/** Transit copies are the flying bodies. Parked copies stay on Perch_Idle. */
	void SetTransit(bool bInTransit);

	virtual void BeginPlay() override;

private:
	void ApplyBody();
};
