#pragma once

// Greybox body for one live lab anomaly. A tinted engine cube, no art, no collision, and it never damages a player.
// The shift rules live on FGFLabRun; AGFLabDirector spawns and destroys these on the server as anomalies wake and sleep.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFLabAnomalyMarker.generated.h"

class UStaticMeshComponent;

UCLASS()
class GFLAB_API AGFLabAnomalyMarker : public AActor
{
	GENERATED_BODY()

public:
	AGFLabAnomalyMarker();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: which anomaly this cube is standing in for. Clients apply the same tint. */
	void Setup(FName InKind);

	UPROPERTY(VisibleAnywhere, Category = "GF|Lab")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(ReplicatedUsing = OnRep_Kind, BlueprintReadOnly, Category = "GF|Lab")
	FName Kind;

protected:
	UFUNCTION()
	void OnRep_Kind();

private:
	void ApplyLook();
	float Flicker = 0.f;
};
