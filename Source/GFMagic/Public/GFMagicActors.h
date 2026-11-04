#pragma once

// Cosmetic actors. The server credits the drop in the same step; these only fly to the player.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFMagicTypes.h"
#include "NiagaraComponent.h"
#include "GFMagicActors.generated.h"

class UStaticMeshComponent;
class USpotLightComponent;

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

/** Field beam. Niagara per element when DT_MiningFX resolves, otherwise the engine cone or sphere, plus the spot light. */
UCLASS()
class GFMAGIC_API AGFMagicFieldMarker : public AActor
{
	GENERATED_BODY()

public:
	AGFMagicFieldMarker();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetBeam(EGFMagicElement InElement, const FVector& Dir, float HalfAngleDeg, float RangeCm, float Intensity);

	/** True when no Niagara system resolved and the engine mesh is showing. */
	bool IsUsingMeshFallback() const { return bMeshFallback; }

	UPROPERTY(ReplicatedUsing = OnRep_Beam)
	EGFMagicElement Element = EGFMagicElement::Spark;

	UPROPERTY(ReplicatedUsing = OnRep_Beam)
	FVector BeamDir = FVector(0.f, 0.f, -1.f);

	UPROPERTY(ReplicatedUsing = OnRep_Beam)
	float BeamHalfAngle = 15.f;

	UPROPERTY(ReplicatedUsing = OnRep_Beam)
	float BeamRangeCm = 800.f;

	UPROPERTY(ReplicatedUsing = OnRep_Beam)
	float BeamIntensity = 0.f;

private:
	UFUNCTION()
	void OnRep_Beam();
	void ApplyMesh();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> FX;

	UPROPERTY()
	TObjectPtr<USpotLightComponent> Light;

	bool bMeshFallback = true;
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
