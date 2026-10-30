#include "GFGrumbleDig.h"
#include "GFDino.h"
#include "GFPlayerState.h"
#include "GFTamingComponent.h"
#include "Components/CapsuleComponent.h"

const FName FGFGrumbleDig::SpeciesId(TEXT("BigGrumble"));

namespace
{
	constexpr float GFGrumbleDigEdge = 0.05f;

	float SegmentSegmentDist2D(const FVector2D& P1, const FVector2D& Q1, const FVector2D& P2, const FVector2D& Q2)
	{
		const FVector2D D1 = Q1 - P1;
		const FVector2D D2 = Q2 - P2;
		const FVector2D R = P1 - P2;
		const float A = FVector2D::DotProduct(D1, D1);
		const float E = FVector2D::DotProduct(D2, D2);
		const float F = FVector2D::DotProduct(D2, R);
		float S = 0.f;
		float T = 0.f;
		constexpr float Eps = 1.e-8f;
		if (A <= Eps && E <= Eps)
		{
			return FVector2D::Distance(P1, P2);
		}
		if (A <= Eps)
		{
			S = 0.f;
			T = FMath::Clamp(F / E, 0.f, 1.f);
		}
		else
		{
			const float C = FVector2D::DotProduct(D1, R);
			if (E <= Eps)
			{
				T = 0.f;
				S = FMath::Clamp(-C / A, 0.f, 1.f);
			}
			else
			{
				const float B = FVector2D::DotProduct(D1, D2);
				const float Denom = A * E - B * B;
				if (!FMath::IsNearlyZero(Denom))
				{
					S = FMath::Clamp((B * F - C * E) / Denom, 0.f, 1.f);
				}
				T = (B * S + F) / E;
				if (T < 0.f)
				{
					T = 0.f;
					S = FMath::Clamp(-C / A, 0.f, 1.f);
				}
				else if (T > 1.f)
				{
					T = 1.f;
					S = FMath::Clamp((B - C) / A, 0.f, 1.f);
				}
			}
		}
		return FVector2D::Distance(P1 + D1 * S, P2 + D2 * T);
	}

