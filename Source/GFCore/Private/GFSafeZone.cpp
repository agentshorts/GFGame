#include "GFSafeZone.h"
#include "Engine/World.h"

float FGFSafeRegion::Radius() const
{
	const int32 Extra = FMath::Max(0, Members - 1);
	return FMath::Max(0.f, FMath::Min(BaseRadius + RadiusPerExtraMember * Extra, FMath::Max(MaxRadius, BaseRadius)));
}

bool FGFSafeCircles::IsInsideSafe(const FVector& Location) const
{
	for (const FGFSafeRegion& R : Regions)
	{
		if (FVector::DistSquared2D(R.Center, Location) <= FMath::Square(R.Radius()))
		{
			return true;
		}
	}
	return false;
}

FVector FGFSafeCircles::ClosestSafePoint(const FVector& Location) const
{
	if (Regions.Num() == 0 || IsInsideSafe(Location))
	{
		return Location;
	}
	FVector Best = Location;
	float BestD = TNumericLimits<float>::Max();
	for (const FGFSafeRegion& R : Regions)
	{
		const FVector Dir = (Location - R.Center).GetSafeNormal2D();
		FVector P = R.Center + Dir * R.Radius();
		P.Z = Location.Z;
		const float D = FVector::Dist2D(P, Location);
		if (D < BestD)
		{
			BestD = D;
			Best = P;
		}
	}
	return Best;
}

bool FGFSafeCircles::ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const
{
	if (Regions.Num() == 0)
	{
		return false;
	}
	// The nearest border point of the union: either a radial projection onto one circle or a point where two circles
	// cross, and not covered by another circle. If every candidate is covered, use the nearest candidate anyway.
	struct FCand { FVector P; FVector N; bool bCovered; };
	TArray<FCand, TInlineAllocator<16>> Cands;
	auto Covered = [&](const FVector& P, int32 SkipA, int32 SkipB)
	{
		for (int32 j = 0; j < Regions.Num(); ++j)
		{
			if (j != SkipA && j != SkipB && FVector::Dist2D(Regions[j].Center, P) < Regions[j].Radius() - 1.f)
			{
				return true;
			}
		}
		return false;
	};
	for (int32 i = 0; i < Regions.Num(); ++i)
	{
		const FGFSafeRegion& R = Regions[i];
		FVector Dir = (Location - R.Center).GetSafeNormal2D();
		if (Dir.IsNearlyZero())
		{
			Dir = FVector(1.f, 0.f, 0.f);
		}
		const FVector P(R.Center.X + Dir.X * R.Radius(), R.Center.Y + Dir.Y * R.Radius(), Location.Z);
		Cands.Add({ P, Dir, Covered(P, i, INDEX_NONE) });
	}
	for (int32 i = 0; i < Regions.Num(); ++i)
	{
		for (int32 j = i + 1; j < Regions.Num(); ++j)
		{
			const FVector2D A(Regions[i].Center), B(Regions[j].Center);
			const float Ra = Regions[i].Radius(), Rb = Regions[j].Radius();
			const float D = FVector2D::Distance(A, B);
			if (D < 1e-3f || D > Ra + Rb || D < FMath::Abs(Ra - Rb))
			{
				continue;
			}
			const float X = (D * D + Ra * Ra - Rb * Rb) / (2.f * D);
			const float H = FMath::Sqrt(FMath::Max(0.f, Ra * Ra - X * X));
			const FVector2D U = (B - A) / D;
			const FVector2D M = A + U * X;
			for (const float S : { 1.f, -1.f })
			{
				const FVector2D Q(M.X - U.Y * H * S, M.Y + U.X * H * S);
				const FVector2D N = ((Q - A).GetSafeNormal() + (Q - B).GetSafeNormal()).GetSafeNormal();
				const FVector P(Q.X, Q.Y, Location.Z);
				Cands.Add({ P, FVector(N.X, N.Y, 0.f), Covered(P, i, j) });
			}
		}
	}
	const FCand* Best = nullptr;
	const FCand* BestAny = nullptr;
	for (const FCand& C : Cands)
	{
		const float D = FVector::DistSquared2D(C.P, Location);
		if (!C.bCovered && (!Best || D < FVector::DistSquared2D(Best->P, Location)))
		{
			Best = &C;
		}
		if (!BestAny || D < FVector::DistSquared2D(BestAny->P, Location))
		{
			BestAny = &C;
		}
	}
	const FCand* Pick = Best ? Best : BestAny;
	OutPoint = Pick->P;
	OutOutward = Pick->N.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Pick->N;
	return true;
}

