#include "GFMagicShapes.h"
#include "GFMagicTables.h"
#include "GFVoxelTypes.h"

namespace
{
	constexpr float RayFar = 1.0e8f;

	FIntVector CellOf(const FVector& World, const FVector& VoxelOrigin)
	{
		const FVector Local = (World - VoxelOrigin) / GFBlockSize;
		return FIntVector(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y), FMath::FloorToInt(Local.Z));
	}

	FVector CellCenter(const FIntVector& Cell, const FVector& VoxelOrigin)
	{
		return VoxelOrigin + FVector(Cell.X + 0.5f, Cell.Y + 0.5f, Cell.Z + 0.5f) * GFBlockSize;
	}

	void Basis(const FVector& Dir, FVector& OutRight, FVector& OutUp)
	{
		const FVector Helper = FMath::Abs(Dir.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector;
		OutRight = FVector::CrossProduct(Dir, Helper).GetSafeNormal();
		OutUp = FVector::CrossProduct(OutRight, Dir).GetSafeNormal();
	}

	void Orthogonal(const FIntVector& Cell, FIntVector (&Out)[6])
	{
		Out[0] = Cell + FIntVector(1, 0, 0);
		Out[1] = Cell + FIntVector(-1, 0, 0);
		Out[2] = Cell + FIntVector(0, 1, 0);
		Out[3] = Cell + FIntVector(0, -1, 0);
		Out[4] = Cell + FIntVector(0, 0, 1);
		Out[5] = Cell + FIntVector(0, 0, -1);
	}

	bool SameOwners(TArray<FString>& Into, const TArray<FString>& From)
	{
		bool bAdded = false;
		for (const FString& Key : From)
		{
			if (!Key.IsEmpty() && !Into.Contains(Key))
			{
				Into.Add(Key);
				bAdded = true;
			}
		}
		return bAdded;
	}

	struct FMarch
	{
		bool bHit = false;
		FIntVector Cell = FIntVector::ZeroValue;
		float Distance = 0.f;
		int32 EntryAxis = 2;
		int32 EntryStep = -1;
		int32 SolidsBefore = 0;
		bool bBlocked = false;
	};

	FMarch MarchFirst(
		const FVector& Start,
		const FVector& DirUnit,
		float MaxDist,
		const FVector& VoxelOrigin,
		TFunctionRef<bool(const FIntVector&)> IsSolid,
		TFunctionRef<bool(const FIntVector&)> IsBlocker,
		const FIntVector* StopBefore)
	{
		FMarch Out;
		const FVector Dir = DirUnit.GetSafeNormal();
		if (Dir.IsNearlyZero() || MaxDist <= 0.f)
		{
			const FIntVector Here = CellOf(Start, VoxelOrigin);
			Out.Cell = Here;
			Out.bHit = IsSolid(Here);
			Out.bBlocked = IsBlocker(Here);
			return Out;
		}

		const FVector Local = (Start - VoxelOrigin) / GFBlockSize;
		int32 X = FMath::FloorToInt(Local.X);
		int32 Y = FMath::FloorToInt(Local.Y);
		int32 Z = FMath::FloorToInt(Local.Z);
		const int32 StepX = Dir.X > 0.f ? 1 : (Dir.X < 0.f ? -1 : 0);
		const int32 StepY = Dir.Y > 0.f ? 1 : (Dir.Y < 0.f ? -1 : 0);
		const int32 StepZ = Dir.Z > 0.f ? 1 : (Dir.Z < 0.f ? -1 : 0);
		const float DeltaX = StepX != 0 ? GFBlockSize / FMath::Abs(Dir.X) : RayFar;
		const float DeltaY = StepY != 0 ? GFBlockSize / FMath::Abs(Dir.Y) : RayFar;
		const float DeltaZ = StepZ != 0 ? GFBlockSize / FMath::Abs(Dir.Z) : RayFar;
		auto Bound = [&](int32 Index, int32 Step, float StartCoord, float DirComp, int32 Axis)
		{
			if (Step == 0 || FMath::IsNearlyZero(DirComp))
			{
				return RayFar;
			}
			const float Plane = VoxelOrigin[Axis] + (Step > 0 ? (Index + 1) : Index) * GFBlockSize;
			return (Plane - StartCoord) / DirComp;
		};
		float MaxX = Bound(X, StepX, Start.X, Dir.X, 0);
		float MaxY = Bound(Y, StepY, Start.Y, Dir.Y, 1);
		float MaxZ = Bound(Z, StepZ, Start.Z, Dir.Z, 2);
		float Travel = 0.f;
		for (int32 Guard = 0; Guard < 256 && Travel <= MaxDist; ++Guard)
		{
			const FIntVector Here(X, Y, Z);
			if (StopBefore && Here == *StopBefore)
			{
				Out.Cell = Here;
				Out.Distance = Travel;
				Out.bHit = true;
				return Out;
			}
			if (IsBlocker(Here))
			{
				Out.bBlocked = true;
				Out.Cell = Here;
				Out.Distance = Travel;
				return Out;
			}
			if (IsSolid(Here))
			{
				Out.bHit = true;
				Out.Cell = Here;
				Out.Distance = Travel;
				return Out;
			}
			int32 Axis = 0;
			if (MaxY < MaxX && MaxY <= MaxZ)
			{
				Axis = 1;
			}
			else if (MaxZ < MaxX && MaxZ <= MaxY)
			{
				Axis = 2;
			}
			if (Axis == 0)
			{
				Travel = MaxX;
				X += StepX;
				MaxX += DeltaX;
				Out.EntryAxis = 0;
				Out.EntryStep = StepX;
			}
			else if (Axis == 1)
			{
				Travel = MaxY;
				Y += StepY;
				MaxY += DeltaY;
				Out.EntryAxis = 1;
				Out.EntryStep = StepY;
			}
			else
			{
				Travel = MaxZ;
				Z += StepZ;
				MaxZ += DeltaZ;
				Out.EntryAxis = 2;
				Out.EntryStep = StepZ;
			}
			++Out.SolidsBefore;
		}
		(void)Out.SolidsBefore;
		return Out;
	}

	int32 SolidsBeforeCell(
		const FVector& Start,
		const FIntVector& Target,
		const FVector& VoxelOrigin,
		TFunctionRef<bool(const FIntVector&)> IsSolid,
		TFunctionRef<bool(const FIntVector&)> IsBlocker,
		bool& bBlocked)
	{
		bBlocked = false;
		const FVector Center = CellCenter(Target, VoxelOrigin);
		const FVector Delta = Center - Start;
		const float Dist = Delta.Size();
		if (Dist <= KINDA_SMALL_NUMBER)
		{
			return 0;
		}
		const FVector Dir = Delta / Dist;
		const FVector Local = (Start - VoxelOrigin) / GFBlockSize;
		int32 X = FMath::FloorToInt(Local.X);
		int32 Y = FMath::FloorToInt(Local.Y);
		int32 Z = FMath::FloorToInt(Local.Z);
		const int32 StepX = Dir.X > 0.f ? 1 : (Dir.X < 0.f ? -1 : 0);
		const int32 StepY = Dir.Y > 0.f ? 1 : (Dir.Y < 0.f ? -1 : 0);
		const int32 StepZ = Dir.Z > 0.f ? 1 : (Dir.Z < 0.f ? -1 : 0);
		const float DeltaX = StepX != 0 ? GFBlockSize / FMath::Abs(Dir.X) : RayFar;
		const float DeltaY = StepY != 0 ? GFBlockSize / FMath::Abs(Dir.Y) : RayFar;
		const float DeltaZ = StepZ != 0 ? GFBlockSize / FMath::Abs(Dir.Z) : RayFar;
		auto Bound = [&](int32 Index, int32 Step, float StartCoord, float DirComp, int32 Axis)
		{
			if (Step == 0)
			{
				return RayFar;
			}
			const float Plane = VoxelOrigin[Axis] + (Step > 0 ? (Index + 1) : Index) * GFBlockSize;
			return (Plane - StartCoord) / DirComp;
		};
		float MaxX = Bound(X, StepX, Start.X, Dir.X, 0);
		float MaxY = Bound(Y, StepY, Start.Y, Dir.Y, 1);
		float MaxZ = Bound(Z, StepZ, Start.Z, Dir.Z, 2);
		int32 Solids = 0;
		float Travel = 0.f;
		for (int32 Guard = 0; Guard < 256 && Travel <= Dist + GFBlockSize; ++Guard)
		{
			const FIntVector Here(X, Y, Z);
			if (Here == Target)
			{
				return Solids;
			}
			if (IsBlocker(Here))
			{
				bBlocked = true;
				return Solids;
			}
			if (IsSolid(Here))
			{
				++Solids;
			}
			if (MaxX <= MaxY && MaxX <= MaxZ)
			{
				Travel = MaxX;
				X += StepX;
				MaxX += DeltaX;
			}
			else if (MaxY <= MaxZ)
			{
				Travel = MaxY;
				Y += StepY;
				MaxY += DeltaY;
			}
			else
			{
				Travel = MaxZ;
				Z += StepZ;
				MaxZ += DeltaZ;
			}
		}
		bBlocked = true;
		return Solids;
	}

	void StepRay(FIntVector& Cell, const FVector& Dir)
	{
		int32 Axis = 0;
		float Best = -1.f;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Abs = FMath::Abs(Dir[Index]);
			if (Abs > Best)
			{
				Best = Abs;
				Axis = Index;
			}
		}
		Cell[Axis] += Dir[Axis] >= 0.f ? 1 : -1;
	}

	bool SolidId(const FMagicWorldView& View, const FIntVector& Cell, FName& OutId)
	{
		OutId = View.BlockAt ? View.BlockAt(Cell) : NAME_None;
		return !OutId.IsNone();
	}

	bool BlockedCell(const FMagicWorldView& View, const FIntVector& Cell, FName BlockId)
	{
		if (View.IsPlaced && View.IsPlaced(Cell))
		{
			return true;
		}
		const FGFBlockAffinityRow Affinity = View.Affinity ? View.Affinity(BlockId) : FGFMagicCatalog::AffinityOrDefault(BlockId);
		if (Affinity.bSpellProof)
		{
			return true;
		}
		return View.IsBlockedLand && View.IsBlockedLand(Cell);
	}

	FMagicPuddle* FindPuddle(TArray<FMagicPuddle>& Puddles, const FIntVector& Cell)
	{
		for (FMagicPuddle& Puddle : Puddles)
		{
			if (Puddle.bAlive && Puddle.Cell == Cell)
			{
				return &Puddle;
			}
		}
		return nullptr;
	}

	int32 PuddlesNear(const TArray<FMagicPuddle>& Puddles, const FIntVector& Cell, int32 Radius)
	{
		int32 Count = 0;
		for (const FMagicPuddle& Puddle : Puddles)
		{
			if (!Puddle.bAlive)
			{
				continue;
			}
			if (FMath::Abs(Puddle.Cell.X - Cell.X) <= Radius && FMath::Abs(Puddle.Cell.Y - Cell.Y) <= Radius && FMath::Abs(Puddle.Cell.Z - Cell.Z) <= Radius)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 PuddlesOf(const TArray<FMagicPuddle>& Puddles, const FString& Owner)
	{
		int32 Count = 0;
		for (const FMagicPuddle& Puddle : Puddles)
		{
			if (Puddle.bAlive && Puddle.Owners.Contains(Owner))
			{
				++Count;
			}
		}
		return Count;
	}
}

float FMagicRhythm::Note(double Now, bool bHit)
{
	if (!bHit)
	{
		Streak = 0;
		WindowUntil = -1.0;
		return 1.f;
	}
	const bool bIn = WindowUntil >= 0.0 && Now <= WindowUntil + KINDA_SMALL_NUMBER;
	const int32 Cap = Step > 0.f ? FMath::Max(1, FMath::RoundToInt(MaxBonus / Step)) : 1;
	if (bIn)
	{
		Streak = FMath::Min(Streak + 1, Cap);
	}
	else
	{
		Streak = 0;
	}
	WindowUntil = Now + Window;
	return 1.f + Step * static_cast<float>(Streak);
}

int32 FGFMagicShapes::ConeRayCount(float RangeCm, float HalfAngleDeg, float CellSizeCm, int32 MinRays, int32 MaxRays)
{
	const float Cell = FMath::Max(1.f, CellSizeCm);
	const float Radius = FMath::Max(0.f, RangeCm) * FMath::Tan(FMath::DegreesToRadians(FMath::Max(0.f, HalfAngleDeg)));
	const float Spots = Radius / Cell;
	const int32 Raw = FMath::CeilToInt(PI * Spots * Spots * 2.f);
	return FMath::Clamp(Raw, FMath::Max(1, MinRays), FMath::Max(MinRays, MaxRays));
}

float FGFMagicShapes::SparkFalloff(float DistanceCm, float FalloffStartCm, float RangeCm, float FalloffMin)
{
	const float Start = FMath::Max(0.f, FalloffStartCm);
	const float End = FMath::Max(Start, RangeCm);
	if (DistanceCm <= Start)
	{
		return 1.f;
	}
	if (End <= Start)
	{
		return FalloffMin;
	}
	const float T = FMath::Clamp((DistanceCm - Start) / (End - Start), 0.f, 1.f);
	return FMath::Lerp(1.f, FalloffMin, T);
}

TArray<FMagicConeHit> FGFMagicShapes::GatherCone(
	const FVector& Origin,
	const FVector& DirIn,
	float RangeCm,
	float HalfAngleDeg,
	int32 Penetration,
	int32 CellCap,
	const FVector& VoxelOrigin,
	TFunctionRef<bool(const FIntVector&)> IsSolid,
	TFunctionRef<bool(const FIntVector&)> IsBlocker)
{
	TArray<FMagicConeHit> Out;
	auto Add = [&Out](const FIntVector& Cell, float Distance)
	{
		for (FMagicConeHit& Hit : Out)
		{
			if (Hit.Cell == Cell)
			{
				Hit.DistanceCm = FMath::Min(Hit.DistanceCm, Distance);
				return;
			}
		}
		FMagicConeHit Hit;
		Hit.Cell = Cell;
		Hit.DistanceCm = Distance;
		Out.Add(Hit);
	};

	const FVector Dir = DirIn.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		const FIntVector Here = CellOf(Origin, VoxelOrigin);
		if (IsSolid(Here) && !IsBlocker(Here))
		{
			Add(Here, 0.f);
		}
		return Out;
	}

	const float Range = FMath::Max(0.f, RangeCm);
	const float HalfRad = FMath::DegreesToRadians(FMath::Clamp(HalfAngleDeg, 0.f, 89.f));
	const float CosLimit = FMath::Cos(HalfRad);
	const float Radial = Range * FMath::Tan(HalfRad);
	FVector Right;
	FVector Up;
	Basis(Dir, Right, Up);
	FIntVector Lo = CellOf(Origin, VoxelOrigin);
	FIntVector Hi = Lo;
	TArray<FVector> Corners;
	Corners.Add(Origin);
	Corners.Add(Origin + Dir * Range + Right * Radial + Up * Radial);
	Corners.Add(Origin + Dir * Range + Right * Radial - Up * Radial);
	Corners.Add(Origin + Dir * Range - Right * Radial + Up * Radial);
	Corners.Add(Origin + Dir * Range - Right * Radial - Up * Radial);
	for (const FVector& Corner : Corners)
	{
		const FIntVector Cell = CellOf(Corner, VoxelOrigin);
		Lo.X = FMath::Min(Lo.X, Cell.X);
		Lo.Y = FMath::Min(Lo.Y, Cell.Y);
		Lo.Z = FMath::Min(Lo.Z, Cell.Z);
		Hi.X = FMath::Max(Hi.X, Cell.X);
		Hi.Y = FMath::Max(Hi.Y, Cell.Y);
		Hi.Z = FMath::Max(Hi.Z, Cell.Z);
	}
	Lo -= FIntVector(1, 1, 1);
	Hi += FIntVector(1, 1, 1);

	const int32 Pen = FMath::Max(0, Penetration);
	for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
	{
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				const FIntVector Cell(X, Y, Z);
				if (!IsSolid(Cell) || IsBlocker(Cell))
				{
					continue;
				}
				const FVector Center = CellCenter(Cell, VoxelOrigin);
				const FVector To = Center - Origin;
				const float Dist = To.Size();
				if (Dist > Range + GFBlockSize)
				{
					continue;
				}
				const FVector Toward = Dist > KINDA_SMALL_NUMBER ? To / Dist : Dir;
				if (FVector::DotProduct(Toward, Dir) < CosLimit)
				{
					continue;
				}
				bool bBlocked = false;
				const int32 Before = SolidsBeforeCell(Origin, Cell, VoxelOrigin, IsSolid, IsBlocker, bBlocked);
				if (bBlocked || Before != 0)
				{
					continue;
				}
				Add(Cell, FMath::Max(0.f, Dist - GFBlockSize * 0.5f));
				FIntVector Behind = Cell;
				FVector Walk = Toward;
				for (int32 Extra = 0; Extra < Pen; ++Extra)
				{
					StepRay(Behind, Walk);
					if (IsBlocker(Behind) || !IsSolid(Behind))
					{
						break;
					}
					Add(Behind, FMath::Max(0.f, Dist - GFBlockSize * 0.5f) + GFBlockSize * static_cast<float>(Extra + 1));
				}
			}
		}
	}

	Out.Sort([](const FMagicConeHit& A, const FMagicConeHit& B) { return A.DistanceCm < B.DistanceCm; });
	const int32 Cap = CellCap > 0 ? CellCap : Out.Num();
	if (Out.Num() > Cap)
	{
		Out.SetNum(Cap);
	}
	return Out;
}

