#include "VoxelFinishGrid.h"

FVoxelFinishGrid::FVoxelFinishGrid(bool bInAuthority)
	: bAuthority(bInAuthority)
{
}

uint16 FVoxelFinishGrid::GetFinishId(FIntVector Cell) const
{
	if (const FChunkCells* Chunk = Chunks.Find(VoxelFinish::CellToChunk(Cell)))
	{
		if (const uint16* Found = Chunk->Cells.Find(static_cast<uint16>(VoxelFinish::LocalIndex(Cell))))
		{
			return *Found;
		}
	}
	return VoxelFinish::UnpaintedId;
}

void FVoxelFinishGrid::AddUse(TMap<uint16, int32>& Uses, uint16 FinishId, int32 Delta)
{
	int32& Count = Uses.FindOrAdd(FinishId);
	Count += Delta;
	if (Count <= 0)
	{
		Uses.Remove(FinishId);
	}
}

void FVoxelFinishGrid::WriteCell(const FIntVector& Cell, uint16 FinishId)
{
	const FIntVector ChunkCoord = VoxelFinish::CellToChunk(Cell);
	const uint16 Local = static_cast<uint16>(VoxelFinish::LocalIndex(Cell));
	FChunkCells* Chunk = Chunks.Find(ChunkCoord);
	const uint16 Previous = Chunk ? Chunk->Cells.FindRef(Local) : VoxelFinish::UnpaintedId;
	if (Previous == FinishId)
	{
		return;
	}
	if (Previous != VoxelFinish::UnpaintedId)
	{
		Chunk->Cells.Remove(Local);
		AddUse(Chunk->Uses, Previous, -1);
		AddUse(UseCounts, Previous, -1);
		--NumPaintedCells;
	}
	if (FinishId != VoxelFinish::UnpaintedId)
	{
		if (!Chunk)
		{
			Chunk = &Chunks.Add(ChunkCoord);
		}
		Chunk->Cells.Add(Local, FinishId);
		AddUse(Chunk->Uses, FinishId, 1);
		AddUse(UseCounts, FinishId, 1);
		++NumPaintedCells;
	}
	if (Chunk && Chunk->Cells.Num() == 0)
	{
		Chunks.Remove(ChunkCoord);
	}
}

void FVoxelFinishGrid::SetFinishId(FIntVector Cell, uint16 FinishId)
{
	if (!bAuthority)
	{
		return;
	}
	WriteCell(Cell, FinishId);
}

void FVoxelFinishGrid::WriteReplicated(FIntVector Cell, uint16 FinishId)
{
	WriteCell(Cell, FinishId);
}

void FVoxelFinishGrid::MarkChunkDirty(FIntVector ChunkCoord)
{
	DirtyChunks.Add(ChunkCoord);
}

void FVoxelFinishGrid::ForEachPaintedBlock(TFunctionRef<void(FIntVector Cell, uint16 FinishId)> Fn) const
{
	for (const TPair<FIntVector, FChunkCells>& Chunk : Chunks)
	{
		for (const TPair<uint16, uint16>& Cell : Chunk.Value.Cells)
		{
			Fn(VoxelFinish::ChunkLocalToCell(Chunk.Key, Cell.Key), Cell.Value);
		}
	}
}

void FVoxelFinishGrid::ClearChunk(const FIntVector& ChunkCoord)
{
	const FChunkCells* Chunk = Chunks.Find(ChunkCoord);
	if (!Chunk)
	{
		return;
	}
	for (const TPair<uint16, int32>& Use : Chunk->Uses)
	{
		AddUse(UseCounts, Use.Key, -Use.Value);
	}
	NumPaintedCells -= Chunk->Cells.Num();
	Chunks.Remove(ChunkCoord);
}

TArray<FIntVector> FVoxelFinishGrid::ConsumeDirtyChunks()
{
	TArray<FIntVector> Out = DirtyChunks.Array();
	DirtyChunks.Reset();
	return Out;
}

void FVoxelFinishGrid::Reset()
{
	Chunks.Reset();
	UseCounts.Reset();
	NumPaintedCells = 0;
	DirtyChunks.Reset();
}

void FVoxelFinishGrid::CountBlockUses(TMap<uint16, int32>& OutUses) const
{
	OutUses = UseCounts;
}

void FVoxelFinishGrid::ExportChunk(const FIntVector& ChunkCoord, TArray<TPair<uint16, uint16>>& OutLocalAndId) const
{
	OutLocalAndId.Reset();
	if (const FChunkCells* Chunk = Chunks.Find(ChunkCoord))
	{
		OutLocalAndId.Reserve(Chunk->Cells.Num());
		for (const TPair<uint16, uint16>& Cell : Chunk->Cells)
		{
			OutLocalAndId.Emplace(Cell.Key, Cell.Value);
		}
	}
}

int32 FVoxelFinishGrid::GetUseCount(uint16 FinishId) const
{
	return UseCounts.FindRef(FinishId);
}

int32 FVoxelFinishGrid::GetChunkUseCount(const FIntVector& ChunkCoord, uint16 FinishId) const
{
	const FChunkCells* Chunk = Chunks.Find(ChunkCoord);
	return Chunk ? Chunk->Uses.FindRef(FinishId) : 0;
}

const TMap<uint16, int32>* FVoxelFinishGrid::GetChunkUseCounts(const FIntVector& ChunkCoord) const
{
	const FChunkCells* Chunk = Chunks.Find(ChunkCoord);
	return Chunk ? &Chunk->Uses : nullptr;
}
