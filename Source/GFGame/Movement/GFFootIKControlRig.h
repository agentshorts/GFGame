#pragma once

// Code-built Control Rig. Bones are imported from the skeletal mesh at runtime, then Forwards Solve
// plants each foot on the stair tread under it. There is no Control Rig asset.

#include "CoreMinimal.h"
#include "ControlRig.h"
#include "GFFootIKControlRig.generated.h"

class USkeletalMeshComponent;
class URigHierarchy;

UCLASS()
class GFGAME_API UGFFootIKControlRig : public UControlRig
{
	GENERATED_BODY()

public:
	virtual void Initialize(bool bInitRigUnits) override;
	virtual bool Execute(const FName& InEventName) override;

	/** Ankle height above the tread, cm. UGFMovementTuning::FootSoleOffsetCm. */
	UPROPERTY(EditAnywhere, Category = "GF|Stair")
	float SoleOffsetCm = 12.5f;

	/** Mesh whose reference skeleton fills the hierarchy, and whose component space the solve uses. */
	void SetHostingMesh(USkeletalMeshComponent* Mesh);

	/** Adds the reference-pose bones once. Safe to call again. */
	void ImportBones();

private:
	void PlantFeet();
	bool PlantChain(URigHierarchy* Hierarchy, FName Thigh, FName Knee, FName Foot);

	TWeakObjectPtr<USkeletalMeshComponent> HostMesh;
};