FMagicGlobHit FGFMagicShapes::IntegrateGlob(
	const FVector& Pos,
	const FVector& VelIn,
	float Dt,
	float GravityZ,
	const FVector& VoxelOrigin,
	TFunctionRef<FName(const FIntVector&)> BlockAt)
{
	FMagicGlobHit Out;
	Out.Vel = VelIn;
	Out.Vel.Z += GravityZ * Dt;
	Out.Pos = Pos + Out.Vel * Dt;
	const FVector Delta = Out.Pos - Pos;
	const float Len = Delta.Size();
	if (Len <= KINDA_SMALL_NUMBER)
	{
		return Out;
	}
	auto Solid = [&BlockAt](const FIntVector& Cell) { return !BlockAt(Cell).IsNone(); };
	auto Blocker = [](const FIntVector&) { return false; };
	const FMarch Hit = MarchFirst(Pos, Delta / Len, Len, VoxelOrigin, Solid, Blocker, nullptr);
	if (!Hit.bHit)
	{
		return Out;
	}
	Out.bHit = true;
	Out.Cell = Hit.Cell;
	Out.EntryAxis = Hit.EntryAxis;
	Out.EntryStep = Hit.EntryStep == 0 ? (Out.Vel.Z < 0.f ? -1 : 1) : Hit.EntryStep;
	Out.bTop = Out.EntryAxis == 2 && Out.Vel.Z < 0.f;
	Out.Pos = Pos + (Delta / Len) * FMath::Max(0.f, Hit.Distance);
	Out.Vel = FVector::ZeroVector;
	return Out;
}