	float RectSegmentDist2D(const FVector2D& RectMin, const FVector2D& RectMax, const FVector2D& A, const FVector2D& B)
	{
		auto Inside = [&](const FVector2D& P)
		{
			return P.X >= RectMin.X && P.X <= RectMax.X && P.Y >= RectMin.Y && P.Y <= RectMax.Y;
		};
		if (Inside(A) || Inside(B))
		{
			return 0.f;
		}
		const FVector2D Corners[4] = {
			RectMin,
			FVector2D(RectMax.X, RectMin.Y),
			RectMax,
			FVector2D(RectMin.X, RectMax.Y)
		};
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i < 4; ++i)
		{
			Best = FMath::Min(Best, SegmentSegmentDist2D(A, B, Corners[i], Corners[(i + 1) % 4]));
		}
		return Best;
	}

	/** Horizontal allowance of an upright capsule at a Z inside [FeetZ, FeetZ + Height]. */
	float HorizontalAllowance(float Z, float FeetZ, float Height, float Radius)
	{
		const float Core0 = FeetZ + Radius;
		const float Core1 = FeetZ + Height - Radius;
		if (Z >= Core0 && Z <= Core1)
		{
			return Radius;
		}
		const float Dz = (Z < Core0) ? (Core0 - Z) : (Z - Core1);
		if (Dz >= Radius)
		{
			return 0.f;
		}
		return FMath::Sqrt(FMath::Max(0.f, Radius * Radius - Dz * Dz));
	}

	/**
	 * Closed cell [Min, Min+Cell] hits the upright capsule swept along a horizontal segment at FeetZ.
	 * A zero-length segment is the capsule standing still. The vertical interval is [FeetZ, FeetZ+Height).
	 */
	bool CellHitsSweep(const FIntVector& Cell, float CellSize, const FVector& FeetA, const FVector& FeetB, float Height, float Radius)
	{
		const float ZMin = Cell.Z * CellSize;
		const float ZMax = ZMin + CellSize;
		const float CapMin = FeetA.Z;
		const float CapMax = FeetA.Z + Height;
		if (ZMax <= CapMin || ZMin >= CapMax)
		{
			return false;
		}
		// Largest horizontal reach anywhere in the cell's overlap with the capsule.
		float Allow = 0.f;
		const float Core0 = FeetA.Z + Radius;
		const float Core1 = FeetA.Z + Height - Radius;
		const float OverMin = FMath::Max(ZMin, CapMin);
		const float OverMax = FMath::Min(ZMax, CapMax);
		if (OverMax > Core0 && OverMin < Core1)
		{
			Allow = Radius;
		}
		else
		{
			const float Z = (OverMax <= Core0) ? OverMax : OverMin;
			Allow = HorizontalAllowance(Z, FeetA.Z, Height, Radius);
		}
		if (Allow <= 0.f)
		{
			return false;
		}
		const FVector2D RectMin(Cell.X * CellSize, Cell.Y * CellSize);
		const FVector2D RectMax = RectMin + FVector2D(CellSize, CellSize);
		const float Dist = RectSegmentDist2D(RectMin, RectMax, FVector2D(FeetA.X, FeetA.Y), FVector2D(FeetB.X, FeetB.Y));
		return Dist <= Allow + KINDA_SMALL_NUMBER;
	}

	void AddHorizontalSweep(const FVector& FeetA, const FVector& FeetB, float Height, float Radius, float CellSize, TSet<FIntVector>& Out)
	{
		if (Height <= 0.f || Radius <= 0.f || CellSize <= 0.f)
		{
			return;
		}
		const float MinX = FMath::Min(FeetA.X, FeetB.X) - Radius;
		const float MaxX = FMath::Max(FeetA.X, FeetB.X) + Radius;
		const float MinY = FMath::Min(FeetA.Y, FeetB.Y) - Radius;
		const float MaxY = FMath::Max(FeetA.Y, FeetB.Y) + Radius;
		const float MinZ = FeetA.Z;
		const float MaxZ = FeetA.Z + Height;
		const int32 X0 = FMath::FloorToInt(MinX / CellSize);
		const int32 X1 = FMath::FloorToInt(MaxX / CellSize);
		const int32 Y0 = FMath::FloorToInt(MinY / CellSize);
		const int32 Y1 = FMath::FloorToInt(MaxY / CellSize);
		const int32 Z0 = FMath::FloorToInt(MinZ / CellSize);
		const int32 Z1 = FMath::FloorToInt((MaxZ - KINDA_SMALL_NUMBER) / CellSize);
		for (int32 X = X0; X <= X1; ++X)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 Z = Z0; Z <= Z1; ++Z)
				{
					const FIntVector Cell(X, Y, Z);
					if (CellHitsSweep(Cell, CellSize, FeetA, FeetB, Height, Radius))
					{
						Out.Add(Cell);
					}
				}
			}
		}
	}

	void AddSegment(const FVector& FeetA, const FVector& FeetB, float Height, float Radius, float CellSize, TSet<FIntVector>& Out)
	{
		const float Dx = FeetB.X - FeetA.X;
		const float Dy = FeetB.Y - FeetA.Y;
		if (Dx * Dx + Dy * Dy <= 1.f)
		{
			const float Low = FMath::Min(FeetA.Z, FeetB.Z);
			const float High = FMath::Max(FeetA.Z, FeetB.Z);
			const FVector At(FeetA.X, FeetA.Y, Low);
			AddHorizontalSweep(At, At, Height + (High - Low), Radius, CellSize, Out);
			return;
		}
		const FVector FlatB(FeetB.X, FeetB.Y, FeetA.Z);
		AddHorizontalSweep(FeetA, FlatB, Height, Radius, CellSize, Out);
	}

	void ChooseStair(float AbsPitchDeg, const FGFGrumbleDigTuning& Tuning, float& OutTreadCm, float& OutRiserCm)
	{
		OutTreadCm = 0.f;
		OutRiserCm = 0.f;
		const float Cell = Tuning.CellSizeCm;
		if (AbsPitchDeg < 0.01f || Cell <= 0.f)
		{
			return;
		}
		const int32 FitCells = FMath::FloorToInt((Tuning.MaxStepHeightCm + 0.001f) / Cell);
		if (FitCells < 1)
		{
			return;
		}
		const float Slope = FMath::Tan(FMath::DegreesToRadians(AbsPitchDeg));
		if (Slope <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		int32 RiserCells = FitCells;
		const float WantedRiser = FMath::Max(Tuning.StepWidthCm, Cell) * Slope;
		if (WantedRiser <= FitCells * Cell)
		{
			RiserCells = FMath::Clamp(FMath::RoundToInt(WantedRiser / Cell), 1, FitCells);
		}
		float Tread = (RiserCells * Cell) / Slope;
		if (Tread < Cell)
		{
			Tread = Cell;
		}
		OutTreadCm = Tread;
		OutRiserCm = RiserCells * Cell;
	}

	void CarvePath(const FGFGrumbleDigMove& Move, float AppliedPitch, const FGFGrumbleDigTuning& Tuning, FGFGrumbleDigResult& Out, TSet<FIntVector>& Cells)
	{
		const float Travel = Move.Velocity.Size() * FMath::Max(0.f, Move.DeltaSeconds);
		float YawDeg = Move.FacingYawDegrees;
		if (Move.Velocity.Size2D() > 1.f)
		{
			YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Move.Velocity.Y, Move.Velocity.X));
		}
		const float Yaw = FMath::DegreesToRadians(YawDeg);
		const FVector2D Fwd(FMath::Cos(Yaw), FMath::Sin(Yaw));
		const float PitchRad = FMath::DegreesToRadians(AppliedPitch);
		const float Horiz = FMath::Max(0.f, Travel * FMath::Cos(PitchRad));
		const float Radius = FMath::Min(Tuning.CapsuleRadiusCm, Tuning.CapsuleHeightCm * 0.5f);
		const float Height = Tuning.CapsuleHeightCm;
		const float Cell = Tuning.CellSizeCm;

		float Tread = 0.f;
		float Riser = 0.f;
		ChooseStair(FMath::Abs(AppliedPitch), Tuning, Tread, Riser);
		Out.StepTreadCm = (Riser <= 0.f) ? Horiz : Tread;
		Out.StepRiserCm = Riser;

		const FVector Origin = Move.Position;
		if (Riser <= 0.f || Tread <= 0.f || Horiz <= GFGrumbleDigEdge)
		{
			const FVector End(Origin.X + Fwd.X * Horiz, Origin.Y + Fwd.Y * Horiz, Origin.Z);
			AddSegment(Origin, End, Height, Radius, Cell, Cells);
			return;
		}

		const float ZSign = AppliedPitch < 0.f ? -1.f : 1.f;
		float Along = 0.f;
		int32 Step = 0;
		while (Along < Horiz - GFGrumbleDigEdge && Step < 100000)
		{
			const float Next = FMath::Min(Horiz, (Step + 1) * Tread);
			const float Z = Origin.Z + Step * Riser * ZSign;
			const FVector Start(Origin.X + Fwd.X * Along, Origin.Y + Fwd.Y * Along, Z);
			const FVector End(Origin.X + Fwd.X * Next, Origin.Y + Fwd.Y * Next, Z);
			AddSegment(Start, End, Height, Radius, Cell, Cells);
			if (Next < Horiz - GFGrumbleDigEdge)
			{
				const FVector Dropped(End.X, End.Y, Origin.Z + (Step + 1) * Riser * ZSign);
				AddSegment(End, Dropped, Height, Radius, Cell, Cells);
			}
			Along = Next;
			++Step;
		}
	}

	void FillYields(FGFGrumbleDigResult& Out, const FGFGrumbleDigTuning& Tuning, const TMap<FIntVector, FName>* Blocks, const TMap<FName, FName>* Drops)
	{
		TMap<FName, int32> Counts;
		auto Add = [&](FName Item)
		{
			if (!Item.IsNone())
			{
				Counts.FindOrAdd(Item) += 1;
			}
		};
		if (!Blocks)
		{
			if (!Tuning.FallbackDrop.IsNone())
			{
				Counts.Add(Tuning.FallbackDrop, Out.Cells.Num());
			}
		}
		else
		{
			for (const FIntVector& Cell : Out.Cells)
			{
				const FName* Block = Blocks->Find(Cell);
				if (!Block || Block->IsNone())
				{
					continue;
				}
				FName Item = *Block;
				if (Drops)
				{
					if (const FName* Drop = Drops->Find(*Block))
					{
						Item = *Drop;
					}
				}
				Add(Item);
			}
		}
		Counts.KeySort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		for (const TPair<FName, int32>& Pair : Counts)
		{
			if (Pair.Value > 0)
			{
				Out.Yields.Add(FGFItemStack(Pair.Key, Pair.Value));
			}
		}
	}
}

