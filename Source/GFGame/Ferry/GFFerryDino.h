#pragma once

// The ferry body. An AActor on purpose: no taming component, no leader, and no owner.
// A stop copy stays parked on the perch. A transit copy carries the basket and the rider.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFFerryDino.generated.h"

class USkeletalMeshComponent;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	TObjectPtr<USkeletalMeshComponent> Bird;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	TObjectPtr<UStaticMeshComponent> Perch;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	TObjectPtr<UStaticMeshComponent> Basket;

	/** Which stop this parked copy belongs to. None on a transit copy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	FName StopId;

	/** Placeholder mesh key. The ferry uses the flyer look, not a tameable species row. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	FName BodyAssetKey;

	/** Anim-map key for the current pose. Parked copies use the perch idle. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	FName AnimKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry")
	bool bTransit = false;

	void SetStopId(FName InStopId) { StopId = InStopId; }

	USkeletalMeshComponent* GetBird() const { return Bird; }

	/** Puts the actor on the perch socket and the perch prop on the stop. Yaw is -90. */
	void ParkAt(const FVector& StopLocation);

	/** Transit copy. Spawns at the perch and hides the perch prop. Does not parent the basket. */
	void BeginTransit(const FVector& PerchWorld);

	/** Plays Key when it changes. A missing clip leaves the key set and the mesh alone. */
	void SetAnim(FName Key);

	/** Basket mesh owned by this actor, unattached, so the bird can leave the perch without dragging it. */
	UStaticMeshComponent* SpawnBasket();

	/** Transit flag only. Parked copies go back to the perch idle. */
	void SetTransit(bool bInTransit);

	virtual void BeginPlay() override;

private:
	void ApplyBody();
	static UStaticMesh* LoadStaticQuiet(const TCHAR* Path);
};
