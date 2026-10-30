#pragma once

#include "CoreMinimal.h"

/**
 * What the game implements on its voxel mesh later.
 * This plugin does not reference game modules. SetFinishId is server-only.
 */
class VOXELFINISH_API IVoxelFinishMeshSource
{
public:
	virtual ~IVoxelFinishMeshSource() = default;

	virtual uint16 GetFinishId(FIntVector Cell) const = 0;
	virtual void SetFinishId(FIntVector Cell, uint16 FinishId) = 0;
	virtual void MarkChunkDirty(FIntVector ChunkCoord) = 0;
	virtual void ForEachPaintedBlock(TFunctionRef<void(FIntVector Cell, uint16 FinishId)> Fn) const = 0;
};
