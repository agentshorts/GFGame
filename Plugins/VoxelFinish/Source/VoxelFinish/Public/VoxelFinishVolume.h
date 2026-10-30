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

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LookupTexture;

	TArray<FColor> LookupPixels;
	TSet<uint16> PendingLookupIds;
};
