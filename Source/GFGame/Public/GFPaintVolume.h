#pragma once

// GFGame's hookup of the VoxelFinish plugin: block paint costs dye items through the ledger, and only existing
// solid blocks of the hub's voxel world can be painted. VoxelFinish cells are AGFVoxelWorld cells (same coordinates).
#include "CoreMinimal.h"
#include "VoxelFinishVolume.h"
#include "GFPaintVolume.generated.h"

struct FFinish;
struct FGFItemStack;
class AGFVoxelWorld;

/** What a finish costs to paint (PLAN.md: "Dyes (from Flowers) recolor outfits and blocks"). */
struct GFGAME_API FGFPaintCost
{
	/** Colours a painted block shows: ColorA x Tint, plus ColorB x Tint when a pattern is used. Clamped to 0..1. */
	static TArray<FLinearColor> VisibleColors(const FFinish& Finish);
	/** Dye items (one per colour family, no repeats) that painting one block with Finish uses. */
	static TArray<FName> DyesFor(const FFinish& Finish);
	/** The colour a painted block is drawn in: ColorA x Tint. Patterns, ColorB and effects are not drawn yet. */
	static FLinearColor DisplayColor(const FFinish& Finish);
};

/**
 * Per-player block credits. FGFDyes::CostForBlocks: one dye item covers FGFDyes::BlocksPerDye blocks, so the first
 * block of each batch burns the item and the next BlocksPerDye - 1 blocks of that dye use the credit it left.
 * Credits are kept by the server for the session only (not saved).
 */
struct GFGAME_API FGFPaintCredits
{
	/**
	 * Spends one block of each dye the colours need. Burn is called at most once, with one CostForBlocks batch for every
	 * dye that has no credit left; it must be all or nothing. False (and nothing changed) when Burn fails.
	 */
	bool TrySpend(const FString& Account, const TArray<FLinearColor>& Colors, TFunctionRef<bool(const TArray<FGFItemStack>&)> Burn);
	int32 GetCredits(const FString& Account, FName DyeItem) const;
	void Reset() { Credits.Reset(); }

private:
	TMap<FString, TMap<FName, int32>> Credits;
};

/**
 * The hub's paint volume. Spawned with the game state (AGFGameMode::StartupDirectorClasses).
 * Server: CanAffordAndSpend burns dye items from the painter's account (ledger reason "Paint.Dye"); a repaint with the
 * finish the block already has is free (the plugin returns AlreadyPainted before spending). With bFreeBuild on, paint
 * is free like block placement. The paint target must be a solid block of AGFVoxelWorld's authoritative grid; the
 * check only reads the grid, so painting never creates a chunk. Reach and the rate limit are on the player's
 * UVoxelFinishPainterComponent (AGFPlayerController::Painter: reach = AGFGameState::MaxEditReach, read at paint time,
 * <= 0 = no limit; 10 paints per second).
 * Display (every machine): painted cells are drawn on AGFVoxelWorld's blocks through its paint overlay, in
 * FGFPaintCost::DisplayColor. Changes are queued by the plugin's hooks and pushed to the voxel world on the next tick
 * (or FlushPaintDisplay); a voxel world that appears later (a joining client) gets every painted cell.
 * Removal (server): a removed or replaced (FillLayers) block loses its paint (no refund), and ClearAll/LoadData unpaint every
 * cell that is no longer a solid block. Bound when the volume first finds the voxel world (tick or FlushPaintDisplay).
 */
UCLASS()
class GFGAME_API AGFPaintVolume : public AVoxelFinishVolume
{
	GENERATED_BODY()

public:
	static FName BurnReason() { return TEXT("Paint.Dye"); }
	static AGFPaintVolume* Find(const UWorld* World);

	AGFPaintVolume();
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;

	bool ServerSpend(const FFinish& Finish, APlayerState* Payer);
	bool ResolveTarget(const FIntVector& Cell, FVector& OutCenter) const;
	const FGFPaintCredits& GetCredits() const { return Credits; }

	/** The hub's voxel world on this machine, or null before it exists. */
	AGFVoxelWorld* FindVoxelWorld() const;
	/** Pushes queued paint changes to the voxel world's overlay now (tests; the tick does it otherwise). */
	void FlushPaintDisplay();

protected:
	virtual void OnPaintChunksChanged(const TArray<FIntVector>& ChunkCoords) override;
	virtual void OnPaletteSlotChanged(uint16 FinishId) override;

private:
	void DrawPaintChunk(AGFVoxelWorld* Voxel, const FIntVector& PaintChunk);
	/** Server: listen for block removal and grid resets on Voxel (and stop listening on the previous one). */
	void BindVoxelWorld(AGFVoxelWorld* Voxel);
	UFUNCTION()
	void HandleBlockChanged(FIntVector Cell, FName BlockId);
	void HandleGridReset();

	FGFPaintCredits Credits;

	/** The voxel world the overlay was last filled on; a different one gets a full redraw. */
	TWeakObjectPtr<AGFVoxelWorld> DrawnOn;
	TWeakObjectPtr<AGFVoxelWorld> BoundTo;
	FDelegateHandle GridResetHandle;
	TSet<FIntVector> PendingChunks;
	TSet<uint16> PendingSlots;
};
