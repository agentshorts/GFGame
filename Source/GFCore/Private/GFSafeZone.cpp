#include "GFSafeZone.h"
#include "Engine/World.h"

float FGFSafeRegion::Radius() const
{
	const int32 Extra = FMath::Max(0, Members - 1);
	return FMath::Max(0.f, FMath::Min(BaseRadius + RadiusPerExtraMember * Extra, FMath::Max(MaxRadius, BaseRadius)));
}

bool FGFSafeCircles::SafeRegionAt(int32 Index, FName& OutId, FVector& OutCenter, float& OutRadius) const
{
	if (!Regions.IsValidIndex(Index))
	{
		return false;
	}
	const FGFSafeRegion& Region = Regions[Index];
	OutId = Region.Id;
	OutCenter = Region.Center;
	OutRadius = Region.Radius();
	return OutRadius > 0.f;
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

namespace GFSafeZonePrivate
{
	/** KeepOut's cheap part: push out from the nearest border, a few times. May end up still not clear. */
	static FVector PushFromEdges(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin, int32 MaxIter)
	{
		FVector Out = P;
		for (int32 Iter = 0; Iter < MaxIter && !FGFSafeZone::IsClear(Zone, Out, Margin); ++Iter)
		{
			FVector EdgeP, Outward;
			if (!Zone.ClosestEdge(Out, EdgeP, Outward))
			{
				break;
			}
			Out = EdgeP + Outward * (Margin + 0.5f);
			Out.Z = P.Z;
		}
		return Out;
	}
}

FVector FGFSafeZone::KeepOut(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin)
{
	FVector Out = GFSafeZonePrivate::PushFromEdges(Zone, P, Margin, 12);
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

bool FGFSafeZone::PushOut(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin, FVector& Out, int32 MaxIter)
{
	const FVector Pushed = GFSafeZonePrivate::PushFromEdges(Zone, P, Margin, MaxIter);
	if (!IsClear(Zone, Pushed, Margin))
	{
		return false;
	}
	Out = Pushed;
	return true;
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
	const FVector Next = From + Dir * Len;
	if (IsClear(Zone, Next, Margin))
	{
		return Next;
	}
	// Slide along the border instead of pushing into it.
	FVector EdgeP, Outward;
	FVector Tangent = FVector::ZeroVector;
	if (Zone.ClosestEdge(From, EdgeP, Outward))
	{
		Tangent = FVector(-Outward.Y, Outward.X, 0.f).GetSafeNormal();
		if (FVector::DotProduct(Tangent, Dir) < 0.f)
		{
			Tangent = -Tangent;
		}
	}
	if (!IsClear(Zone, From, Margin))
	{
		// Safety grew under it: out at once, as far as it takes.
		return KeepOut(Zone, Tangent.IsNearlyZero() ? Next : From + Tangent * Len, Margin);
	}
	// From a clear spot: at most one step. Slide (pushed back out from the border, the cheap way)...
	if (!Tangent.IsNearlyZero())
	{
		const FVector Slid = GFSafeZonePrivate::PushFromEdges(Zone, From + Tangent * Len, Margin, 4);
		if (IsClear(Zone, Slid, Margin) && FVector::Dist2D(Slid, From) <= Len + 1.f)
		{
			return Slid;
		}
	}
	// ...else the clear direction nearest the wanted one (15 degree steps, either side), else stay put: never a jump
	// across or past a gap too narrow to pass.
	for (int32 k = 1; k <= 12; ++k)
	{
		for (const float Side : { 1.f, -1.f })
		{
			const FVector P = From + Dir.RotateAngleAxis(Side * 15.f * k, FVector::UpVector) * Len;
			if (IsClear(Zone, P, Margin))
			{
				return P;
			}
		}
	}
	return From;
}

// ---------------- No-entry boxes ----------------

bool FGFNoEntry::IsInside(const TArray<FBox>& Boxes, const FVector& P)
{
	return Boxes.ContainsByPredicate([&](const FBox& B) { return B.IsValid && B.IsInsideOrOn(P); });
}

FVector FGFNoEntry::KeepOut(const TArray<FBox>& Boxes, const FVector& P, float Radius, float HalfHeight)
{
	FVector Out = P;
	for (int32 Iter = 0; Iter < 4; ++Iter)
	{
		bool bMoved = false;
		for (const FBox& B : Boxes)
		{
			if (!B.IsValid || B.Max.Z < P.Z - HalfHeight || B.Min.Z > P.Z + HalfHeight)
			{
				continue;
			}
			const float MinX = B.Min.X - Radius, MaxX = B.Max.X + Radius;
			const float MinY = B.Min.Y - Radius, MaxY = B.Max.Y + Radius;
			if (Out.X <= MinX || Out.X >= MaxX || Out.Y <= MinY || Out.Y >= MaxY)
			{
				continue;
			}
			// Out to the nearest side (so a mover slides along the box instead of stopping).
			const float DX0 = Out.X - MinX, DX1 = MaxX - Out.X, DY0 = Out.Y - MinY, DY1 = MaxY - Out.Y;
			const float M = FMath::Min(FMath::Min(DX0, DX1), FMath::Min(DY0, DY1));
			if (M == DX0) { Out.X = MinX - 0.5f; }
			else if (M == DX1) { Out.X = MaxX + 0.5f; }
			else if (M == DY0) { Out.Y = MinY - 0.5f; }
			else { Out.Y = MaxY + 0.5f; }
			bMoved = true;
		}
		if (!bMoved)
		{
			break;
		}
	}
	return Out;
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

bool UGFSafeZoneSubsystem::IsSafeFor(const FGFSafeSubject& Who, const FVector& Location) const
{
	// Owned land answers through its owner (territory knows protection), even when a newer provider (the giant's
	// circles) would be the current zone. Unowned land falls back to the current zone, as before.
	FName Owner;
	const IGFSafeZoneQuery* Q = OwnerZoneAt(Location, Owner);
	if (!Q)
	{
		Q = GetZone();
	}
	return Q && Q->HasAnySafe() && Q->IsSafeFor(Who, Location);
}

bool UGFSafeZoneSubsystem::IsHazardAt(const FVector& Location, const UObject* Skip) const
{
	if (Override.IsValid() && Override->IsHazardAt(Location))
	{
		return true;
	}
	for (int32 i = Providers.Num() - 1; i >= 0; --i)
	{
		const UObject* Obj = Providers[i].Get();
		if (!Obj || Obj == Skip)
		{
			continue;
		}
		if (const IGFSafeZoneProvider* P = Cast<IGFSafeZoneProvider>(Obj))
		{
			if (const IGFSafeZoneQuery* Q = P->GetHazardQuery())
			{
				if (Q->IsHazardAt(Location))
				{
					return true;
				}
			}
		}
	}
	return false;
}

FName UGFSafeZoneSubsystem::OwnerAt(const FVector& Location) const
{
	FName Owner;
	OwnerZoneAt(Location, Owner);
	return Owner;
}

const IGFSafeZoneQuery* UGFSafeZoneSubsystem::OwnerZoneAt(const FVector& Location, FName& OutOwner) const
{
	OutOwner = NAME_None;
	if (Override.IsValid())
	{
		OutOwner = Override->OwnerAt(Location);
		return OutOwner.IsNone() ? nullptr : Override.Get();
	}
	for (int32 i = Providers.Num() - 1; i >= 0; --i)
	{
		if (const IGFSafeZoneProvider* P = Cast<IGFSafeZoneProvider>(Providers[i].Get()))
		{
			if (const IGFSafeZoneQuery* Q = P->GetSafeZoneQuery())
			{
				OutOwner = Q->OwnerAt(Location);
				if (!OutOwner.IsNone())
				{
					return Q;
				}
			}
		}
	}
	return nullptr;
}

void UGFSafeZoneSubsystem::SetNoEntry(const UObject* Owner, const FBox& Box)
{
	if (!Owner)
	{
		return;
	}
	ClearNoEntry(Owner);
	NoEntry.Add({ Owner, Box });
}

void UGFSafeZoneSubsystem::ClearNoEntry(const UObject* Owner)
{
	NoEntry.RemoveAll([&](const TPair<TWeakObjectPtr<const UObject>, FBox>& E) { return !E.Key.IsValid() || E.Key.Get() == Owner; });
}

TArray<const IGFSafeZoneQuery*> UGFSafeZoneSubsystem::GetProviderZones(const UObject* Skip) const
{
	TArray<const IGFSafeZoneQuery*> Out;
	AppendProviderZones(Out, Skip);
	return Out;
}

void UGFSafeZoneSubsystem::AppendProviderZones(TArray<const IGFSafeZoneQuery*>& Out, const UObject* Skip) const
{
	for (int32 i = Providers.Num() - 1; i >= 0; --i)
	{
		UObject* Obj = Providers[i].Get();
		if (!Obj || Obj == Skip)
		{
			continue;
		}
		if (const IGFSafeZoneProvider* P = Cast<IGFSafeZoneProvider>(Obj))
		{
			if (const IGFSafeZoneQuery* Q = P->GetSafeZoneQuery())
			{
				Out.Add(Q);
			}
		}
	}
}

TArray<FBox> UGFSafeZoneSubsystem::GetNoEntry() const
{
	TArray<FBox> Out;
	for (const TPair<TWeakObjectPtr<const UObject>, FBox>& E : NoEntry)
	{
		if (E.Key.IsValid())
		{
			Out.Add(E.Value);
		}
	}
	return Out;
}


// ---------------- Union ----------------

FGFSafeZoneUnion::FPartsScope::FPartsScope(const FGFSafeZoneUnion* InUnion)
	: Union(InUnion)
{
	if (Union)
	{
		++Union->ScopeDepth;
	}
}

FGFSafeZoneUnion::FPartsScope::~FPartsScope()
{
	if (Union && --Union->ScopeDepth <= 0)
	{
		Union->ScopeDepth = 0;
		Union->bPartsCached = false;
	}
}

const TArray<const IGFSafeZoneQuery*>& FGFSafeZoneUnion::Parts() const
{
	if (ScopeDepth > 0 && bPartsCached)
	{
		return PartsBuffer;
	}
	PartsBuffer.Reset();
	ProviderBuffer.Reset();
	if (const UGFSafeZoneSubsystem* Sub = Zones.Get())
	{
		Sub->AppendProviderZones(ProviderBuffer, Skip.Get());
	}
	for (const IGFSafeZoneQuery* Q : ProviderBuffer)
	{
		if (Q && Q != this && Q != Own && Q->HasAnySafe())
		{
			PartsBuffer.Add(Q);
		}
	}
	if (Own && Own->HasAnySafe() && !(bOwnOnlyWithoutLand && PartsBuffer.Num() > 0))
	{
		PartsBuffer.Insert(Own, 0);
	}
	bPartsCached = ScopeDepth > 0;
	return PartsBuffer;
}

bool FGFSafeZoneUnion::IsHazardAt(const FVector& Location) const
{
	if (Depth > 0)
	{
		return false;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	for (const IGFSafeZoneQuery* Q : Parts())
	{
		if (Q->IsHazardAt(Location))
		{
			return true;
		}
	}
	return false;
}

int32 FGFSafeZoneUnion::SafeRegionCount() const
{
	if (Depth > 0)
	{
		return 0;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	int32 Count = 0;
	for (const IGFSafeZoneQuery* Q : Parts())
	{
		Count += Q->SafeRegionCount();
	}
	return Count;
}

bool FGFSafeZoneUnion::SafeRegionAt(int32 Index, FName& OutId, FVector& OutCenter, float& OutRadius) const
{
	if (Depth > 0)
	{
		return false;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	for (const IGFSafeZoneQuery* Q : Parts())
	{
		const int32 Count = Q->SafeRegionCount();
		if (Index < Count)
		{
			return Q->SafeRegionAt(Index, OutId, OutCenter, OutRadius);
		}
		Index -= Count;
	}
	return false;
}

bool FGFSafeZoneUnion::HasAnySafe() const
{
	if (Depth > 0)
	{
		return false;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	return Parts().Num() > 0;
}

bool FGFSafeZoneUnion::IsInsideSafe(const FVector& Location) const
{
	if (Depth > 0)
	{
		return false;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	for (const IGFSafeZoneQuery* Q : Parts())
	{
		if (Q->IsInsideSafe(Location))
		{
			return true;
		}
	}
	return false;
}

bool FGFSafeZoneUnion::IsHazardAt(const FVector& Location) const
{
	if (Depth > 0)
	{
		return false;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	if (Own && Own->IsHazardAt(Location))
	{
		return true;
	}
	const UGFSafeZoneSubsystem* Sub = Zones.Get();
	return Sub && Sub->IsHazardAt(Location, Skip.Get());
}

FName FGFSafeZoneUnion::OwnerAt(const FVector& Location) const
{
	if (Depth > 0)
	{
		return NAME_None;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	for (const IGFSafeZoneQuery* Q : Parts())
	{
		const FName Owner = Q->OwnerAt(Location);
		if (!Owner.IsNone())
		{
			return Owner;
		}
	}
	return NAME_None;
}

bool FGFSafeZoneUnion::IsSafeFor(const FGFSafeSubject& Who, const FVector& Location) const
{
	if (Depth > 0)
	{
		return false;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	const TArray<const IGFSafeZoneQuery*>& All = Parts();
	// Owned land: only its owner decides (protection, guests, visitors).
	for (const IGFSafeZoneQuery* Q : All)
	{
		if (!Q->OwnerAt(Location).IsNone())
		{
			return Q->IsSafeFor(Who, Location);
		}
	}
	for (const IGFSafeZoneQuery* Q : All)
	{
		if (Q->IsSafeFor(Who, Location))
		{
			return true;
		}
	}
	return false;
}

FVector FGFSafeZoneUnion::ClosestSafePoint(const FVector& Location) const
{
	if (Depth > 0)
	{
		return Location;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	const TArray<const IGFSafeZoneQuery*>& All = Parts();
	FVector Best = Location;
	float BestD = TNumericLimits<float>::Max();
	for (const IGFSafeZoneQuery* Q : All)
	{
		if (Q->IsInsideSafe(Location))
		{
			return Location;
		}
		const FVector P = Q->ClosestSafePoint(Location);
		const float D = FVector::DistSquared2D(P, Location);
		// A part that answers "here" while Location is outside it has no real nearest point: skip it.
		if (D > 1e-4f && D < BestD)
		{
			BestD = D;
			Best = P;
		}
	}
	return Best;
}

namespace GFSafeZonePrivate
{
	/**
	 * Where the borders of A and B cross, near Start (a point on A's border inside B): project back and forth between
	 * the two borders until it settles. Outward is the mean of both outward directions (out of the notch). False if
	 * the borders don't meet near Start.
	 */
	static bool CrossBorders(const IGFSafeZoneQuery& A, const IGFSafeZoneQuery& B, const FVector& Start, FVector& OutPoint, FVector& OutOutward)
	{
		FVector X = Start, Y = Start, NA = FVector::ZeroVector, NB = FVector::ZeroVector;
		for (int32 It = 0; It < 64; ++It)
		{
			FVector Yb, Nb, Xa, Na;
			if (!B.ClosestEdge(X, Yb, Nb) || !A.ClosestEdge(Yb, Xa, Na))
			{
				return false;
			}
			const float Moved = FVector::Dist2D(Xa, X);
			X = Xa;
			Y = Yb;
			NA = Na;
			NB = Nb;
			if (Moved < 0.1f)
			{
				break;
			}
		}
		if (FVector::Dist2D(X, Y) > 1.f)
		{
			return false;
		}
		FVector N = NA.GetSafeNormal2D() + NB.GetSafeNormal2D();
		N.Z = 0.f;
		N = N.GetSafeNormal();
		if (N.IsNearlyZero())
		{
			return false;
		}
		OutPoint = FVector((X.X + Y.X) * 0.5f, (X.Y + Y.Y) * 0.5f, Start.Z);
		OutOutward = N;
		return true;
	}
}

bool FGFSafeZoneUnion::ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const
{
	if (Depth > 0)
	{
		return false;
	}
	TGuardValue<int32> Guard(Depth, Depth + 1);
	const TArray<const IGFSafeZoneQuery*>& All = Parts();
	// The part (other than SkipA and SkipB) that has P inside, or INDEX_NONE.
	auto CoveredBy = [&All](const FVector& P, int32 SkipA, int32 SkipB)
	{
		for (int32 j = 0; j < All.Num(); ++j)
		{
			if (j != SkipA && j != SkipB && All[j]->IsInsideSafe(P))
			{
				return j;
			}
		}
		return int32(INDEX_NONE);
	};
	bool bAny = false, bFree = false;
	float BestD = TNumericLimits<float>::Max();
	auto Offer = [&](const FVector& P, const FVector& N, bool bCovered)
	{
		const float D = FVector::DistSquared2D(P, Location);
		// Uncovered borders win; a covered one is used only when every border is covered.
		if ((!bCovered && (!bFree || D < BestD)) || (bCovered && !bFree && D < BestD))
		{
			bAny = true;
			bFree = bFree || !bCovered;
			BestD = D;
			OutPoint = P;
			OutOutward = N;
		}
	};
	for (int32 i = 0; i < All.Num(); ++i)
	{
		FVector P, N;
		if (!All[i]->ClosestEdge(Location, P, N))
		{
			continue;
		}
		// A border point that lies inside another part (just outside this one) is not a border of the union...
		const int32 j = CoveredBy(P + N.GetSafeNormal2D() * 2.f, i, INDEX_NONE);
		Offer(P, N, j != INDEX_NONE);
		if (j == INDEX_NONE)
		{
			continue;
		}
		// ...but where this border runs into part j, the two borders cross: that notch is a border of the union.
		FVector Q, NQ;
		if (GFSafeZonePrivate::CrossBorders(*All[i], *All[j], P, Q, NQ) && CoveredBy(Q + NQ * 2.f, INDEX_NONE, INDEX_NONE) == INDEX_NONE)
		{
			Offer(Q, NQ, false);
		}
	}
	return bAny;
}
