#include "GFVoxelTypes.h"

FGFVoxelGrid::FGFVoxelGrid(FIntVector InSizeInBlocks)
	: Size(InSizeInBlocks)
{
	Clear();
}

bool FGFVoxelGrid::IsInBounds(const FIntVector& C) const
{
	return C.X >= 0 && C.Y >= 0 && C.Z >= 0 && C.X < Size.X && C.Y < Size.Y && C.Z < Size.Z;
}

FIntVector FGFVoxelGrid::CellToChunk(const FIntVector& C)
{
	return FIntVector(C.X / GFChunkSize, C.Y / GFChunkSize, C.Z / GFChunkSize);
}

int32 FGFVoxelGrid::LocalIndex(const FIntVector& C)
{
	const int32 X = C.X % GFChunkSize, Y = C.Y % GFChunkSize, Z = C.Z % GFChunkSize;
	return X + Y * GFChunkSize + Z * GFChunkSize * GFChunkSize;
}

uint16 FGFVoxelGrid::GetOrAddPaletteIndex(FName BlockId)
{
	if (BlockId.IsNone())
	{
		return 0;
	}
	if (const uint16* Found = PaletteLookup.Find(BlockId))
	{
		return *Found;
	}
	check(Palette.Num() < MAX_uint16);
	const uint16 NewIndex = static_cast<uint16>(Palette.Add(BlockId));
	PaletteLookup.Add(BlockId, NewIndex);
	return NewIndex;
}

bool FGFVoxelGrid::Set(const FIntVector& Cell, FName BlockId)
{
	if (!IsInBounds(Cell))
	{
		return false;
	}
	const FIntVector ChunkCoord = CellToChunk(Cell);
	TArray<uint16>* Chunk = Chunks.Find(ChunkCoord);
	if (!Chunk)
	{
		if (BlockId.IsNone())
		{
			return true; // already air
		}
		Chunk = &Chunks.Add(ChunkCoord);
		Chunk->SetNumZeroed(GFChunkVolume);
	}
	(*Chunk)[LocalIndex(Cell)] = GetOrAddPaletteIndex(BlockId);
	return true;
}

FName FGFVoxelGrid::Get(const FIntVector& Cell) const
{
	if (!IsInBounds(Cell))
	{
		return NAME_None;
	}
	const TArray<uint16>* Chunk = Chunks.Find(CellToChunk(Cell));
	if (!Chunk)
	{
		return NAME_None;
	}
	const uint16 Index = (*Chunk)[LocalIndex(Cell)];
	return Palette.IsValidIndex(Index) ? Palette[Index] : NAME_None;
}

void FGFVoxelGrid::Clear()
{
	Chunks.Reset();
	Palette.Reset();
	PaletteLookup.Reset();
	Palette.Add(NAME_None); // 0 = air
}

void FGFVoxelGrid::ForEachSolid(TFunctionRef<void(const FIntVector&, FName)> Fn) const
{
	for (const TPair<FIntVector, TArray<uint16>>& Pair : Chunks)
	{
		const FIntVector Base = Pair.Key * GFChunkSize;
		const TArray<uint16>& Data = Pair.Value;
		for (int32 i = 0; i < GFChunkVolume; ++i)
		{
			if (Data[i] != 0)
			{
				const FIntVector Local(i % GFChunkSize, (i / GFChunkSize) % GFChunkSize, i / (GFChunkSize * GFChunkSize));
				Fn(Base + Local, Palette[Data[i]]);
			}
		}
	}
}

int32 FGFVoxelGrid::CountSolid() const
{
	int32 N = 0;
	ForEachSolid([&N](const FIntVector&, FName) { ++N; });
	return N;
}