void FGFMagicShapes::FeedPuddle(
	TArray<FMagicPuddle>& Puddles,
	const FIntVector& Cell,
	float Volume,
	const TArray<FString>& Owners,
	const FIntVector& SinkDir,
	float Rate,
	double Now,
	const FGFMiningSpellRow& Spell,
	const FMagicWorldView& View)
{
	if (Volume <= 0.f)
	{
		return;
	}
	FName BlockId;
	if (!SolidId(View, Cell, BlockId) || BlockId == TEXT("Lava") || BlockedCell(View, Cell, BlockId))
	{
		return;
	}
	const int32 Area = Spell.AreaCells > 0 ? Spell.AreaCells : 64;
	const int32 AreaLimit = Spell.AreaCap > 0 ? Spell.AreaCap : 128;
	const int32 OwnerLimit = Spell.MaxPuddles > 0 ? Spell.MaxPuddles : 24;
	FMagicPuddle* Existing = FindPuddle(Puddles, Cell);
	if (!Existing)
	{
		if (PuddlesNear(Puddles, Cell, Area) >= AreaLimit)
		{
			return;
		}
		for (const FString& Owner : Owners)
		{
			if (PuddlesOf(Puddles, Owner) >= OwnerLimit)
			{
				return;
			}
		}
		FMagicPuddle& Made = Puddles.AddDefaulted_GetRef();
		Made.Cell = Cell;
		Made.DepthLeft = FMath::Max(1, Spell.PuddleMaxDepth);
		Made.SinkDir = SinkDir.IsZero() ? FIntVector(0, 0, -1) : SinkDir;
		Existing = &Made;
	}
	Existing->Volume += Volume;
	Existing->Rate = FMath::Max(Existing->Rate, Rate);
	Existing->LastFed = Now;
	Existing->bAlive = true;
	SameOwners(Existing->Owners, Owners);
	const float SpillAt = Spell.PuddleSpill > 0.f ? Spell.PuddleSpill : 2.f;
	const float Cap = Spell.PuddleMaxVolume > 0.f ? Spell.PuddleMaxVolume : 4.f;
	for (int32 Guard = 0; Guard < 16 && Existing && Existing->Volume > SpillAt; ++Guard)
	{
		const float RateCopy = Existing->Rate;
		const TArray<FString> OwnerCopy = Existing->Owners;
		const FIntVector From = Existing->Cell;
		float Extra = Existing->Volume - SpillAt;
		bool bMoved = false;
		const FIntVector Neighbours[4] = {
			From + FIntVector(1, 0, 0),
			From + FIntVector(-1, 0, 0),
			From + FIntVector(0, 1, 0),
			From + FIntVector(0, -1, 0)
		};
		for (const FIntVector& Next : Neighbours)
		{
			FName NextId;
			if (!SolidId(View, Next, NextId) || NextId == TEXT("Lava") || BlockedCell(View, Next, NextId))
			{
				continue;
			}
			FMagicPuddle* Neighbor = FindPuddle(Puddles, Next);
			const float Room = Neighbor ? FMath::Max(0.f, SpillAt - Neighbor->Volume) : SpillAt;
			if (Room <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const float Moved = FMath::Min(Extra, Room);
			if (!Neighbor)
			{
				FMagicPuddle Made;
				Made.Cell = Next;
				Made.DepthLeft = FMath::Max(1, Spell.PuddleMaxDepth);
				Made.SinkDir = FIntVector(0, 0, -1);
				Made.Rate = RateCopy;
				Made.Volume = Moved;
				Made.LastFed = Now;
				Made.bAlive = true;
				Made.Owners = OwnerCopy;
				Puddles.Add(Made);
			}
			else
			{
				Neighbor->Volume += Moved;
				Neighbor->LastFed = Now;
				Neighbor->bAlive = true;
				SameOwners(Neighbor->Owners, OwnerCopy);
			}
			if (FMagicPuddle* Source = FindPuddle(Puddles, From))
			{
				Source->Volume -= Moved;
				Existing = Source;
			}
			bMoved = true;
			break;
		}
		if (!Existing)
		{
			return;
		}
		if (!bMoved)
		{
			Existing->Volume = FMath::Min(Existing->Volume, Cap);
			break;
		}
	}
}

FMagicHitOutput FGFMagicShapes::TickPuddles(
	TArray<FMagicPuddle>& Puddles,
	TMap<FIntVector, FMagicWound>& Wounds,
	double Now,
	float Dt,
	const FGFMiningSpellRow& Spell,
		const FMagicWorldView& View,
		int32 Budget,
		float SmeltChance)
{
	FMagicHitOutput Out;
	const float Cool = Spell.PuddleCoolSeconds > 0.f ? Spell.PuddleCoolSeconds : 6.f;
	const float Dissolve = Spell.PuddleDissolve > 0.f ? Spell.PuddleDissolve : 80.f;
	TArray<FMagicPuddle> Kept;
	Kept.Reserve(Puddles.Num());
	for (FMagicPuddle& Puddle : Puddles)
	{
		if (!Puddle.bAlive || Puddle.Volume <= KINDA_SMALL_NUMBER || Now - Puddle.LastFed > Cool)
		{
			continue;
		}
		FName BlockId;
		if (!SolidId(View, Puddle.Cell, BlockId))
		{
			continue;
		}
		if (BlockedCell(View, Puddle.Cell, BlockId) || BlockId == TEXT("Lava"))
		{
			Kept.Add(Puddle);
			continue;
		}
		if (Budget <= 0)
		{
			Kept.Add(Puddle);
			continue;
		}
		FMagicHitRequest Request;
		Request.Cells.Add(Puddle.Cell);
		Request.Element = EGFMagicElement::Magma;
		Request.Contributors = Puddle.Owners;
		Request.bImpulse = true;
		Request.ImpulsePower = Dissolve * Dt;
		Request.Rate = Puddle.Rate > 0.f ? Puddle.Rate : 1.f;
		Request.SmeltChance = SmeltChance;
		Request.Budget = 1;
		Request.Dt = Dt;
		const FMagicHitOutput Hit = FGFMagicRules::Apply(Wounds, Request, View);
		Budget -= Hit.Spent;
		Out.Spent += Hit.Spent;
		if (Hit.Breaks.Num() == 0)
		{
			Kept.Add(Puddle);
			continue;
		}
		Out.Breaks.Append(Hit.Breaks);
		Puddle.Volume -= 1.f;
		Puddle.DepthLeft -= 1;
		if (Puddle.Volume <= KINDA_SMALL_NUMBER || Puddle.DepthLeft <= 0)
		{
			continue;
		}
		const FIntVector Next = Puddle.Cell + Puddle.SinkDir;
		FName NextId;
		if (!SolidId(View, Next, NextId) || NextId == TEXT("Lava"))
		{
			continue;
		}
		Puddle.Cell = Next;
		Kept.Add(Puddle);
	}
	Puddles = MoveTemp(Kept);
	return Out;
}

FMagicHitOutput FGFMagicShapes::TickSmears(
	TArray<FMagicSmear>& Smears,
	TMap<FIntVector, FMagicWound>& Wounds,
	double Now,
	float Dt,
		const FMagicWorldView& View,
		int32 Budget,
		float SmeltChance)
{
	FMagicHitOutput Out;
	TArray<FMagicSmear> Kept;
	for (const FMagicSmear& Smear : Smears)
	{
		if (Now > Smear.Until)
		{
			continue;
		}
		FName BlockId;
		if (!SolidId(View, Smear.Cell, BlockId) || BlockedCell(View, Smear.Cell, BlockId))
		{
			Kept.Add(Smear);
			continue;
		}
		if (Budget <= 0)
		{
			Kept.Add(Smear);
			continue;
		}
		FMagicHitRequest Request;
		Request.Cells.Add(Smear.Cell);
		Request.Element = EGFMagicElement::Magma;
		Request.Contributors = Smear.Owners;
		Request.bImpulse = true;
		Request.ImpulsePower = Smear.PerSecond * Dt;
		Request.Rate = Smear.Rate > 0.f ? Smear.Rate : 1.f;
		Request.SmeltChance = SmeltChance;
		Request.Budget = 1;
		const FMagicHitOutput Hit = FGFMagicRules::Apply(Wounds, Request, View);
		Budget -= Hit.Spent;
		Out.Spent += Hit.Spent;
		Out.Breaks.Append(Hit.Breaks);
		if (Hit.Breaks.Num() == 0)
		{
			Kept.Add(Smear);
		}
	}
	Smears = MoveTemp(Kept);
	return Out;
}

const FGFRootmaulMoveRow* FGFMagicShapes::Move(FName Form, FName MoveId)
{
	return FGFMagicCatalog::Move(Form, MoveId);
}

FName FGFMagicShapes::NextMove(FName Form, FName Current, bool bHeavy)
{
	if (Current.IsNone())
	{
		return bHeavy ? FName(TEXT("HeavyOverhead")) : FName(TEXT("Swing1"));
	}
	const FGFRootmaulMoveRow* Row = Move(Form, Current);
	if (!Row)
	{
		return bHeavy ? FName(TEXT("HeavyOverhead")) : FName(TEXT("Swing1"));
	}
	const FName Next = bHeavy ? Row->NextOnHeavy : Row->NextOnLight;
	return Next.IsNone() ? FName(TEXT("Swing1")) : Next;
}

TArray<FIntVector> FGFMagicShapes::MeleeCells(const FIntVector& FeetCell, const FVector& Dir, int32 Width, int32 Height, int32 Depth)
{
	FVector Aim = Dir.GetSafeNormal();
	if (Aim.IsNearlyZero())
	{
		Aim = FVector::ForwardVector;
	}
	int32 Axis = 0;
	float Best = -1.f;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Abs = FMath::Abs(Aim[Index]);
		if (Abs > Best)
		{
			Best = Abs;
			Axis = Index;
		}
	}
	FIntVector Start = FeetCell;
	Start[Axis] += Aim[Axis] >= 0.f ? 1 : -1;
	return FGFMagicRules::ClawTunnel(Start, Aim, FMath::Max(1, Depth), FMath::Max(1, Width), FMath::Max(1, Height));
}

