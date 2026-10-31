#include "GFStairMovement.h"

#include "GFDino.h"
#include "GFFootIKComponent.h"
#include "GFMovementTuning.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

void GFStairMovement::Apply(UCharacterMovementComponent* Move, float StepHeightCm, float WalkableFloorAngleDeg)
{
	if (!Move)
	{
		return;
	}
	float Step = FMath::Max(0.f, StepHeightCm);
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Move->UpdatedComponent))
	{
		const float Limit = 2.f * Capsule->GetUnscaledCapsuleHalfHeight() - Capsule->GetUnscaledCapsuleRadius();
		if (Limit > 1.f)
		{
			Step = FMath::Min(Step, Limit - 1.f);
		}
	}
	Move->MaxStepHeight = Step;
	Move->SetWalkableFloorAngle(FMath::Clamp(WalkableFloorAngleDeg, 0.f, 89.f));
}

namespace GFStairDinoHook
{
	void Prepare(AGFDino* Dino)
	{
		if (!Dino)
		{
			return;
		}
		const UGFMovementTuning& Tuning = *GetDefault<UGFMovementTuning>();
		GFStairMovement::Apply(Dino->GetCharacterMovement(), Tuning.MaxStepHeight, Tuning.WalkableFloorAngle);
		UGFFootIKComponent* IK = Dino->FindComponentByClass<UGFFootIKComponent>();
		if (!IK)
		{
			IK = NewObject<UGFFootIKComponent>(Dino, TEXT("FootIK"));
			if (IK)
			{
				IK->RegisterComponent();
				Dino->AddInstanceComponent(IK);
			}
		}
		if (IK)
		{
			IK->Bind();
		}
	}
}

void GFInstallDinoStairHook()
{
	GFSetDinoReadyHook(&GFStairDinoHook::Prepare);
}
