#pragma once

// Tamed Big Grumble mining. Pure: a movement sample (feet position, velocity, pitch) becomes the voxel cells to
// remove and the yields those cells give. The capsule is upright and about 38 m tall (tunable). A downward pitch
// carves a walkable stairwell (tunable step height and tread); a steeper request, including straight down, is
// clamped to MaxPitchDegrees so the carve stays a stair and never a shaft.
//
// This does not read or write the voxel world. Positions are centimetres in the same local space as
// AGFVoxelWorld cells (Floor(pos / GFBlockSize) when that actor sits at the origin). A caller that already
// knows block ids passes them in for yields (each cell yields its own block's drop); air cells stay in the carve
// list and add no yield. With no block map nothing is yielded unless FallbackDrop is set.
//
// Tame state, species, owner, host flag and faction come from AGFDino / UGFTamingComponent / AGFPlayerState.
// The giant body (AGFGiantDino) lives in GFGiant, which this module does not link. Fill DisplayName from that
// actor ("Big Grumble") or set Species to BigGrumble and call Plan. See HANDOFF_feature_grumble-dig.md.

#include "CoreMinimal.h"
#include "GFDinoTypes.h"
#include "GFItemTypes.h"
#include "GFVoxelTypes.h"

class AGFDino;
class AGFPlayerState;

/** Why Plan removed nothing. NAME_None when it dug. */
namespace GFGrumbleDigSkip
{
	static const FName Untamed(TEXT("Untamed"));
	static const FName NotGrumble(TEXT("NotGrumble"));
	static const FName NoMotion(TEXT("NoMotion"));
	static const FName BadTuning(TEXT("BadTuning"));
}

/** One movement sample. Position is the capsule bottom (feet), cm. */
struct GFGAME_API FGFGrumbleDigMove
{
	FVector Position = FVector::ZeroVector;
	/** cm/s. Speed is the length. XY, when present, is the heading; pitch replaces the vertical component. */
	FVector Velocity = FVector::ZeroVector;
	/** Requested dig pitch, degrees. Unreal convention: negative looks down. */
	float PitchDegrees = 0.f;
	/** Heading used when Velocity has no XY (a vertical request). 0 faces +X. */
	float FacingYawDegrees = 0.f;
	float DeltaSeconds = 0.f;
};

/** Distances in cm. CellSizeCm follows GFBlockSize unless a test overrides it. */
struct GFGAME_API FGFGrumbleDigTuning
{
	/** Upright capsule, bottom to top. About 38 m. */
	float CapsuleHeightCm = 3800.f;
	/** Horizontal radius of that capsule. Clamped to half the height so the shape stays a capsule. */
	float CapsuleRadiusCm = 350.f;
	/** Steepest dig, degrees. A vertical request becomes this. */
	float MaxPitchDegrees = 45.f;
	/**
	 * Tallest stair riser, cm. The carve drops by whole cells that fit inside this, so one block (100 cm) is the
	 * smallest riser a voxel stair can have; below one cell the carve stays flat. Players (AGFCharacter) keep the
	 * engine MaxStepHeight (45 cm) and have no auto-step or vault, but their jump clears one block, so the default
	 * is one block: a hop per step. Never raise it past one block for player stairwells.
	 */
	float MaxStepHeightCm = GFBlockSize;
	/** Preferred tread along the dig, cm. Shortened when that is what keeps the riser inside MaxStepHeightCm. */
	float StepWidthCm = GFBlockSize * 2.f;
	float CellSizeCm = GFBlockSize;
	/** Travel (speed * dt) shorter than this does not dig. */
	float MinTravelCm = 1.f;
	/**
	 * Yield per carved cell when the caller does not pass a block map. None by default: without the real block ids
	 * a cell may be air, and yielding for it would mint materials. Set it (e.g. Stone) only where every cell is known solid.
	 */
	FName FallbackDrop = NAME_None;
};

/** Who is digging. Build with FromDino, or fill the fields in a test. */
struct GFGAME_API FGFGrumbleDigContext
{
	EGFTameState TameState = EGFTameState::Wild;
	FName Species = NAME_None;
	/** AGFGiantDino::DisplayName. "Big Grumble" counts even when Species is unset. */
	FString DisplayName;
	bool bOwnerIsHost = false;
	FName OwnerFaction = NAME_None;
	FString OwnerKey;

	/** Reads UGFTamingComponent on the dino and the owner's AGFPlayerState (host, faction, key). */
	static FGFGrumbleDigContext FromDino(const AGFDino* Dino, const AGFPlayerState* Owner);
};

struct GFGAME_API FGFGrumbleDigResult
{
	TArray<FIntVector> Cells;
	TArray<FGFItemStack> Yields;
	/** Pitch after the max-angle clamp. The stair uses this, not the raw request. */
	float AppliedPitchDegrees = 0.f;
	/** Tread and riser actually carved, cm. Riser is 0 on a flat run. */
	float StepTreadCm = 0.f;
	float StepRiserCm = 0.f;
	bool bDug = false;
	FName SkipReason = NAME_None;
};

struct GFGAME_API FGFGrumbleDig
{
	static const FName SpeciesId;

	static bool IsBigGrumble(FName Species, const FString& DisplayName);
	static float ClampPitch(float PitchDegrees, const FGFGrumbleDigTuning& Tuning);

	/** Feet, velocity and pitch off an AGFDino. The dig capsule is the tuning, not the pawn's capsule. */
	static FGFGrumbleDigMove MoveFromDino(const AGFDino* Dino, float DeltaSeconds);

	/**
	 * Cells the upright capsule sweeps, plus yields.
	 * Blocks: optional cell -> block id. Null means every carved cell yields FallbackDrop.
	 * Drops: optional block id -> item id. A missing drop yields the block id (DT_Blocks DropItem None).
	 * Neither map is read from the world.
	 */
	static FGFGrumbleDigResult Plan(
		const FGFGrumbleDigMove& Move,
		const FGFGrumbleDigContext& Context,
		const FGFGrumbleDigTuning& Tuning = FGFGrumbleDigTuning(),
		const TMap<FIntVector, FName>* Blocks = nullptr,
		const TMap<FName, FName>* Drops = nullptr);
};
