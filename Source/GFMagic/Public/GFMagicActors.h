#pragma once

// Cosmetic actors. The server credits the drop in the same step; these only fly to the player.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFMagicTypes.h"
#include "GFMagicActors.generated.h"

class UStaticMeshComponent;

UCLASS()
class GFMAGIC_API AGFMagicDropRibbon : public AActor
{
	GENERATED_BODY()

public:
	AGFMagicDropRibbon();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Setup(const FVector& InFrom, APawn* InTarget, const FVector& InFallbackTo, FName InItemId);

	UPROPERTY(Replicated)
	FVector From = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector FallbackTo = FVector::ZeroVector;

	UPROPERTY(Replicated)
	TObjectPtr<APawn> Target;

	UPROPERTY(Replicated)
	FName ItemId;

private:
	void ApplyMesh();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	float Age = 0.f;
	static constexpr float LifeSeconds = 0.45f;
};

/** A small sphere at the field, so a hold reads before Niagara lands. */
UCLASS()
class GFMAGIC_API AGFMagicFieldMarker : public AActor
{
	GENERATED_BODY()

public:
	AGFMagicFieldMarker();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetElement(EGFMagicElement InElement);

	UPROPERTY(ReplicatedUsing = OnRep_Element)
	EGFMagicElement Element = EGFMagicElement::Spark;

private:
	UFUNCTION()
	void OnRep_Element();
	void ApplyMesh();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;
};

/** The summon, sweeping the tunnel it just cut. */
UCLASS()
class GFMAGIC_API AGFMagicClaw : public AActor
{
	GENERATED_BODY()

public:
	AGFMagicClaw();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Setup(const FVector& InFrom, const FVector& InTo, FName InForm, const FLinearColor& InColor);

	UPROPERTY(Replicated)
	FVector From = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector To = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FName Form;

	UPROPERTY(ReplicatedUsing = OnRep_Color)
	FLinearColor Color = FLinearColor::White;

private:
	UFUNCTION()
	void OnRep_Color();
	void ApplyMesh();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	float Age = 0.f;
	static constexpr float LifeSeconds = 0.4f;
};
