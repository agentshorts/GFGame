#pragma once

#include "CoreMinimal.h"
#include "VoxelFinishGrid.h"
#include "VoxelFinishPalette.h"
#include "VoxelFinishTypes.h"
#include "GameFramework/PlayerState.h"

enum class EVoxelFinishPaintResult : uint8
{
	Applied,
	/** The cell already holds this finish. Nothing is spent and nothing is replicated. */
	AlreadyPainted,
	RejectedNotAuthority,
	RejectedCannotAfford,
	RejectedPaletteFull,
	RejectedEffectCap,
	/** The paint request had no player behind it (set by UVoxelFinishPainterComponent). */
	RejectedNoPayer,
	/** No AVoxelFinishVolume was found for the request. */
	RejectedNoVolume,
	/** Non-finite colour or intensity, unknown pattern id, or an effect id that is not registered. */
	RejectedInvalidFinish,
	/** The player sent more paint requests than UVoxelFinishPainterComponent::MaxPaintsPerSecond allows. */
	RejectedRateLimited,
	/** The block is farther than UVoxelFinishPainterComponent::MaxReachCm from the player's pawn, or there is no pawn. */
	RejectedOutOfReach,
	/** The cell is not an existing solid block (AVoxelFinishVolume::SetPaintTargetResolver), or no resolver is bound. */
	RejectedNoBlock
};

/**
 * Server paint session: palette + mesh-source grid + chunk deltas.
 * Clients reject paint. Replication copies the palette table and only the dirtied chunk.
 * CanAffordAndSpend is the dye check. The game spends materials there; this plugin has no currency of its own.
 * Until a hook is bound every paint is refused (RejectedCannotAfford).
 */
class VOXELFINISH_API FVoxelFinishSession
{
public:
	explicit FVoxelFinishSession(bool bInAuthority = true);

	void SetAuthority(bool bInAuthority);
	bool IsAuthority() const { return Grid.IsAuthority(); }

	void SetCanAffordAndSpend(TFunction<bool(const FFinish&, APlayerState*)> InFn);

	/** Validates and clamps Finish (see SanitizeFinish) before anything else. */
	EVoxelFinishPaintResult ServerPaint(const FIntVector& Cell, const FFinish& Finish, APlayerState* Player);

	/**
	 * Unpaints Cells (unpainted cells are skipped) and frees the palette slots of finishes that no block and no
	 * blueprint uses any more. Nothing is refunded. Returns how many cells were unpainted; 0 when not the authority.
	 */
	int32 ServerClearCells(const TArray<FIntVector>& Cells);

	/**
	 * Effect ids a client may paint. Nothing is allowed until registered; INDEX_NONE (no effect) always is.
	 * AVoxelFinishVolume registers the built-in sparkle id and the effect_index of every accepted starter graph.
	 */
	void AllowEffectId(int32 EffectId);
	void SetAllowedEffectIds(const TSet<int32>& InIds);
	bool IsEffectIdAllowed(int32 EffectId) const;

	/**
	 * Client finishes are untrusted. Rejects non-finite colours or intensity, a PatternId that is not 0-15 or 255,
	 * and an EffectId that is neither INDEX_NONE nor allowed. Clamps colours to 0..1 and intensity to 0..8.
	 */
	bool SanitizeFinish(const FFinish& In, FFinish& Out) const;

	/** Blueprint keeps the full FFinish. This re-resolves the palette id and counts one blueprint ref. */
	uint16 RestoreBlueprintFinish(const FFinish& Stored);
	void ReleaseBlueprint(uint16 FinishId);

	int32 CollectGarbage();

	FVoxelFinishSaveData ToSaveData() const;
	/**
	 * Rebuilds the palette and grid from a save (sparse version 2 or dense version 0).
	 * A saved slot whose id does not fit this palette (e.g. id 4096 of a version 0 save) is re-added under a free id
	 * and its cells are remapped to it; if the palette is full those cells load unpainted. Any other cell whose id
	 * is not in the restored palette also loads unpainted.
	 */
	void RestoreFromSave(const FVoxelFinishSaveData& Data);

	void ExportChunkDelta(const FIntVector& ChunkCoord, TArray<TPair<uint16, uint16>>& OutLocalAndId) const;
	void ApplyReplicatedPaletteSlot(uint16 FinishId, const FFinish& Finish, int32 BlueprintRefs);
	void ApplyReplicatedPaletteRemoval(uint16 FinishId);
	void ApplyReplicatedChunk(const FIntVector& ChunkCoord, const TArray<TPair<uint16, uint16>>& LocalAndId);

	FVoxelFinishPalette& GetPalette() { return Palette; }
	const FVoxelFinishPalette& GetPalette() const { return Palette; }
	FVoxelFinishGrid& GetGrid() { return Grid; }
	const FVoxelFinishGrid& GetGrid() const { return Grid; }

	IVoxelFinishMeshSource& GetMeshSource() { return Grid; }
	const IVoxelFinishMeshSource& GetMeshSource() const { return Grid; }

private:
	bool WouldExceedEffectCaps(const FIntVector& Cell, const FFinish& Finish) const;
	/** Distinct effect-bearing ids among Uses (id -> painted block count). */
	int32 CountEffectFinishes(const TMap<uint16, int32>* Uses) const;

	FVoxelFinishPalette Palette;
	FVoxelFinishGrid Grid;
	TFunction<bool(const FFinish&, APlayerState*)> CanAffordAndSpend;
	TArray<FFinish> BlueprintFinishes;
	TSet<int32> AllowedEffectIds;
};
