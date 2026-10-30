#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFVoxelTypes.h"
#include "GFVoxelReplication.h"
#include "GFVoxelWorld.generated.h"

class UDataTable;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGFOnBlockChanged, FIntVector, Cell, FName, BlockId);

/**
 * The hub's block grid: 100 cm cells, block types from DT_Blocks.
 * Rendering: one instanced mesh per (chunk, block type); only dirty chunks rebuild (PLAN.md section 8, first version).
 * Networking (PLAN.md section 4.1): the server owns the grid. Clients keep the replicated authoritative grid
 * (AuthGrid) plus their own predicted edits; the displayed grid = AuthGrid + pending predictions.
 * Cell (0,0,0) sits at the actor location; a cell's bottom-centre is ActorLocation + (Cell + (0.5,0.5,0)) * 100.
 */
UCLASS()
class GFVOXEL_API AGFVoxelWorld : public AActor
{
	GENERATED_BODY()

public:
	AGFVoxelWorld();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** DataTable with FGFBlockRow rows. Loaded from Data/CSV/DT_Blocks.csv when unset (each machine loads its own). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Voxel")
	TObjectPtr<UDataTable> BlockTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "GF|Voxel")
	FIntVector PlotSizeInBlocks = FIntVector(64, 64, 32);

	/** If the grid is empty on BeginPlay (server), fill this many layers with GroundBlock so an empty map is playable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Voxel")
	int32 StarterGroundLayers = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Voxel")
	FName GroundBlock = TEXT("Grass");

	/** Edits per chunk before the server folds them into a new base snapshot. */
	UPROPERTY(EditAnywhere, Category = "GF|Voxel|Net")
	int32 CompactThreshold = 64;

	// ---------------- Server (authority) API ----------------

	/**
	 * Server only. Sets a cell (None = remove). Validates bounds, occupancy (place needs an empty cell, remove a
	 * solid one) and that the block is placeable. Permission/reach/inventory checks are the caller's job (GFNet).
	 */
	bool ServerApplyEdit(const FIntVector& Cell, FName BlockId, FName* OutPrevious = nullptr, FString* OutReason = nullptr);

	/** Server/standalone convenience wrappers around ServerApplyEdit. Return false on clients. */
	UFUNCTION(BlueprintCallable, Category = "GF|Voxel")
	bool PlaceBlock(FIntVector Cell, FName BlockId);

	/** Returns the removed block id (None if the cell was empty or on a client). */
	UFUNCTION(BlueprintCallable, Category = "GF|Voxel")
	FName RemoveBlock(FIntVector Cell);

	/** Server: fills the bottom NumLayers layers with BlockId. OnBlockChanged fires for every block it replaces (not for filled air). */
	UFUNCTION(BlueprintCallable, Category = "GF|Voxel")
	void FillLayers(int32 NumLayers, FName BlockId);

	/**
	 * Server: replaces the grid with BlockAt (None = air). One rebase, then OnGridReset.
	 * Used by the hub overworld so a full terrain does not emit one delta per cell.
	 */
	void ServerStamp(TFunctionRef<FName(const FIntVector& Cell)> BlockAt);

	/** Server: removes every block in [Min, Max]. Returns how many were removed. */
	int32 ClearRegion(const FIntVector& Min, const FIntVector& Max);
	/** Server: clears [Min, Min+size) and writes Data (relative to Min) into it. */
	bool ApplyRegion(const FIntVector& Min, const FGFVoxelSaveData& Data, const FIntVector& RegionSize);

	UFUNCTION(BlueprintCallable, Category = "GF|Voxel")
	void ClearAll();

	FGFVoxelSaveData SaveData() const { return Grid.ToSaveData(); }
	/** Server/standalone: replaces the grid and re-bases every chunk. */
	bool LoadData(const FGFVoxelSaveData& Data);

	// ---------------- Client prediction ----------------

	/** Client: show an edit right away while the server decides. */
	void PredictEdit(int32 EditId, const FIntVector& Cell, FName BlockId);
	/** Client: server verdict. Rejected = roll the cell back to the authoritative block. */
	void ResolvePrediction(int32 EditId, bool bAccepted, int32 ServerChunkRevision);
	int32 NumPendingPredictions() const { return Predictions.Num(); }

	// ---------------- Queries ----------------

	UFUNCTION(BlueprintPure, Category = "GF|Voxel")
	FName GetBlock(FIntVector Cell) const { return Grid.Get(Cell); }

	UFUNCTION(BlueprintPure, Category = "GF|Voxel")
	FIntVector WorldToCell(FVector WorldLocation) const;

	/** Bottom-centre of the cell in world space. */
	UFUNCTION(BlueprintPure, Category = "GF|Voxel")
	FVector CellToWorld(FIntVector Cell) const;

	/** From a trace hit on a block: the cell that was hit, and the empty cell in front of the hit face. */
	UFUNCTION(BlueprintPure, Category = "GF|Voxel")
	void HitToCells(FVector HitLocation, FVector HitNormal, FIntVector& OutHitCell, FIntVector& OutAdjacentCell) const;

	UFUNCTION(BlueprintPure, Category = "GF|Voxel")
	TArray<FName> GetPlaceableBlockIds() const;

	/** The displayed grid (authoritative on the server; authoritative + predictions on a client). */
	const FGFVoxelGrid& GetGrid() const { return Grid; }
	/** What the server has confirmed (equals GetGrid() on the server). */
	const FGFVoxelGrid& GetAuthoritativeGrid() const { return HasAuthority() ? Grid : AuthGrid; }
	uint32 GetAuthoritativeHash() const { return GetAuthoritativeGrid().GetContentHash(); }
	/** Server: current revision of a chunk. Client: the revision it has applied. */
	int32 GetChunkRevision(const FIntVector& ChunkCoord) const;
	/** Sum of all chunk revisions (tests: has a client caught up?). */
	int64 GetTotalRevision() const;
	const FGFBlockRow* FindBlockRow(FName BlockId) const;

	UPROPERTY(BlueprintAssignable, Category = "GF|Voxel")
	FGFOnBlockChanged OnBlockChanged;

	/** Server: the grid was replaced as a whole (ClearAll, LoadData) without an OnBlockChanged per cell. */
	FSimpleMulticastDelegate OnGridReset;

	// Called by the fast-array items (clients).
	void OnChunkReplicated(const FIntVector& ChunkCoord);

	// ---------------- Paint overlay (display only) ----------------
	// A colour drawn over a cell's block. Any machine; not part of the grid, its save or its replication: whoever owns
	// the paint (GFGame's AGFPaintVolume) keeps it in sync on each machine. Painted blocks get their own instanced
	// mesh per (chunk, block, colour), drawn with a copy of the block's material whose PaintColorParameter is set.
	// A cell keeps its colour while empty, so a block placed there later would show it: owners clear it on removal.

	/** Vector parameter the paint colour is written to on the painted copy of a block's material. */
	static const FName PaintColorParameter;

	/** Draws Cell's block in Color (opaque; alpha ignored, stored as 8-bit sRGB). */
	void SetCellPaint(const FIntVector& Cell, const FLinearColor& Color);
	void ClearCellPaint(const FIntVector& Cell);
	/** Drops every overlay colour in the box [Min, Max] (inclusive). */
	void ClearPaintInRegion(const FIntVector& Min, const FIntVector& Max);
	void ClearAllPaint();
	bool GetCellPaint(const FIntVector& Cell, FLinearColor& OutColor) const;
	int32 NumPaintedCells() const { return PaintedCells.Num(); }

	/** Rebuilds dirty chunk meshes now instead of next tick (no-op on a dedicated server). */
	void FlushMeshes() { RebuildDirtyChunks(); }
	/** Instances drawn for BlockId in a chunk with the given paint colour (unpainted when bPainted is false). Tests. */
	int32 CountDrawnInstances(const FIntVector& ChunkCoord, FName BlockId, bool bPainted, const FLinearColor& Color = FLinearColor::Black) const;
	/** Material on the mesh drawing BlockId in that colour in a chunk, or null. Tests. */
	UMaterialInterface* GetDrawnMaterial(const FIntVector& ChunkCoord, FName BlockId, bool bPainted, const FLinearColor& Color = FLinearColor::Black) const;

