#pragma once

// Binds a UGFFootIKControlRig to the owner's skeletal mesh after the locomotion clip is chosen.
// The rig ticks in PostUpdateWork, after UGFSimpleAnimComponent, and writes the planted pose back.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ControlRigComponent.h"
#include "GFFootIKComponent.generated.h"

class USkeletalMeshComponent;

UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFGAME_API UGFFootIKComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFFootIKComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Creates the rig once the skeletal mesh has an asset. Does nothing until then.
	 * A later call is a no-op after a successful bind.
	 */
	void Bind();

private:
	USkeletalMeshComponent* FindMesh() const;

	UPROPERTY(Transient)
	TObjectPtr<UControlRigComponent> Rig;

	bool bBound = false;
};
