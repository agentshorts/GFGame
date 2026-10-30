#pragma once

// Greybox underground jail cell: a floor, four walls and a ceiling (block-all boxes) around a spot far below the hub.
// Replicated so clients collide with it too. Art replaces the boxes later.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFJailCell.generated.h"

class UBoxComponent;

UCLASS()
class GFSOCIAL_API AGFJailCell : public AActor
{
	GENERATED_BODY()

public:
	AGFJailCell();

	/** Inner half size (cm). */
	static constexpr float HalfX = 300.f;
	static constexpr float HalfZ = 200.f;
	static constexpr float Wall = 50.f;

	/** Where a jailed pawn stands (centre, just above the floor). */
	FVector GetStandSpot() const { return GetActorLocation() + FVector(0.f, 0.f, 100.f); }

	UPROPERTY(VisibleAnywhere, Category = "GF|Social") TObjectPtr<UBoxComponent> Floor;
	UPROPERTY(VisibleAnywhere, Category = "GF|Social") TArray<TObjectPtr<UBoxComponent>> Walls;
};
