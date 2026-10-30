#pragma once

#include "CoreMinimal.h"
#include "IVoxelFinishMeshSource.h"
#include "VoxelFinish.h"

/**
 * Sparse painted cells, stored per chunk (chunk -> local index -> id).
 * Keeps per-id use counts world-wide and per chunk, so cap checks and chunk work never walk the whole world.
 * SetFinishId does nothing when this grid is not the authority.
 */
class VOXELFINISH_API FVoxelFinishGrid : public IVoxelFinishMeshSource
{
public:
	explicit FVoxelFinishGrid(bool bInAuthority = true);

	void SetAuthority(bool bInAuthority) { bAuthority = bInAuthority; }
	bool IsAuthority() const { return bAuthority; }

	virtual uint16 GetFinishId(FIntVector Cell) const override;
	virtual void SetFinishId(FIntVector Cell, uint16 FinishId) override;
	virtual void MarkChunkDirty(FIntVector ChunkCoord) override;
	virtual void ForEachPaintedBlock(TFunctionRef<void(FIntVector Cell, uint16 FinishId)> Fn) const override;

	/** Replication and save restore. Does not check authority. */
	void WriteReplicated(FIntVector Cell, uint16 FinishId);
	/** O(cells in that chunk). */
	void ClearChunk(const FIntVector& ChunkCoord);

	TArray<FIntVector> ConsumeDirtyChunks();
	const TSet<FIntVector>& GetDirtyChunks() const { return DirtyChunks; }
	void ClearDirty() { DirtyChunks.Reset(); }

	int32 NumPainted() const { return NumPaintedCells; }
	void Reset();

	void CountBlockUses(TMap<uint16, int32>& OutUses) const;
	/** O(cells in that chunk). */
	void ExportChunk(const FIntVector& ChunkCoord, TArray<TPair<uint16, uint16>>& OutLocalAndId) const;

	/** Painted blocks holding FinishId, world-wide. */
	int32 GetUseCount(uint16 FinishId) const;
	/** Painted blocks holding FinishId inside one chunk. */
	int32 GetChunkUseCount(const FIntVector& ChunkCoord, uint16 FinishId) const;
	/** Id -> block count for every id painted somewhere. */
	const TMap<uint16, int32>& GetUseCounts() const { return UseCounts; }
	/** Id -> block count inside one chunk, or null when the chunk has no paint. */
	const TMap<uint16, int32>* GetChunkUseCounts(const FIntVector& ChunkCoord) const;

private:
	struct FChunkCells
	{
		TMap<uint16, uint16> Cells;
		TMap<uint16, int32> Uses;
	};

	void WriteCell(const FIntVector& Cell, uint16 FinishId);
	static void AddUse(TMap<uint16, int32>& Uses, uint16 FinishId, int32 Delta);

	bool bAuthority = true;
	TMap<FIntVector, FChunkCells> Chunks;
	TMap<uint16, int32> UseCounts;
	int32 NumPaintedCells = 0;
	TSet<FIntVector> DirtyChunks;
};