FGFVoxelSaveData FGFVoxelGrid::ToSaveData() const
{
	FGFVoxelSaveData Out;
	// Compact palette: only ids still in use, air first.
	TMap<uint16, uint16> Remap;
	Out.Palette.Add(NAME_None);
	Remap.Add(0, 0);
	for (const TPair<FIntVector, TArray<uint16>>& Pair : Chunks)
	{
		bool bAnySolid = false;
		for (uint16 V : Pair.Value)
		{
			if (V != 0)
			{
				bAnySolid = true;
				if (!Remap.Contains(V))
				{
					Remap.Add(V, static_cast<uint16>(Out.Palette.Add(Palette[V])));
				}
			}
		}
		if (!bAnySolid)
		{
			continue; // skip all-air chunks
		}
		FGFVoxelChunkSave& C = Out.Chunks.AddDefaulted_GetRef();
		C.Coord = Pair.Key;
		uint16 Run = 0;
		uint16 Current = Remap[Pair.Value[0]];
		for (int32 i = 0; i < GFChunkVolume; ++i)
		{
			const uint16 V = Remap[Pair.Value[i]];
			if (V == Current && Run < MAX_uint16)
			{
				++Run;
			}
			else
			{
				C.RLE.Add(Run);
				C.RLE.Add(Current);
				Current = V;
				Run = 1;
			}
		}
		C.RLE.Add(Run);
		C.RLE.Add(Current);
	}
	// Deterministic order for diffable saves.
	Out.Chunks.Sort([](const FGFVoxelChunkSave& A, const FGFVoxelChunkSave& B)
	{
		if (A.Coord.Z != B.Coord.Z) return A.Coord.Z < B.Coord.Z;
		if (A.Coord.Y != B.Coord.Y) return A.Coord.Y < B.Coord.Y;
		return A.Coord.X < B.Coord.X;
	});
	return Out;
}

bool FGFVoxelGrid::FromSaveData(const FGFVoxelSaveData& Data)
{
	Clear();
	if (Data.Palette.Num() == 0)
	{
		return true;
	}
	TArray<uint16> Remap;
	Remap.SetNumZeroed(Data.Palette.Num());
	for (int32 i = 1; i < Data.Palette.Num(); ++i)
	{
		Remap[i] = GetOrAddPaletteIndex(Data.Palette[i]);
	}
	bool bOk = true;
	for (const FGFVoxelChunkSave& C : Data.Chunks)
	{
		TArray<uint16>& Chunk = Chunks.Add(C.Coord);
		Chunk.SetNumZeroed(GFChunkVolume);
		int32 Write = 0;
		for (int32 i = 0; i + 1 < C.RLE.Num(); i += 2)
		{
			const int32 Run = C.RLE[i];
			const uint16 Idx = C.RLE[i + 1];
			const uint16 Mapped = Remap.IsValidIndex(Idx) ? Remap[Idx] : 0;
			bOk &= Remap.IsValidIndex(Idx);
			for (int32 r = 0; r < Run && Write < GFChunkVolume; ++r)
			{
				Chunk[Write++] = Mapped;
			}
		}
		bOk &= (Write == GFChunkVolume);
	}
	return bOk;
}

uint32 FGFVoxelGrid::GetContentHash() const
{
	// XOR of per-cell hashes is order-independent.
	uint32 Hash = 0;
	ForEachSolid([&Hash](const FIntVector& Cell, FName Id)
	{
		Hash ^= HashCombine(GetTypeHash(Cell), GetTypeHash(Id.ToString()));
	});
	return HashCombine(Hash, GetTypeHash(CountSolid()));
}

FIntVector FGFVoxelGrid::ChunkLocalToCell(const FIntVector& ChunkCoord, int32 Index)
{
	return ChunkCoord * GFChunkSize + FIntVector(Index % GFChunkSize, (Index / GFChunkSize) % GFChunkSize, Index / (GFChunkSize * GFChunkSize));
}

TArray<FIntVector> FGFVoxelGrid::GetChunkCoords() const
{
	TArray<FIntVector> Out;
	Chunks.GetKeys(Out);
	return Out;
}

void FGFVoxelGrid::EncodeChunk(const FIntVector& ChunkCoord, TArray<FName>& OutPalette, TArray<uint16>& OutRLE) const
{
	OutPalette.Reset();
	OutRLE.Reset();
	OutPalette.Add(NAME_None);
	const TArray<uint16>* Data = Chunks.Find(ChunkCoord);
	if (!Data)
	{
		OutRLE.Add(static_cast<uint16>(GFChunkVolume));
		OutRLE.Add(0);
		return;
	}
	TMap<uint16, uint16> Remap;
	Remap.Add(0, 0);
	uint16 Run = 0;
	uint16 Current = 0;
	for (int32 i = 0; i < GFChunkVolume; ++i)
	{
		const uint16 Raw = (*Data)[i];
		uint16* Mapped = Remap.Find(Raw);
		const uint16 V = Mapped ? *Mapped : Remap.Add(Raw, static_cast<uint16>(OutPalette.Add(Palette[Raw])));
		if (i == 0)
		{
			Current = V;
			Run = 1;
		}
		else if (V == Current && Run < MAX_uint16)
		{
			++Run;
		}
		else
		{
			OutRLE.Add(Run);
			OutRLE.Add(Current);
			Current = V;
			Run = 1;
		}
	}
	OutRLE.Add(Run);
	OutRLE.Add(Current);
}

