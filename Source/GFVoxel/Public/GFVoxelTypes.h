#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFVoxelTypes.generated.h"

class UStaticMesh;
class UMaterialInterface;

/** Edge length of one block in cm (PLAN.md / ASSET_BRIEFS.md: 1 block = 100 cm). */
static constexpr float GFBlockSize = 100.f;
/** Chunk edge in blocks (16x16x16 uint16 ids, PLAN.md section 4). */
static constexpr int32 GFChunkSize = 16;
static constexpr int32 GFChunkVolume = GFChunkSize * GFChunkSize * GFChunkSize;

/** Row for DT_Blocks. Row name = block id (e.g. Grass, WoodPlank, Stone). */
USTRUCT(BlueprintType)
struct GFVOXEL_API FGFBlockRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	FText DisplayName;

	/** Artist-made SM_Block_* (bottom-centre pivot). Empty = engine cube placeholder tinted with DebugColor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	/** True for art meshes (bottom centre). The engine placeholder cube is centre-pivoted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	bool bPivotIsBottomCenter = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	FLinearColor DebugColor = FLinearColor::White;

	/** Seconds to break by hand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block", meta = (ClampMin = "0"))
	float Hardness = 0.5f;

	/** Item given back when removed (None = the block's own row name). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	FName DropItem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	bool bPlaceable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	bool bDyeable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	bool bEmissive = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block")
	FName SoundSet;
};

/** One saved chunk: coordinate + run-length encoded palette indices (pairs of run length, index). */
USTRUCT()
struct GFVOXEL_API FGFVoxelChunkSave
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	FIntVector Coord = FIntVector::ZeroValue;

	UPROPERTY(SaveGame)
	TArray<uint16> RLE;
};

/**
 * Saved voxel state. The palette maps block ids to indices per save, so DataTable reordering
 * never breaks old saves (PLAN.md section 7). Index 0 is always air.
 */
USTRUCT()
struct GFVOXEL_API FGFVoxelSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	TArray<FName> Palette;

	UPROPERTY(SaveGame)
	TArray<FGFVoxelChunkSave> Chunks;
};

/**
 * Pure voxel storage, no UObjects, so it can be unit-tested. Cells are integer block coordinates
 * inside [0, Size). Chunks are allocated on first write.
 */
struct GFVOXEL_API FGFVoxelGrid
{
	explicit FGFVoxelGrid(FIntVector InSizeInBlocks = FIntVector(64, 64, 32));

	FIntVector GetSize() const { return Size; }
	bool IsInBounds(const FIntVector& Cell) const;

	/** Sets a cell. BlockId None = air (remove). Returns false if out of bounds. */
	bool Set(const FIntVector& Cell, FName BlockId);
	/** Returns None for air or out-of-bounds. */
	FName Get(const FIntVector& Cell) const;
	bool IsSolid(const FIntVector& Cell) const { return !Get(Cell).IsNone(); }

	void Clear();
	int32 CountSolid() const;
	void ForEachSolid(TFunctionRef<void(const FIntVector& Cell, FName BlockId)> Fn) const;

	FGFVoxelSaveData ToSaveData() const;
	/** Replaces the grid content. Unknown ids are kept by name; the world decides how to render them. */
	bool FromSaveData(const FGFVoxelSaveData& Data);

	/** Order-independent content hash (cells + ids) for round-trip tests. */
	uint32 GetContentHash() const;

	static FIntVector CellToChunk(const FIntVector& Cell);
	static int32 LocalIndex(const FIntVector& Cell);
	static FIntVector ChunkLocalToCell(const FIntVector& ChunkCoord, int32 LocalIndex);

	// --- Per-chunk access (replication: snapshots and deltas, PLAN.md section 4.1) ---
	TArray<FIntVector> GetChunkCoords() const;
	bool HasChunk(const FIntVector& ChunkCoord) const { return Chunks.Contains(ChunkCoord); }
	/** Chunk as (palette, RLE pairs of run/index). A missing chunk encodes as all air. */
	void EncodeChunk(const FIntVector& ChunkCoord, TArray<FName>& OutPalette, TArray<uint16>& OutRLE) const;
	/** Replaces one chunk from an encoding made by EncodeChunk. Returns false if the RLE is malformed. */
	bool DecodeChunk(const FIntVector& ChunkCoord, const TArray<FName>& InPalette, const TArray<uint16>& InRLE);
	/** Copies one chunk's cells from another grid of the same size. */
	void CopyChunkFrom(const FGFVoxelGrid& Other, const FIntVector& ChunkCoord);
	uint32 GetChunkHash(const FIntVector& ChunkCoord) const;

	// --- Regions (plot blueprints, PLAN.md section 2a) ---
	/** Cells in [Min, Max] (inclusive) as save data relative to Min. */
	FGFVoxelSaveData ExtractRegion(const FIntVector& Min, const FIntVector& Max) const;
	int32 CountSolidInRegion(const FIntVector& Min, const FIntVector& Max) const;
	/** Order-independent hash of a region, relative to Min (so the same build hashes equal on any plot). */
	uint32 GetRegionHash(const FIntVector& Min, const FIntVector& Max) const;
	static uint32 HashSaveData(const FGFVoxelSaveData& Data, const FIntVector& RegionSize);

private:
	uint16 GetOrAddPaletteIndex(FName BlockId);

	FIntVector Size;
	/** Runtime palette; index 0 = air. */
	TArray<FName> Palette;
	TMap<FName, uint16> PaletteLookup;
	TMap<FIntVector, TArray<uint16>> Chunks;
};
