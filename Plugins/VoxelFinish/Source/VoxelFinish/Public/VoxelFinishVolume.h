#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelFinishReplication.h"
#include "VoxelFinishSession.h"
#include "GameFramework/PlayerState.h"
#include "VoxelFinishVolume.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;

/**
 * Replicates the finish palette and per-chunk paint deltas.
 * Paint commands run on the server (see UVoxelFinishPainterComponent). A repaint marks only that chunk's fast-array item dirty.
 */
UCLASS()
class VOXELFINISH_API AVoxelFinishVolume : public AActor
{
	GENERATED_BODY()

public:
	AVoxelFinishVolume();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;

	/**
	 * Server-side paint. Not an RPC: clients paint through UVoxelFinishPainterComponent on an actor they own,
	 * and the server passes the payer it derived from that owner.
	 */
	EVoxelFinishPaintResult AuthorityPaint(const FIntVector& Cell, const FFinish& Finish, APlayerState* Payer);

	void SetCanAffordAndSpend(TFunction<bool(const FFinish&, APlayerState*)> InFn);

	/**
	 * Server: unpaints Cells, e.g. when their blocks were removed, and frees finishes nothing uses any more
	 * (FVoxelFinishSession::ServerClearCells). Replicates like a paint. Returns how many cells were unpainted.
	 */
	int32 AuthorityClearCells(const TArray<FIntVector>& Cells);
	/** Server: unpaints every painted cell the paint target resolver refuses (no resolver = every cell). O(painted cells). */
	int32 AuthorityClearUnresolvable();

	/**
	 * Server: replaces all paint with a save (FVoxelFinishSession::RestoreFromSave; empty data = no paint) and
	 * replicates the result. Cells the paint target resolver refuses are then unpainted, so bind it (and load the
	 * game's blocks) first.
	 */
	void AuthorityRestore(const FVoxelFinishSaveData& Data);

	/**
	 * Game hook for server paint requests: true when Cell is an existing solid block, with that block's world-space
	 * centre in OutCenter (UVoxelFinishPainterComponent measures reach to it). The hook must only read: it must not
	 * create a chunk or a block. Until one is bound every request through the painter component is refused
	 * (RejectedNoBlock).
	 */
	void SetPaintTargetResolver(TFunction<bool(const FIntVector& Cell, FVector& OutCenter)> InFn);
	bool ResolvePaintTarget(const FIntVector& Cell, FVector& OutCenter) const;

	/** Adds a paintable effect id on the server (the built-in sparkle id and accepted starter graphs are added at startup). */
	void AllowEffectId(int32 EffectId);

	/**
	 * Per-world lookup texture (transient, 4096x4, nearest, linear). Created on first use on machines that render.
	 * Only the columns of changed palette ids are re-uploaded. The shared T_FinishLookup asset is never written.
	 */
	UTexture2D* GetLookupTexture();

	/** Points a material instance's FinishLookup texture parameter at GetLookupTexture(). One instance per master, not per block. */
	void ApplyLookupTo(UMaterialInstanceDynamic* Material);

	FVoxelFinishSession& GetSession() { return Session; }
	const FVoxelFinishSession& GetSession() const { return Session; }

	void NotifyPaletteItem(const FVoxelFinishPaletteItem& Item);
	void NotifyPaletteItemRemoved(const FVoxelFinishPaletteItem& Item, const FVoxelFinishPaletteArray& InArray);
	void NotifyChunkItem(const FVoxelFinishChunkItem& Item);

protected:
	/**
	 * Display hooks for games that draw paint themselves. Both run on every machine: on the server after the session
	 * changed (and its replication was updated), on clients after the replicated item was applied to the session.
	 * A palette slot is reported before the chunks that use it on the server; on clients they may arrive in either
	 * order, so a chunk can name an id whose finish is not known yet (its slot callback follows).
	 */
	virtual void OnPaintChunksChanged(const TArray<FIntVector>& ChunkCoords) {}
	/** A palette slot was written or freed (Find returns null for a freed slot). */
	virtual void OnPaletteSlotChanged(uint16 FinishId) {}

private:
	void RegisterAllowedEffects();
	void PushSessionChanges();
	void MarkLookupDirty(uint16 FinishId);
	void FlushLookup();
	void UpsertPaletteItem(uint16 FinishId);
	void RemovePaletteItem(uint16 FinishId);
	void UpsertChunk(const FIntVector& ChunkCoord);

	UPROPERTY(Replicated)
	FVoxelFinishPaletteArray PaletteRep;

	UPROPERTY(Replicated)
	FVoxelFinishChunkArray ChunkRep;

	FVoxelFinishSession Session;

	TFunction<bool(const FIntVector&, FVector&)> PaintTargetResolver;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LookupTexture;

	TArray<FColor> LookupPixels;
	TSet<uint16> PendingLookupIds;
};