bool FGFVoxelGrid::DecodeChunk(const FIntVector& ChunkCoord, const TArray<FName>& InPalette, const TArray<uint16>& InRLE)
{
	TArray<uint16> Remap;
	Remap.SetNumZeroed(InPalette.Num());
	for (int32 i = 1; i < InPalette.Num(); ++i)
	{
		Remap[i] = GetOrAddPaletteIndex(InPalette[i]);
	}
	TArray<uint16> NewData;
	NewData.SetNumZeroed(GFChunkVolume);
	int32 Write = 0;
	bool bOk = true;
	bool bAnySolid = false;
	for (int32 i = 0; i + 1 < InRLE.Num(); i += 2)
	{
		const int32 Run = InRLE[i];
		const uint16 Idx = InRLE[i + 1];
		bOk &= Remap.IsValidIndex(Idx);
		const uint16 Mapped = Remap.IsValidIndex(Idx) ? Remap[Idx] : 0;
		bAnySolid |= (Mapped != 0 && Run > 0);
		for (int32 r = 0; r < Run && Write < GFChunkVolume; ++r)
		{
			NewData[Write++] = Mapped;
		}
	}
	bOk &= (Write == GFChunkVolume);
	if (bAnySolid)
	{
		Chunks.Add(ChunkCoord, MoveTemp(NewData));
	}
	else
	{
		Chunks.Remove(ChunkCoord);
	}
	return bOk;
}

void FGFVoxelGrid::CopyChunkFrom(const FGFVoxelGrid& Other, const FIntVector& ChunkCoord)
{
	TArray<FName> Pal;
	TArray<uint16> Rle;
	Other.EncodeChunk(ChunkCoord, Pal, Rle);
	DecodeChunk(ChunkCoord, Pal, Rle);
}

uint32 FGFVoxelGrid::GetChunkHash(const FIntVector& ChunkCoord) const
{
	const TArray<uint16>* Data = Chunks.Find(ChunkCoord);
	if (!Data)
	{
		return 0;
	}
	uint32 Hash = 0;
	for (int32 i = 0; i < GFChunkVolume; ++i)
	{
		if ((*Data)[i] != 0)
		{
			Hash ^= HashCombine(GetTypeHash(i), GetTypeHash(Palette[(*Data)[i]].ToString()));
		}
	}
	return Hash;
}

FGFVoxelSaveData FGFVoxelGrid::ExtractRegion(const FIntVector& Min, const FIntVector& Max) const
{
	const FIntVector RegionSize = Max - Min + FIntVector(1, 1, 1);
	FGFVoxelGrid Temp(RegionSize);
	for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				const FName Id = Get(FIntVector(X, Y, Z));
				if (!Id.IsNone())
				{
					Temp.Set(FIntVector(X, Y, Z) - Min, Id);
				}
			}
		}
	}
	return Temp.ToSaveData();
}

int32 FGFVoxelGrid::CountSolidInRegion(const FIntVector& Min, const FIntVector& Max) const
{
	int32 N = 0;
	ForEachSolid([&](const FIntVector& C, FName)
	{
		if (C.X >= Min.X && C.Y >= Min.Y && C.Z >= Min.Z && C.X <= Max.X && C.Y <= Max.Y && C.Z <= Max.Z)
		{
			++N;
		}
	});
	return N;
}

uint32 FGFVoxelGrid::GetRegionHash(const FIntVector& Min, const FIntVector& Max) const
{
	uint32 Hash = 0;
	int32 N = 0;
	ForEachSolid([&](const FIntVector& C, FName Id)
	{
		if (C.X >= Min.X && C.Y >= Min.Y && C.Z >= Min.Z && C.X <= Max.X && C.Y <= Max.Y && C.Z <= Max.Z)
		{
			Hash ^= HashCombine(GetTypeHash(C - Min), GetTypeHash(Id.ToString()));
			++N;
		}
	});
	return HashCombine(Hash, GetTypeHash(N));
}

uint32 FGFVoxelGrid::HashSaveData(const FGFVoxelSaveData& Data, const FIntVector& RegionSize)
{
	FGFVoxelGrid Temp(RegionSize);
	Temp.FromSaveData(Data);
	return Temp.GetRegionHash(FIntVector::ZeroValue, RegionSize - FIntVector(1, 1, 1));
}
