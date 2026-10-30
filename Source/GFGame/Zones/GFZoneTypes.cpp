#include "GFZoneTypes.h"

bool FGFZoneRow::IsShapeValid() const
{
	if (Shape == EGFZoneShape::Circle)
	{
		return Radius > 0.f;
	}
	if (Shape == EGFZoneShape::Ring)
	{
		return Radius > InnerRadius && InnerRadius >= 0.f;
	}
	return HalfExtent.X > 0.f && HalfExtent.Y > 0.f;
}

bool FGFZoneRow::ParseBias(float& Delver, float& Wrangler, float& Scavenger) const
{
	TArray<FString> Parts;
	ResourceBias.ParseIntoArray(Parts, TEXT(";"), true);
	if (Parts.Num() != 3)
	{
		return false;
	}
	Delver = FCString::Atof(*Parts[0]);
	Wrangler = FCString::Atof(*Parts[1]);
	Scavenger = FCString::Atof(*Parts[2]);
	return true;
}

bool FGFZoneRow::ContainsXY(const FVector& Location) const
{
	return DistanceOutside(Location) <= 0.f;
}

float FGFZoneRow::DistanceOutside(const FVector& Location) const
{
	const float DX = Location.X - Center.X;
	const float DY = Location.Y - Center.Y;
	if (Shape == EGFZoneShape::Box)
	{
		const float OutX = FMath::Max(0.f, FMath::Abs(DX) - FMath::Abs(HalfExtent.X));
		const float OutY = FMath::Max(0.f, FMath::Abs(DY) - FMath::Abs(HalfExtent.Y));
		return FMath::Sqrt(OutX * OutX + OutY * OutY);
	}
	const float Dist = FMath::Sqrt(DX * DX + DY * DY);
	if (Shape == EGFZoneShape::Ring)
	{
		if (Dist < InnerRadius)
		{
			return InnerRadius - Dist;
		}
		if (Dist > Radius)
		{
			return Dist - Radius;
		}
		return 0.f;
	}
	return FMath::Max(0.f, Dist - Radius);
}

float FGFZoneAmbienceRow::ClampedExposure() const
{
	return FMath::Clamp(ExposureOffset, -FGFZoneConstants::ExposureLimit, FGFZoneConstants::ExposureLimit);
}

float FGFZoneAmbienceRow::BlendTime() const
{
	return BlendSeconds > 0.f ? BlendSeconds : FGFZoneConstants::DefaultBlendSeconds;
}