bool FGFGrumbleDig::IsBigGrumble(FName Species, const FString& DisplayName)
{
	if (Species == SpeciesId)
	{
		return true;
	}
	return DisplayName.Equals(TEXT("Big Grumble"), ESearchCase::IgnoreCase);
}

float FGFGrumbleDig::ClampPitch(float PitchDegrees, const FGFGrumbleDigTuning& Tuning)
{
	const float Max = FMath::Max(0.f, Tuning.MaxPitchDegrees);
	return FMath::Clamp(PitchDegrees, -Max, Max);
}

FGFGrumbleDigContext FGFGrumbleDigContext::FromDino(const AGFDino* Dino, const AGFPlayerState* Owner)
{
	FGFGrumbleDigContext Context;
	if (Dino && Dino->Taming)
	{
		Context.Species = Dino->Taming->Species;
		Context.TameState = Dino->Taming->GetTameState();
		Context.OwnerKey = Dino->Taming->GetOwnerKey();
	}
	if (Owner)
	{
		Context.OwnerKey = Owner->PlayerKey;
		Context.bOwnerIsHost = Owner->bIsHost;
		Context.OwnerFaction = Owner->FactionId;
	}
	return Context;
}

FGFGrumbleDigMove FGFGrumbleDig::MoveFromDino(const AGFDino* Dino, float DeltaSeconds)
{
	FGFGrumbleDigMove Move;
	Move.DeltaSeconds = DeltaSeconds;
	if (!Dino)
	{
		return Move;
	}
	const UCapsuleComponent* Cap = Dino->GetCapsuleComponent();
	const float Half = Cap ? Cap->GetScaledCapsuleHalfHeight() : 0.f;
	Move.Position = Dino->GetActorLocation() - FVector(0.f, 0.f, Half);
	Move.Velocity = Dino->GetVelocity();
	const FRotator Rot = Dino->GetActorRotation();
	Move.PitchDegrees = Rot.Pitch;
	Move.FacingYawDegrees = Rot.Yaw;
	return Move;
}

