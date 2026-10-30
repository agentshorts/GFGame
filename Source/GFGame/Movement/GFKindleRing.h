#pragma once

// Hearth volume. Overlap gives the player infinite stamina (the pool stays full, dashes still obey their limits).
// The hub spawns one from UGFMovementTuning when the map is not a mini-game and none is already placed.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFKindleRing.generated.h"

class USphereComponent;

UCLASS()
class GFGAME_API AGFKindleRing : public AActor
{
	GENERATED_BODY()

public:
	AGFKindleRing();

	void ApplyRadius(float RadiusCm);
	float GetRadius() const;

	virtual void BeginPlay() override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Movement")
	TObjectPtr<USphereComponent> Sphere;

private:
	void RefreshPawn(AActor* Other) const;
};
