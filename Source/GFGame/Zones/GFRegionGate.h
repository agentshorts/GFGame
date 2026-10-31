#pragma once

// Porch arch. Overlap on the server plans the trip. A missing map tells the player and stays put.
// Grumble Dig is the exception while its map package is missing: the arch drops the pawn on the in-world site.
// Hub shortcut gates are a different path and never set a pending Shardstone.

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "GFRegionGate.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class APawn;
struct FGFRegionSlot;

UCLASS()
class GFGAME_API AGFRegionGate : public AActor
{
	GENERATED_BODY()

public:
	AGFRegionGate();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Setup(const FGFRegionSlot& Slot);

	/**
	 * In-world Grumble Dig entry. False unless this is the enabled Grumble Dig arch and L_GrumbleDig
	 * is not a package yet. Does not open a level and does not set a pending stone.
	 */
	bool EnterGrumbleDig(APawn* Pawn);

protected:
	void ApplyVisual();

	UFUNCTION()
	void OnRep_Color();

	UFUNCTION()
	void OnGateOverlap(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere, Category = "GF|Zone")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "GF|Zone")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(Replicated)
	FName RegionId;

	UPROPERTY(Replicated)
	FName GateId;

	UPROPERTY(Replicated)
	bool bGateEnabled = false;

	UPROPERTY(ReplicatedUsing = OnRep_Color)
	FLinearColor Color = FLinearColor::White;

	TMap<FString, double> LastUseSeconds;
};