protected:
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;

	void EnsureGridSize();
	void EnsureBlockTable();
	/** Server: bring the replicated base of a chunk up to date with the grid and clear its deltas. */
	void ServerRebaseChunk(const FIntVector& ChunkCoord);
	void ServerRebaseAll();
	FGFChunkBaseItem* FindBase(const FIntVector& ChunkCoord);
	FGFChunkDeltaItem* FindDelta(const FIntVector& ChunkCoord);
	/** Client: rebuild AuthGrid's chunk from base + deltas, then lay pending predictions over it. */
	void ClientReconcileChunk(const FIntVector& ChunkCoord);
	void RefreshDisplayedChunk(const FIntVector& ChunkCoord);

	void MarkChunkDirty(const FIntVector& ChunkCoord);
	void RebuildDirtyChunks();
	void RebuildChunkMeshes(const FIntVector& ChunkCoord);
	/** PaintKey 0 = unpainted; otherwise the packed sRGB colour with alpha 255 (see PaintKeyOf). */
	UInstancedStaticMeshComponent* GetOrCreateIsm(const FIntVector& ChunkCoord, FName BlockId, uint32 PaintKey = 0);
	UMaterialInterface* GetBlockMaterial(FName BlockId);
	/** Row mesh, else a known art mesh, else the engine cube. Art meshes are bottom-centre. */
	UStaticMesh* ResolveDrawMesh(FName BlockId, bool& bOutBottomPivot);
	UMaterialInterface* GetPaintedMaterial(FName BlockId, uint32 PaintKey);
	static uint32 PaintKeyOf(const FLinearColor& Color);
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "GF|Voxel")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Replicated)
	FGFChunkBaseArray ChunkBases;

	UPROPERTY(Replicated)
	FGFChunkDeltaArray ChunkDeltas;

	/** Keeps the per-(chunk, type) meshes alive; lookup goes through IsmIndex. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> AllIsms;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UMaterialInterface>> MaterialByBlock;

	/** Painted copies of block materials, keyed "<Block>_P<RRGGBBAA>". */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UMaterialInterface>> PaintedMaterials;

	/** (chunk, block, paint key) -> mesh. Paint key 0 = unpainted. */
	TMap<TTuple<FIntVector, FName, uint32>, UInstancedStaticMeshComponent*> IsmIndex;
	/** Overlay colours (sRGB, alpha 255) by cell. */
	TMap<FIntVector, FColor> PaintedCells;
	TSet<FIntVector> DirtyChunks;

	FGFVoxelGrid Grid;
	/** Client only: the server-confirmed state. */
	FGFVoxelGrid AuthGrid;

	struct FCachedBlockDraw
	{
		UStaticMesh* Mesh = nullptr;
		bool bBottomPivot = false;
		bool bReady = false;
	};
	TMap<FName, FCachedBlockDraw> DrawCache;

	struct FChunkApplied
	{
		int32 BaseRevision = -1;
		int32 EditsApplied = 0;
	};
	TMap<FIntVector, FChunkApplied> ClientApplied;

	struct FPrediction
	{
		FIntVector Cell = FIntVector::ZeroValue;
		FName BlockId;
		bool bAcked = false;
		int32 AckRevision = 0;
	};
	TMap<int32, FPrediction> Predictions;
};

/**
 * Block albedo for the lit floor. Imported meshes ship on the engine grid (near white),
 * and a few debug colours (ice) are almost white too. Under a fixed exposure that
 * does not darken them, the ground reads as a white sheet. Keep luminance in 0.2..0.5.
 * Black stays black. Alpha is forced to 1.
 */
inline FLinearColor GFClampBlockAlbedo(FLinearColor Color)
{
	Color.A = 1.f;
	Color.R = FMath::Max(0.f, Color.R);
	Color.G = FMath::Max(0.f, Color.G);
	Color.B = FMath::Max(0.f, Color.B);
	const float Luma = Color.GetLuminance();
	constexpr float MinLuma = 0.20f;
	constexpr float MaxLuma = 0.50f;
	if (Luma > MaxLuma)
	{
		Color *= MaxLuma / Luma;
	}
	else if (Luma > KINDA_SMALL_NUMBER && Luma < MinLuma)
	{
		Color *= MinLuma / Luma;
	}
	Color.A = 1.f;
	return Color;
}
