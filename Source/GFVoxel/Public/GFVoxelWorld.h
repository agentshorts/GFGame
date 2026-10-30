#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFVoxelTypes.h"
#include "GFVoxelReplication.h"
#include "GFVoxelWorld.generated.h"

class UDataTable;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

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

	UFUNCTION(BlueprintCallable, Category = "GF|Voxel")
	void FillLayers(int32 NumLayers, FName BlockId);

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

	// Called by the fast-array items (clients).
	void OnChunkReplicated(const FIntVector& ChunkCoord);

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
	UInstancedStaticMeshComponent* GetOrCreateIsm(const FIntVector& ChunkCoord, FName BlockId);
	UMaterialInterface* GetBlockMaterial(FName BlockId);
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

	TMap<TTuple<FIntVector, FName>, UInstancedStaticMeshComponent*> IsmIndex;
	TSet<FIntVector> DirtyChunks;

	FGFVoxelGrid Grid;
	/** Client only: the server-confirmed state. */
	FGFVoxelGrid AuthGrid;

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