bool FGFMagicShapes::AcceptMeleeTiming(double SwingStart, double ExpectedNotify, double Now, double SlackSeconds)
{
	return FMath::Abs(Now - (SwingStart + ExpectedNotify)) <= FMath::Max(0.0, SlackSeconds);
}

FMagicHitOutput FGFMagicShapes::TickFreeze(
	TMap<FIntVector, FMagicFreezeCell>& Cells,
	TMap<int32, int32>& ChainCounts,
	const TArray<FIntVector>& BeamCells,
	const TArray<FIntVector>& Detonate,
	const FIntVector& AimCell,
	const TArray<FString>& Contributors,
	double Now,
	float Dt,
	float Rate,
	int32 QuenchRadius,
	int32 FrostBonus,
	const FGFMiningSpellRow& Spell,
	const FMagicWorldView& View,
	int32& NextChainId)
{
	FMagicHitOutput Out;
	const float FreezeRate = Spell.FreezeRate > 0.f ? Spell.FreezeRate : 1.f;
	const float Contract = Spell.ContractionSeconds > 0.f ? Spell.ContractionSeconds : 1.2f;
	const int32 GenCap = Spell.ChainGenerations > 0 ? Spell.ChainGenerations : 4;
	const int32 CellCap = Spell.ChainMaxCells > 0 ? Spell.ChainMaxCells : 64;
	const float Power = FMath::Max(0.f, Rate);

	auto Quench = [&](const FIntVector& Cell)
	{
		FName BlockId;
		if (!SolidId(View, Cell, BlockId) || BlockId != TEXT("Lava") || BlockedCell(View, Cell, BlockId))
		{
			return;
		}
		for (const FMagicBreak& Break : Out.Breaks)
		{
			if (Break.Cell == Cell)
			{
				return;
			}
		}
		FMagicBreak Break;
		Break.Cell = Cell;
		Break.BlockId = BlockId;
		Break.TransformTo = TEXT("Basalt");
		Break.DropCount = 0;
		Break.Contributors = Contributors;
		Out.Breaks.Add(Break);
		Cells.Remove(Cell);
	};

	if (QuenchRadius >= 0 && BeamCells.Num() > 0)
	{
		for (int32 Z = -QuenchRadius; Z <= QuenchRadius; ++Z)
		{
			for (int32 Y = -QuenchRadius; Y <= QuenchRadius; ++Y)
			{
				for (int32 X = -QuenchRadius; X <= QuenchRadius; ++X)
				{
					if (FMath::Max3(FMath::Abs(X), FMath::Abs(Y), FMath::Abs(Z)) > QuenchRadius)
					{
						continue;
					}
					Quench(AimCell + FIntVector(X, Y, Z));
				}
			}
		}
	}

	auto Lock = [&](FMagicFreezeCell& Cell, int32 Generation, int32 ChainId, bool bFull, double When)
	{
		Cell.Progress = 1.f;
		Cell.bLocked = true;
		Cell.LockTime = When;
		Cell.ShatterAt = When + Contract;
		Cell.Generation = Generation;
		Cell.ChainId = ChainId;
		Cell.bFullSpread = bFull;
		SameOwners(Cell.Contributors, Contributors);
		int32& Count = ChainCounts.FindOrAdd(ChainId);
		++Count;
	};

	for (const FIntVector& Beam : BeamCells)
	{
		FName BlockId;
		if (!SolidId(View, Beam, BlockId) || BlockId == TEXT("Lava") || BlockedCell(View, Beam, BlockId))
		{
			continue;
		}
		const FGFBlockAffinityRow Affinity = View.Affinity ? View.Affinity(BlockId) : FGFMagicCatalog::AffinityOrDefault(BlockId);
		const float Hardness = FMath::Max(0.05f, View.Hardness ? View.Hardness(BlockId) : FGFMagicCatalog::BlockHardness(BlockId));
		FMagicFreezeCell& Slot = Cells.FindOrAdd(Beam);
		SameOwners(Slot.Contributors, Contributors);
		if (Slot.bLocked)
		{
			continue;
		}
		const float Mult = FMath::Max(0.f, Affinity.Frost);
		Slot.Progress += FreezeRate * Mult / Hardness * Dt * Power;
		if (Slot.Progress >= 1.f)
		{
			const int32 ChainId = NextChainId++;
			Lock(Slot, 0, ChainId, false, Now);
		}
	}

	for (const FIntVector& Cell : Detonate)
	{
		if (FMagicFreezeCell* Slot = Cells.Find(Cell))
		{
			if (Slot->bLocked)
			{
				Slot->ShatterAt = Now;
				Slot->bFullSpread = true;
			}
		}
	}

	bool bProgress = true;
	for (int32 Guard = 0; Guard < CellCap + 8 && bProgress; ++Guard)
	{
		bProgress = false;
		TArray<FIntVector> Due;
		for (const TPair<FIntVector, FMagicFreezeCell>& Pair : Cells)
		{
			if (Pair.Value.bLocked && Pair.Value.ShatterAt <= Now + KINDA_SMALL_NUMBER)
			{
				Due.Add(Pair.Key);
			}
		}
		for (const FIntVector& Cell : Due)
		{
			FMagicFreezeCell Slot;
			if (!Cells.RemoveAndCopyValue(Cell, Slot))
			{
				continue;
			}
			FName BlockId;
			if (!SolidId(View, Cell, BlockId) || BlockedCell(View, Cell, BlockId))
			{
				continue;
			}
			const FGFBlockAffinityRow Affinity = View.Affinity ? View.Affinity(BlockId) : FGFMagicCatalog::AffinityOrDefault(BlockId);
			FMagicBreak Break;
			Break.Cell = Cell;
			Break.BlockId = BlockId;
			Break.Contributors = Slot.Contributors;
			if (!Affinity.FrostTransform.IsNone())
			{
				Break.TransformTo = Affinity.FrostTransform;
				Break.DropCount = 0;
			}
			else
			{
				FName Drop = View.DropOf ? View.DropOf(BlockId) : FGFMagicCatalog::BlockDrop(BlockId);
				Break.DropItem = Drop;
				const int32 Bonus = (FrostBonus > 0 && Affinity.FreezeSpread >= 0.999f) ? FrostBonus : 0;
				Break.DropCount = 1 + Bonus;
			}
			Out.Breaks.Add(Break);
			bProgress = true;

			if (Slot.Generation >= GenCap)
			{
				continue;
			}
			const int32 ChainCount = ChainCounts.FindRef(Slot.ChainId);
			if (ChainCount >= CellCap)
			{
				continue;
			}
			FIntVector Neighbours[6];
			Orthogonal(Cell, Neighbours);
			for (const FIntVector& Next : Neighbours)
			{
				if (ChainCounts.FindRef(Slot.ChainId) >= CellCap)
				{
					break;
				}
				FName NextId;
				if (!SolidId(View, Next, NextId) || NextId == TEXT("Lava") || BlockedCell(View, Next, NextId))
				{
					continue;
				}
				const FGFBlockAffinityRow NextAffinity = View.Affinity ? View.Affinity(NextId) : FGFMagicCatalog::AffinityOrDefault(NextId);
				float Spread = NextAffinity.FreezeSpread;
				if (Slot.bFullSpread)
				{
					Spread = 1.f;
				}
				if (Spread <= 0.f)
				{
					continue;
				}
				FMagicFreezeCell& Neighbor = Cells.FindOrAdd(Next);
				SameOwners(Neighbor.Contributors, Slot.Contributors);
				if (Neighbor.bLocked)
				{
					continue;
				}
				Neighbor.Progress += Spread;
				if (Neighbor.Progress >= 1.f)
				{
					Lock(Neighbor, Slot.Generation + 1, Slot.ChainId, Slot.bFullSpread, Now);
					if (Neighbor.ShatterAt <= Now + KINDA_SMALL_NUMBER)
					{
						bProgress = true;
					}
				}
			}
		}
	}
	return Out;
}
