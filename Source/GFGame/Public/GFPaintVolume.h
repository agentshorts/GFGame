#pragma once

// GFGame's hookup of the VoxelFinish plugin: block paint costs dye items through the ledger, and only existing
// solid blocks of the hub's voxel world can be painted. VoxelFinish cells are AGFVoxelWorld cells (same coordinates).
#include "CoreMinimal.h"
#include "VoxelFinishVolume.h"
#include "GFPaintVolume.generated.h"

struct FFinish;
struct FGFItemStack;

/** What a finish costs to paint (PLAN.md: "Dyes (from Flowers) recolor outfits and blocks"). */
struct GFGAME_API FGFPaintCost
{
	/** Colours a painted block shows: ColorA x Tint, plus ColorB x Tint when a pattern is used. Clamped to 0..1. */
	static TArray<FLinearColor> VisibleColors(const FFinish& Finish);
	/** Dye items (one per colour family, no repeats) that painting one block with Finish uses. */
	static TArray<FName> DyesFor(const FFinish& Finish);
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
 * UVoxelFinishPainterComponent (AGFPlayerController::Painter: 600 cm, 10 paints per second).
 */
UCLASS()
class GFGAME_API AGFPaintVolume : public AVoxelFinishVolume
{
	GENERATED_BODY()

public:
	static FName BurnReason() { return TEXT("Paint.Dye"); }
	static AGFPaintVolume* Find(const UWorld* World);

	virtual void PostInitializeComponents() override;

	bool ServerSpend(const FFinish& Finish, APlayerState* Payer);
	bool ResolveTarget(const FIntVector& Cell, FVector& OutCenter) const;
	const FGFPaintCredits& GetCredits() const { return Credits; }

private:
	FGFPaintCredits Credits;
};