FGFGrumbleDigResult FGFGrumbleDig::Plan(
	const FGFGrumbleDigMove& Move,
	const FGFGrumbleDigContext& Context,
	const FGFGrumbleDigTuning& Tuning,
	const TMap<FIntVector, FName>* Blocks,
	const TMap<FName, FName>* Drops)
{
	FGFGrumbleDigResult Out;
	if (!IsBigGrumble(Context.Species, Context.DisplayName))
	{
		Out.SkipReason = GFGrumbleDigSkip::NotGrumble;
		return Out;
	}
	if (Context.TameState != EGFTameState::Tamed)
	{
		Out.SkipReason = GFGrumbleDigSkip::Untamed;
		return Out;
	}
	Out.AppliedPitchDegrees = ClampPitch(Move.PitchDegrees, Tuning);
	if (Tuning.CapsuleHeightCm <= 0.f || Tuning.CapsuleRadiusCm <= 0.f || Tuning.CellSizeCm <= 0.f)
	{
		Out.SkipReason = GFGrumbleDigSkip::BadTuning;
		return Out;
	}
	if (Move.Velocity.Size() * FMath::Max(0.f, Move.DeltaSeconds) < Tuning.MinTravelCm)
	{
		Out.SkipReason = GFGrumbleDigSkip::NoMotion;
		return Out;
	}

	TSet<FIntVector> Unique;
	CarvePath(Move, Out.AppliedPitchDegrees, Tuning, Out, Unique);
	Out.Cells.Reserve(Unique.Num());
	for (const FIntVector& Cell : Unique)
	{
		Out.Cells.Add(Cell);
	}
	Out.Cells.Sort([](const FIntVector& A, const FIntVector& B)
	{
		if (A.X != B.X) { return A.X < B.X; }
		if (A.Y != B.Y) { return A.Y < B.Y; }
		return A.Z < B.Z;
	});
	Out.bDug = Out.Cells.Num() > 0;
	if (!Out.bDug)
	{
		Out.SkipReason = GFGrumbleDigSkip::NoMotion;
		return Out;
	}
	FillYields(Out, Tuning, Blocks, Drops);
	return Out;
}
