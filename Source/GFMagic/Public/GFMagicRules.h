#pragma once

// Pure mining math. No world, no GAS. The subsystem and the tests both call this.

#include "CoreMinimal.h"
#include "GFItemTypes.h"
#include "GFMagicTypes.h"

struct GFMAGIC_API FMagicWound
{
	float Remaining = 0.f;
	bool bStarted = false;
	TArray<FString> Contributors;
};

struct GFMAGIC_API FMagicHitRequest
{
	TArray<FIntVector> Cells;
	EGFMagicElement Element = EGFMagicElement::Spark;
	TArray<FString> Contributors;
	bool bFlat = false;
	float FlatIntegrity = 250.f;
	float Dt = GFMagic::TickSeconds;
	float IntegrityPerSecond = 100.f;
	float Rate = 1.f;
	int32 Budget = GFMagic::CellsPerTick;
	bool bSoilChain = false;
	int32 ExtraDrop = 0;
	bool bJackpot = false;
	/** One hit, not per second. Still multiplied by the block matrix. */
	bool bImpulse = false;
	float ImpulsePower = 0.f;
	/** Missing cells stay at 1. Sparkcone writes a falloff here. */
	TMap<FIntVector, float> CellScale;
	/** 0 keeps the old unlimited conductive walk. The cone passes the spell cap. */
	int32 ConductiveCap = 0;
	float SmeltChance = 0.f;
};

struct GFMAGIC_API FMagicBreak
{
	FIntVector Cell = FIntVector::ZeroValue;
	FName BlockId;
	FName DropItem;
	int32 DropCount = 0;
	/** Set when Frost quenches the cell into another block. DropCount is then 0. */
	FName TransformTo;
	TArray<FString> Contributors;
};

struct GFMAGIC_API FMagicHitOutput
{
	TArray<FMagicBreak> Breaks;
	/** Cells that still need a later tick (the 64-cell cap). */
	TArray<FIntVector> Deferred;
	/** Cells that actually took a hit this call. Shared across casters so a tick stays at 64. */
	int32 Spent = 0;
};

/** How the rules read a grid without owning one. */
struct GFMAGIC_API FMagicWorldView
{
	TFunction<FName(const FIntVector&)> BlockAt;
	TFunction<bool(const FIntVector&)> IsPlaced;
	TFunction<bool(const FIntVector&)> IsBlockedLand;
	TFunction<float(FName)> Hardness;
	TFunction<FName(FName)> DropOf;
	TFunction<FGFBlockAffinityRow(FName)> Affinity;
};

struct GFMAGIC_API FGFMagicModifiers
{
	float RangeBonusCm = 0.f;
	int32 ClawLengthBonus = 0;
	float IntegrityRate = 1.f;
	bool bSoilChain = false;
	bool bStairwell = false;
	int32 ExtraDrop = 0;
	bool bOreGlint = false;
	bool bBondCall = false;
	bool bWorkingDinos = false;
	bool bJackpot = false;
	float SmeltChance = 0.f;
	bool bSticky = false;
	FName MeleeForm = TEXT("Rootmaul");
	int32 FrostBonus = 0;
	int32 QuenchRadius = 0;
};

struct GFMAGIC_API FMagicUpgradeState
{
	FName FactionId;
	EGFPath Path = EGFPath::None;
	TArray<FName> Owned;
	TMap<FName, int32> Bag;
};

struct GFMAGIC_API FMagicUpgradeDecision
{
	FName Code = TEXT("Unknown");
	TArray<FName> Owned;
	TMap<FName, int32> Bag;
};

struct GFMAGIC_API FGFMagicRules
{
	static float IntegrityOf(float Hardness);
	static float Multiplier(const FGFBlockAffinityRow& Affinity, EGFMagicElement Element);
	static FLinearColor ElementColor(EGFMagicElement Element);
	/** Stable 0..1 roll so a smelt bonus does not depend on the frame. */
	static bool SmeltProcs(const FIntVector& Cell, float Chance);
	/** 1 at one break, rising to 1 + Bonus once Count reaches Peak. */
	static float ChainScale(int32 Count, int32 Peak, float Bonus);

	static FName PathId(EGFPath Path);
	static EGFMagicElement FactionElement(FName FactionId);
	static FName ClawForm(FName FactionId);

	/** Cells whose centre is within Radius of Center. Radius 1 is the centre plus the six face neighbours. */
	static TArray<FIntVector> FieldCells(const FIntVector& Center, int32 Radius);
	/** 6 x 3 x 2 by default: length along the aim, width centred, height growing up. */
	static TArray<FIntVector> ClawTunnel(const FIntVector& Start, const FVector& Dir, int32 Length, int32 Width, int32 Height);
	/** Pulls a point back so it sits within RangeCm of Origin. */
	static FVector ClampRange(const FVector& Origin, const FVector& Point, float RangeCm);
	/**
	 * First solid cell along Origin -> Point, or the cell at Point when the ray is empty.
	 * VoxelOrigin is the actor location (cell 0,0,0).
	 */
	static FIntVector ResolveCenter(const FVector& Origin, const FVector& Point, const FVector& VoxelOrigin, TFunctionRef<FName(const FIntVector&)> BlockAt);

	static FMagicHitOutput Apply(TMap<FIntVector, FMagicWound>& Wounds, const FMagicHitRequest& Request, const FMagicWorldView& View);

	static FGFMagicModifiers ModifiersFor(const TArray<FName>& Owned);
	static FMagicUpgradeDecision TryBuy(const FMagicUpgradeState& State, FName UpgradeId);

	/** Plan cells kept for one claw: in front of the pawn, at most the work cap. */
	static TArray<FIntVector> CapStair(const TArray<FIntVector>& Planned, const FIntVector& FeetCell, const FVector& Dir, int32 Cap = GFMagic::CellsPerTick);
};
