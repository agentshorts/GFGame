#pragma once

// Spell shapes for the v3 field. Pure: the subsystem and the tests both call this.
// Cone coverage walks a DDA ray to each lit cell. Puddles, swings and freeze timers
// live in these structs so a tick can run without actors.

#include "CoreMinimal.h"
#include "GFMagicRules.h"
#include "GFMagicTypes.h"

struct GFMAGIC_API FMagicConeHit
{
	FIntVector Cell = FIntVector::ZeroValue;
	float DistanceCm = 0.f;
};

struct GFMAGIC_API FMagicGlob
{
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	float Volume = 0.25f;
	FString Owner;
	float Rate = 1.f;
	bool bSticky = false;
};

struct GFMAGIC_API FMagicGlobHit
{
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	bool bHit = false;
	FIntVector Cell = FIntVector::ZeroValue;
	/** 0 = X, 1 = Y, 2 = Z. The axis of the face the glob entered through. */
	int32 EntryAxis = 2;
	/** Step that entered the cell. -1 entered from the positive side of that axis. */
	int32 EntryStep = -1;
	bool bTop = false;
};

struct GFMAGIC_API FMagicPuddle
{
	FIntVector Cell = FIntVector::ZeroValue;
	float Volume = 0.f;
	int32 DepthLeft = 3;
	double LastFed = 0.0;
	TArray<FString> Owners;
	FIntVector SinkDir = FIntVector(0, 0, -1);
	float Rate = 1.f;
	bool bAlive = true;
};

struct GFMAGIC_API FMagicSmear
{
	FIntVector Cell = FIntVector::ZeroValue;
	double Until = 0.0;
	float PerSecond = 40.f;
	TArray<FString> Owners;
	float Rate = 1.f;
};

struct GFMAGIC_API FMagicFreezeCell
{
	float Progress = 0.f;
	bool bLocked = false;
	double LockTime = 0.0;
	double ShatterAt = 0.0;
	int32 Generation = 0;
	int32 ChainId = 0;
	bool bFullSpread = false;
	TArray<FString> Contributors;
};

struct GFMAGIC_API FMagicMark
{
	FName Name;
	double Until = 0.0;
};

struct GFMAGIC_API FMagicSwing
{
	FName MoveId;
	int32 Width = 1;
	int32 Height = 1;
	int32 Depth = 1;
	float Power = 0.f;
	float ChargeCost = 0.f;
	float LungeCm = 0.f;
	bool bPitch = false;
	bool bRooted = false;
	float Rhythm = 1.f;
	FVector Forward = FVector::ForwardVector;
	FVector AimDir = FVector::ForwardVector;
};

struct GFMAGIC_API FMagicRhythm
{
	int32 Streak = 0;
	double WindowUntil = -1.0;
	float Window = 0.15f;
	float Step = 0.25f;
	float MaxBonus = 0.75f;

	/** Scale for this hit. A miss closes the window. The first hit opens it and stays at 1. */
	float Note(double Now, bool bHit);
};

struct GFMAGIC_API FGFMagicShapes
{
	static int32 ConeRayCount(float RangeCm, float HalfAngleDeg, float CellSizeCm, int32 MinRays, int32 MaxRays);
	static float SparkFalloff(float DistanceCm, float FalloffStartCm, float RangeCm, float FalloffMin);

	/**
	 * Lit cells. A zero direction is the single solid cell under the crystal.
	 * Otherwise every solid whose centre sits in the cone is tested with a DDA
	 * ray: the first solid and up to Penetration cells behind it, then the ray stops.
	 * Blockers (placed, spell-proof) stop the ray and are not returned.
	 */
	static TArray<FMagicConeHit> GatherCone(
		const FVector& Origin,
		const FVector& Dir,
		float RangeCm,
		float HalfAngleDeg,
		int32 Penetration,
		int32 CellCap,
		const FVector& VoxelOrigin,
		TFunctionRef<bool(const FIntVector&)> IsSolid,
		TFunctionRef<bool(const FIntVector&)> IsBlocker);

	static FMagicGlobHit IntegrateGlob(
		const FVector& Pos,
		const FVector& Vel,
		float Dt,
		float GravityZ,
		const FVector& VoxelOrigin,
		TFunctionRef<FName(const FIntVector&)> BlockAt);

	/** Adds volume, spills above PuddleSpill onto solid neighbours, and respects the caps. */
	static void FeedPuddle(
		TArray<FMagicPuddle>& Puddles,
		const FIntVector& Cell,
		float Volume,
		const TArray<FString>& Owners,
		const FIntVector& SinkDir,
		float Rate,
		double Now,
		const FGFMiningSpellRow& Spell,
		const FMagicWorldView& View);

	/**
	 * Dissolves the cell under each puddle. A break sinks the puddle along SinkDir,
	 * spends 1 volume, and never writes a Lava block. Protected cells take no damage.
	 * Unfed puddles drop out after PuddleCoolSeconds.
	 */
	static FMagicHitOutput TickPuddles(
		TArray<FMagicPuddle>& Puddles,
		TMap<FIntVector, FMagicWound>& Wounds,
		double Now,
		float Dt,
		const FGFMiningSpellRow& Spell,
		const FMagicWorldView& View,
		int32 Budget,
		float SmeltChance = 0.f);

	static FMagicHitOutput TickSmears(
		TArray<FMagicSmear>& Smears,
		TMap<FIntVector, FMagicWound>& Wounds,
		double Now,
		float Dt,
		const FMagicWorldView& View,
		int32 Budget,
		float SmeltChance = 0.f);

	static const FGFRootmaulMoveRow* Move(FName Form, FName MoveId);
	static FName NextMove(FName Form, FName Current, bool bHeavy);
	static TArray<FIntVector> MeleeCells(const FIntVector& FeetCell, const FVector& Dir, int32 Width, int32 Height, int32 Depth);
	static bool AcceptMeleeTiming(double SwingStart, double ExpectedNotify, double Now, double SlackSeconds);

	/**
	 * BeamCells empty skips the build and only pops due shatters.
	 * Detonate shatters those locked cells now and spreads at 100%.
	 * Lava inside QuenchRadius of AimCell becomes Basalt and does not freeze.
	 */
	static FMagicHitOutput TickFreeze(
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
		int32& NextChainId);
};
