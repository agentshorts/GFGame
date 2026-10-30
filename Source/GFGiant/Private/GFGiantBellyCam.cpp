#include "GFGiantBellyCam.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AGFGiantBellyCam::AGFGiantBellyCam()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	bAlwaysRelevant = false;
	SetReplicatingMovement(false);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	SetRootComponent(Camera);
	Camera->FieldOfView = 70.f;
	Camera->PostProcessBlendWeight = 1.f;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = -6.f;
	Camera->PostProcessSettings.bOverride_ColorGain = true;
	Camera->PostProcessSettings.ColorGain = FVector4(0.35f, 0.08f, 0.1f, 1.f);
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 1.f;
}

void AGFGiantBellyCam::BeginPlay()
{
	Super::BeginPlay();
	TryShow();
}

void AGFGiantBellyCam::OnRep_Owner()
{
	Super::OnRep_Owner();
	if (HasActorBegunPlay())
	{
		TryShow();
	}
}

void AGFGiantBellyCam::TryShow()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!bShowingLocally && PC && PC->IsLocalController())
	{
		bShowingLocally = true;
		PC->SetViewTargetWithBlend(this, 0.2f);
	}
}

void AGFGiantBellyCam::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bShowingLocally)
	{
		APlayerController* PC = Cast<APlayerController>(GetOwner());
		if (PC && PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(PC->GetPawn(), 0.2f);
		}
		bShowingLocally = false;
	}
	Super::EndPlay(Reason);
}
