#include "GFFootIK.h"

bool GFFootIK::SolveTwoBone(const FVector& Hip, const FVector& KneeIn, const FVector& AnkleIn, const FVector& AnkleTarget, FVector& OutKnee, FVector& OutAnkle)
{
	const float Upper = FVector::Distance(Hip, KneeIn);
	const float Lower = FVector::Distance(KneeIn, AnkleIn);
	if (Upper < 1.f || Lower < 1.f)
	{
		return false;
	}

	const float MaxReach = Upper + Lower - 0.5f;
	const float MinReach = FMath::Abs(Upper - Lower) + 0.5f;
	FVector ToTarget = AnkleTarget - Hip;
	float Dist = ToTarget.Size();
	if (Dist < KINDA_SMALL_NUMBER)
	{
		ToTarget = FVector::UpVector;
		Dist = 0.f;
	}
	const bool bReached = Dist <= MaxReach && Dist >= MinReach;
	const FVector Dir = ToTarget.GetSafeNormal();
	Dist = FMath::Clamp(Dist, MinReach, MaxReach);

	FVector Pole = KneeIn - Hip;
	Pole = Pole - Dir * FVector::DotProduct(Pole, Dir);
	if (Pole.SizeSquared() < 1.f)
	{
		const FVector Hint = FMath::Abs(Dir.Z) > 0.9f ? FVector::ForwardVector : FVector::UpVector;
		Pole = FVector::CrossProduct(Dir, Hint);
	}
	Pole = Pole.GetSafeNormal();

	const float Proj = (Upper * Upper - Lower * Lower + Dist * Dist) / (2.f * Dist);
	const float Height = FMath::Sqrt(FMath::Max(0.f, Upper * Upper - Proj * Proj));
	OutKnee = Hip + Dir * Proj + Pole * Height;
	OutAnkle = Hip + Dir * Dist;
	return bReached;
}

FQuat GFFootIK::AlignPrimary(const FQuat& Current, const FVector& CurrentPrimaryDir, const FVector& DesiredPrimaryDir)
{
	const FVector From = CurrentPrimaryDir.GetSafeNormal();
	const FVector To = DesiredPrimaryDir.GetSafeNormal();
	if (From.IsNearlyZero() || To.IsNearlyZero())
	{
		return Current;
	}
	return FQuat::FindBetweenNormals(From, To) * Current;
}
