#include "GFFootIKControlRig.h"

#include "GFFootIK.h"
#include "GFStairFlight.h"
#include "ControlRigComponent.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace GFFootIKRigDetail
{
	USkeletalMeshComponent* MeshOnActor(const UControlRig* Rig)
	{
		const UControlRigComponent* Comp = Rig ? Cast<UControlRigComponent>(Rig->GetOuter()) : nullptr;
		const AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
		if (!Owner)
		{
			return nullptr;
		}
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Owner);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->GetSkeletalMeshAsset())
			{
				return Mesh;
			}
		}
		return nullptr;
	}

	bool IsForwardsSolve(const FName& EventName)
	{
		return EventName == TEXT("ForwardsSolve") || EventName == TEXT("ForwardSolve");
	}
}

void UGFFootIKControlRig::SetHostingMesh(USkeletalMeshComponent* Mesh)
{
	HostMesh = Mesh;
	ImportBones();
}

void UGFFootIKControlRig::Initialize(bool bInitRigUnits)
{
	ImportBones();
	Super::Initialize(bInitRigUnits);
}

bool UGFFootIKControlRig::Execute(const FName& InEventName)
{
	// An empty rig has no VM. Skip the graph call so a missing Forwards Solve event does not log every tick.
	bool bRan = true;
	if (GetVM())
	{
		bRan = Super::Execute(InEventName);
	}
	if (GFFootIKRigDetail::IsForwardsSolve(InEventName))
	{
		ImportBones();
		PlantFeet();
		return true;
	}
	return bRan;
}

void UGFFootIKControlRig::ImportBones()
{
	URigHierarchy* Hierarchy = GetHierarchy();
	USkeletalMeshComponent* MeshComp = HostMesh.Get();
	if (!MeshComp)
	{
		MeshComp = GFFootIKRigDetail::MeshOnActor(this);
		HostMesh = MeshComp;
	}
	USkeletalMesh* Asset = MeshComp ? MeshComp->GetSkeletalMeshAsset() : nullptr;
	if (!Hierarchy || !Asset)
	{
		return;
	}
	if (Hierarchy->Find(FRigElementKey(TEXT("foot_l"), ERigElementType::Bone)) || Hierarchy->Find(FRigElementKey(TEXT("foot_r"), ERigElementType::Bone)))
	{
		return;
	}
	URigHierarchyController* Controller = Hierarchy->GetController(true);
	if (!Controller)
	{
		return;
	}

	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const TArray<FTransform>& Pose = Ref.GetRefBonePose();
	for (int32 Index = 0; Index < Pose.Num(); ++Index)
	{
		const FName Name = Ref.GetBoneName(Index);
		if (Name.IsNone())
		{
			continue;
		}
		const int32 ParentIndex = Ref.GetParentIndex(Index);
		FRigElementKey ParentKey;
		if (ParentIndex != INDEX_NONE)
		{
			ParentKey = FRigElementKey(Ref.GetBoneName(ParentIndex), ERigElementType::Bone);
		}
		Controller->AddBone(Name, ParentKey, Pose[Index], false, ERigBoneType::Imported, false, false);
	}
}

void UGFFootIKControlRig::PlantFeet()
{
	URigHierarchy* Hierarchy = GetHierarchy();
	if (!Hierarchy)
	{
		return;
	}
	const bool bCalf = Hierarchy->Find(FRigElementKey(TEXT("calf_l"), ERigElementType::Bone)) != nullptr;
	const FName KneeL = bCalf ? TEXT("calf_l") : TEXT("shin_l");
	const FName KneeR = bCalf ? TEXT("calf_r") : TEXT("shin_r");
	PlantChain(Hierarchy, TEXT("thigh_l"), KneeL, TEXT("foot_l"));
	PlantChain(Hierarchy, TEXT("thigh_r"), KneeR, TEXT("foot_r"));
}

bool UGFFootIKControlRig::PlantChain(URigHierarchy* Hierarchy, FName Thigh, FName Knee, FName Foot)
{
	const FRigElementKey ThighKey(Thigh, ERigElementType::Bone);
	const FRigElementKey KneeKey(Knee, ERigElementType::Bone);
	const FRigElementKey FootKey(Foot, ERigElementType::Bone);
	if (!Hierarchy->Find(ThighKey) || !Hierarchy->Find(KneeKey) || !Hierarchy->Find(FootKey))
	{
		return false;
	}

	USkeletalMeshComponent* MeshComp = HostMesh.Get();
	if (!MeshComp)
	{
		MeshComp = GFFootIKRigDetail::MeshOnActor(this);
		HostMesh = MeshComp;
	}
	if (!MeshComp)
	{
		return false;
	}

	const FTransform ThighX = Hierarchy->GetGlobalTransform(ThighKey);
	const FTransform KneeX = Hierarchy->GetGlobalTransform(KneeKey);
	const FTransform FootX = Hierarchy->GetGlobalTransform(FootKey);
	const FTransform MeshToWorld = MeshComp->GetComponentTransform();
	const FVector AnkleWorld = MeshToWorld.TransformPosition(FootX.GetLocation());

	float SurfaceZ = 0.f;
	if (!AGFStairFlight::SurfaceZAt(MeshComp->GetWorld(), AnkleWorld, SurfaceZ))
	{
		return false;
	}

	FVector TargetWorld = AnkleWorld;
	TargetWorld.Z = SurfaceZ + SoleOffsetCm;
	const FVector Target = MeshToWorld.InverseTransformPosition(TargetWorld);

	FVector NewKnee = KneeX.GetLocation();
	FVector NewAnkle = FootX.GetLocation();
	GFFootIK::SolveTwoBone(ThighX.GetLocation(), KneeX.GetLocation(), FootX.GetLocation(), Target, NewKnee, NewAnkle);

	const FQuat ThighRot = GFFootIK::AlignPrimary(ThighX.GetRotation(), KneeX.GetLocation() - ThighX.GetLocation(), NewKnee - ThighX.GetLocation());
	const FQuat KneeRot = GFFootIK::AlignPrimary(KneeX.GetRotation(), FootX.GetLocation() - KneeX.GetLocation(), NewAnkle - NewKnee);
	Hierarchy->SetGlobalTransform(ThighKey, FTransform(ThighRot, ThighX.GetLocation()), false, false, false);
	Hierarchy->SetGlobalTransform(KneeKey, FTransform(KneeRot, NewKnee), false, false, false);
	Hierarchy->SetGlobalTransform(FootKey, FTransform(FootX.GetRotation(), NewAnkle), false, false, false);
	return true;
}
