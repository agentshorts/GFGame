#include "GFMagicStairwell.h"
#include "GFMagicRules.h"
#include "GFMagicTypes.h"
#include "GFVoxelWorld.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Pawn.h"

FGFGrumbleDigContext FGFMagicStairwell::Context()
{
	FGFGrumbleDigContext Out;
	Out.TameState = EGFTameState::Tamed;
	Out.Species = FGFGrumbleDig::SpeciesId;
	Out.DisplayName = TEXT("Big Grumble");
	return Out;
}

FGFGrumbleDigTuning FGFMagicStairwell::Tuning()
{
	FGFGrumbleDigTuning Out;
	Out.CapsuleHeightCm = 180.f;
	Out.CapsuleRadiusCm = 40.f;
	Out.MinTravelCm = 1.f;
	Out.MaxStepHeightCm = GFBlockSize;
	return Out;
}

FGFGrumbleDigMove FGFMagicStairwell::MoveFor(const APawn* Pawn, const FVector& AimPoint)
{
	FGFGrumbleDigMove Move;
	if (!Pawn)
	{
		return Move;
	}
	float Half = 88.f;
	if (const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>())
	{
		Half = Capsule->GetScaledCapsuleHalfHeight();
	}
	Move.Position = Pawn->GetActorLocation() - FVector(0.f, 0.f, Half);
	const FVector Dir = (AimPoint - Pawn->GetActorLocation()).GetSafeNormal();
	const FVector Heading = Dir.IsNearlyZero() ? FVector::ForwardVector : Dir;
	Move.Velocity = Heading * 600.f;
	Move.PitchDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Heading.Z, -1.f, 1.f)));
	Move.FacingYawDegrees = Heading.Rotation().Yaw;
	Move.DeltaSeconds = 1.f;
	return Move;
}

TArray<FIntVector> FGFMagicStairwell::Cells(const APawn* Pawn, const FVector& AimPoint, const AGFVoxelWorld* Voxel)
{
	if (!Pawn || !Voxel)
	{
		return {};
	}
	const FGFGrumbleDigMove Move = MoveFor(Pawn, AimPoint);
	const FGFGrumbleDigResult Planned = FGFGrumbleDig::Plan(Move, Context(), Tuning());
	const FVector Dir = (AimPoint - Pawn->GetActorLocation()).GetSafeNormal();
	const FIntVector Feet = Voxel->WorldToCell(Move.Position);
	return FGFMagicRules::CapStair(Planned.Cells, Feet, Dir.IsNearlyZero() ? FVector::ForwardVector : Dir, GFMagic::CellsPerTick);
}
