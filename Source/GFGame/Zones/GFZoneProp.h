#pragma once

// Replicated placeholder. Engine basic shapes are 100 cm. Grounded props lift by half their height.

#include "CoreMinimal.h"
#include "GFAssetMap.h"
#include "GameFramework/Actor.h"
#include "GFZoneProp.generated.h"

class UStaticMeshComponent;

USTRUCT()
struct FGFZonePropVisual
{
	GENERATED_BODY()

	UPROPERTY() EGFPlaceholderShape Shape = EGFPlaceholderShape::Cube;
	UPROPERTY() FLinearColor Color = FLinearColor::White;
	UPROPERTY() FVector Scale = FVector(1.f, 1.f, 1.f);
	UPROPERTY() float Yaw = 0.f;
	UPROPERTY() bool bSitOnGround = true;
};

UCLASS()
class GFGAME_API AGFZoneProp : public AActor
{
	GENERATED_BODY()

public:
	AGFZoneProp();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Configure(EGFPlaceholderShape Shape, const FLinearColor& Color, const FVector& Scale, float Yaw, bool bSitOnGround);

protected:
	void ApplyVisual();

	UFUNCTION()
	void OnRep_Visual();

	UPROPERTY(VisibleAnywhere, Category = "GF|Zone")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(ReplicatedUsing = OnRep_Visual)
	FGFZonePropVisual Visual;
};
