#pragma once

// The giant's body: a reusable, replicated greybox actor (DT_AssetMap placeholder for DT_GiantDino.AssetKey, scaled by
// MeshScale). Art is the skeletal mesh for that same asset key when the row has one; Body stays the placeholder.
// It knows nothing about the event: whoever drives it (today UGFGiantEventComponent; later e.g. a tamed
// companion mode owned by the host) moves it with MoveBody and listens to OnAttackedBy. It has no health and never
// damages anyone.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFGiantTypes.h"
#include "GFGiantDino.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UGFSimpleAnimComponent;
class AGFPlayerState;

DECLARE_MULTICAST_DELEGATE_TwoParams(FGFGiantAttackedBy, class AGFGiantDino* /*Dino*/, AGFPlayerState* /*By*/);

UCLASS()
class GFGIANT_API AGFGiantDino : public AActor
{
	GENERATED_BODY()

public:
	AGFGiantDino();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(VisibleAnywhere, Category = "GF|Giant") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, Category = "GF|Giant") TObjectPtr<USceneComponent> ScaleRoot;
	UPROPERTY(VisibleAnywhere, Category = "GF|Giant") TObjectPtr<UStaticMeshComponent> Body;
	/** Skeletal art under ScaleRoot (MeshScale still scales both). Hidden when the asset key has no skeletal mesh. */
	UPROPERTY(VisibleAnywhere, Category = "GF|Giant") TObjectPtr<USkeletalMeshComponent> Art;
	UPROPERTY(VisibleAnywhere, Category = "GF|Giant") TObjectPtr<UGFSimpleAnimComponent> SimpleAnim;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Giant") EGFGiantState State = EGFGiantState::None;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Giant") FString TargetKey;
	UPROPERTY(ReplicatedUsing = OnRep_Look, BlueprintReadOnly, Category = "GF|Giant") FString DisplayName;
	UPROPERTY(ReplicatedUsing = OnRep_Look, BlueprintReadOnly, Category = "GF|Giant") FName AssetKey;
	UPROPERTY(ReplicatedUsing = OnRep_Look, BlueprintReadOnly, Category = "GF|Giant") float MeshScale = 3.5f;

	/** Server: a player hit it (from TakeDamage or a Hit request routed here). Its driver decides what that means. */
	FGFGiantAttackedBy OnAttackedBy;
	/** Server: report an attack by a player (broadcasts OnAttackedBy). */
	void NotifyAttackedBy(AGFPlayerState* By);
	/** Server: move the body (horizontal; keeps its height) and face FaceToward if given. */
	void MoveBody(const FVector& Location, const FVector* FaceToward = nullptr);

	void SetLook(const FString& InName, FName InAssetKey, float InScale);

protected:
	UFUNCTION()
	void OnRep_Look();
	void ApplyLook();
};