float FGFSafeZone::DistanceOutside(const IGFSafeZoneQuery& Zone, const FVector& P)
{
	if (!Zone.HasAnySafe() || Zone.IsInsideSafe(P))
	{
		return 0.f;
	}
	return FVector::Dist2D(Zone.ClosestSafePoint(P), P);
}

FVector FGFSafeZone::DirToSafe(const IGFSafeZoneQuery& Zone, const FVector& P)
{
	if (!Zone.HasAnySafe())
	{
		return FVector::ZeroVector;
	}
	if (!Zone.IsInsideSafe(P))
	{
		const FVector D = (Zone.ClosestSafePoint(P) - P).GetSafeNormal2D();
		if (!D.IsNearlyZero())
		{
			return D;
		}
	}
	FVector EdgeP, Outward;
	return Zone.ClosestEdge(P, EdgeP, Outward) ? -Outward : FVector::ZeroVector;
}

bool FGFSafeZone::IsClear(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin)
{
	if (!Zone.HasAnySafe())
	{
		return true;
	}
	return !Zone.IsInsideSafe(P) && DistanceOutside(Zone, P) >= Margin - 0.5f;
}

FVector FGFSafeZone::KeepOut(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin)
{
	FVector Out = P;
	for (int32 Iter = 0; Iter < 12 && !IsClear(Zone, Out, Margin); ++Iter)
	{
		FVector EdgeP, Outward;
		if (!Zone.ClosestEdge(Out, EdgeP, Outward))
		{
			break;
		}
		Out = EdgeP + Outward * (Margin + 0.5f);
		Out.Z = P.Z;
	}
	if (IsClear(Zone, Out, Margin))
	{
		return Out;
	}
	// Awkward corners between regions: search outward in 32 directions for the nearest clear spot.
	float BestD = TNumericLimits<float>::Max();
	for (int32 k = 0; k < 32; ++k)
	{
		const float A = 2.f * PI * k / 32.f;
		const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
		for (int32 Stepi = 1; Stepi <= 400 && Stepi * 50.f < BestD; ++Stepi)
		{
			const FVector Q = P + Dir * (Stepi * 50.f);
			if (IsClear(Zone, Q, Margin))
			{
				BestD = Stepi * 50.f;
				Out = Q;
				break;
			}
		}
	}
	return Out;
}

FVector FGFSafeZone::StepOutside(const IGFSafeZoneQuery& Zone, const FVector& From, const FVector& Goal, float StepLen, float Margin)
{
	FVector Want = Goal - From;
	Want.Z = 0.f;
	const float Len = FMath::Min(Want.Size(), StepLen);
	if (Len <= KINDA_SMALL_NUMBER)
	{
		return KeepOut(Zone, From, Margin);
	}
	const FVector Dir = Want.GetSafeNormal();
	FVector Next = From + Dir * Len;
	if (!IsClear(Zone, Next, Margin))
	{
		// Slide along the border instead of pushing into it.
		FVector EdgeP, Outward;
		if (Zone.ClosestEdge(From, EdgeP, Outward))
		{
			FVector Tangent(-Outward.Y, Outward.X, 0.f);
			if (FVector::DotProduct(Tangent, Dir) < 0.f)
			{
				Tangent = -Tangent;
			}
			Next = From + Tangent * Len;
		}
	}
	return KeepOut(Zone, Next, Margin);
}

// ---------------- Subsystem ----------------

UGFSafeZoneSubsystem* UGFSafeZoneSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGFSafeZoneSubsystem>() : nullptr;
}

void UGFSafeZoneSubsystem::RegisterProvider(UObject* Provider)
{
	if (Provider && Cast<IGFSafeZoneProvider>(Provider))
	{
		Providers.Remove(Provider);
		Providers.Add(Provider);
	}
}

void UGFSafeZoneSubsystem::UnregisterProvider(UObject* Provider)
{
	Providers.Remove(Provider);
}

const IGFSafeZoneQuery* UGFSafeZoneSubsystem::GetZone() const
{
	if (Override.IsValid())
	{
		return Override.Get();
	}
	for (int32 i = Providers.Num() - 1; i >= 0; --i)
	{
		if (const IGFSafeZoneProvider* P = Cast<IGFSafeZoneProvider>(Providers[i].Get()))
		{
			if (const IGFSafeZoneQuery* Q = P->GetSafeZoneQuery())
			{
				return Q;
			}
		}
	}
	return nullptr;
}

bool UGFSafeZoneSubsystem::IsInsideSafe(const FVector& Location) const
{
	const IGFSafeZoneQuery* Q = GetZone();
	return Q && Q->HasAnySafe() && Q->IsInsideSafe(Location);
}
