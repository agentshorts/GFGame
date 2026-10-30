#pragma once

#include "CoreMinimal.h"

/**
 * Paint is baked. These constants bound storage, replication, and effect graphs.
 * Effect graphs are validated and rejected; they are not evaluated per block.
 */
namespace VoxelFinish
{
	/** FinishId 0 is unpainted and is never a palette slot. */
	constexpr uint16 UnpaintedId = 0;
	/** Palette ids are 1..MaxPaletteEntries. Each id is one column of the 4096-wide lookup (column 0 is unpainted). */
	constexpr int32 MaxPaletteEntries = 4095;

	/** Distinct effect-bearing finishes (not plain paint) allowed in one chunk. */
	constexpr int32 MaxEffectFinishesPerChunk = 32;
	/** Distinct effect-bearing finishes with at least one painted block, world-wide. Matches the 256-slot bake atlas. */
	constexpr int32 MaxEffectFinishesGlobal = 256;

	/** Blocks per chunk edge. Matches the game grid without linking a game module. */
	constexpr int32 ChunkSize = 16;
	constexpr int32 ChunkVolume = ChunkSize * ChunkSize * ChunkSize;

	/** Atlas and face cells are 16x16 texels, sampled nearest. */
	constexpr int32 AtlasCellTexels = 16;
	/** Pattern atlas is a 4x4 grid of those cells (patterns 0-15). */
	constexpr int32 PatternAtlasGrid = 4;
	/** FFinish::PatternId for a solid colour (no pattern). */
	constexpr int32 SolidPatternId = 255;

	constexpr int32 SparkleFrameCount = 8;
	constexpr float SparkleFramesPerSecond = 12.f;
	/** Built-in sparkle effect index. The flipbook is global time, not a per-block graph. */
	constexpr int32 SparkleEffectId = 4;

	/** Past this distance the paint stays and the effect contribution drops to zero. 32 blocks at 100 cm. */
	constexpr float EffectDistanceCutoffCm = 3200.f;

	/** Authored effect graphs. */
	constexpr int32 MaxAuthoredNodes = 32;
	/** Inlined SubEffect result. */
	constexpr int32 MaxFlattenedNodes = 64;
	constexpr int32 MaxFlattenedOps = 128;
	/** parent -> child -> grandchild -> great-grandchild. */
	constexpr int32 MaxSubEffectDepth = 3;
	/** Inlined node count of a combined effect. */
	constexpr int32 MaxCombinedSteps = 64;

	constexpr int32 LookupTextureWidth = 4096;
	constexpr int32 LookupTextureHeight = 4;
	static_assert(MaxPaletteEntries < LookupTextureWidth, "Every palette id needs a lookup column; column 0 is unpainted");

	inline int32 FloorDiv(int32 A, int32 B)
	{
		int32 Q = A / B;
		const int32 R = A % B;
		if (R != 0 && ((A < 0) != (B < 0)))
		{
			--Q;
		}
		return Q;
	}

	inline int32 FloorMod(int32 A, int32 B)
	{
		const int32 R = A % B;
		return R < 0 ? R + B : R;
	}

	inline FIntVector CellToChunk(const FIntVector& Cell)
	{
		return FIntVector(FloorDiv(Cell.X, ChunkSize), FloorDiv(Cell.Y, ChunkSize), FloorDiv(Cell.Z, ChunkSize));
	}

	inline int32 LocalIndex(const FIntVector& Cell)
	{
		const int32 X = FloorMod(Cell.X, ChunkSize);
		const int32 Y = FloorMod(Cell.Y, ChunkSize);
		const int32 Z = FloorMod(Cell.Z, ChunkSize);
		return X + Y * ChunkSize + Z * ChunkSize * ChunkSize;
	}

	inline FIntVector ChunkLocalToCell(const FIntVector& Chunk, int32 Local)
	{
		const int32 X = Local % ChunkSize;
		const int32 Y = (Local / ChunkSize) % ChunkSize;
		const int32 Z = Local / (ChunkSize * ChunkSize);
		return FIntVector(Chunk.X * ChunkSize + X, Chunk.Y * ChunkSize + Y, Chunk.Z * ChunkSize + Z);
	}

	/** Paint stays at any distance. Effects drop to zero past the cutoff (inclusive inside). */
	inline bool IsEffectVisibleAtDistance(float DistanceCm)
	{
		return DistanceCm <= EffectDistanceCutoffCm;
	}

	inline float EffectWeightAtDistance(float DistanceCm)
	{
		return IsEffectVisibleAtDistance(DistanceCm) ? 1.f : 0.f;
	}

	/** Writes FinishId into UV channel 2. Face UVs are packed into the second component as a 16x16 texel index. */
	VOXELFINISH_API void WriteFinishToVertex(uint16 FinishId, const FVector2f& FaceUVs, FVector2f& OutUV2);

	/** Inverse of WriteFinishToVertex. Face UV is the centre of the packed texel. */
	VOXELFINISH_API void DecodeFinishVertex(const FVector2f& UV2, uint16& OutFinishId, FVector2f& OutFaceUV);
}
