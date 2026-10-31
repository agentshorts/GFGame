#include "GFFootIKComponent.h"

#include "GFFootIKControlRig.h"
#include "GFMovementTuning.h"
#include "GFSimpleAnimComponent.h"
#include "Components/SkeletalMeshComponent.h"

UGFFootIKComponent::UGFFootIKComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGFFootIKComponent::BeginPlay()
{
	Super::BeginPlay();
	Bind();
}

void UGFFootIKComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bBound)
	{
		Bind();
	}
}

USkeletalMeshComponent* UGFFootIKComponent::FindMesh() const
{
	const AActor* Owner = GetOwner();
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

void UGFFootIKComponent::Bind()
{
	if (bBound)
	{
		return;
	}
	AActor* Owner = GetOwner();
	USkeletalMeshComponent* Mesh = FindMesh();
	if (!Owner || !Mesh)
	{
		return;
	}

	if (!Rig)
	{
		Rig = NewObject<UControlRigComponent>(Owner, TEXT("FootIKRig"));
		Rig->SetupAttachment(Mesh);
		Rig->SetHiddenInGame(true);
		Rig->SetVisibility(false);
		Rig->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rig->SetCanEverAffectNavigation(false);
		Rig->PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
		Rig->PrimaryComponentTick.bCanEverTick = true;
		Rig->PrimaryComponentTick.bStartWithTickEnabled = true;
		Rig->AddTickPrerequisiteComponent(Mesh);
		if (UGFSimpleAnimComponent* Anim = Owner->FindComponentByClass<UGFSimpleAnimComponent>())
		{
			Rig->AddTickPrerequisiteComponent(Anim);
		}
		Rig->RegisterComponent();
		Rig->SetControlRigClass(UGFFootIKControlRig::StaticClass());
		Rig->bUpdateRigOnTick = true;
		Rig->bResetTransformBeforeTick = true;
		Rig->AddMappedCompleteSkeletalMesh(Mesh, EControlRigComponentMapDirection::Input);
		Rig->AddMappedCompleteSkeletalMesh(Mesh, EControlRigComponentMapDirection::Output);
		Rig->Initialize();
	}

	UGFFootIKControlRig* FootRig = Cast<UGFFootIKControlRig>(Rig->GetControlRig());
	if (!FootRig)
	{
		return;
	}
	FootRig->SoleOffsetCm = GetDefault<UGFMovementTuning>()->FootSoleOffsetCm;
	FootRig->SetHostingMesh(Mesh);
	bBound = true;
	SetComponentTickEnabled(false);
}
