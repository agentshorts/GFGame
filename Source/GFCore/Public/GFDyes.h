#pragma once

// Dyes (PLAN.md: "Dyes (from Flowers) recolor outfits and blocks"). One material item per colour family in DT_Items;
// anything that recolours (fashion outfits, voxel block finishes) charges the same items through the ledger.
#include "CoreMinimal.h"
#include "GFItemTypes.h"

struct GFCORE_API FGFDyes
{
	/** Outfits: dye items per dyed piece. */
	static constexpr int32 PerOutfitPiece = 1;
	/** Blocks: one dye item covers this many blocks. */
	static constexpr int32 BlocksPerDye = 8;
	/** Below this saturation a colour is a neutral (white, grey, black) and takes ChalkDye. */
	static constexpr float NeutralSaturation = 0.15f;

	/** Every dye item id (DT_Items rows), in palette order: RoseDye, SunDye, FernDye, SkyDye, PlumDye, ChalkDye. */
	static const TArray<FName>& AllDyeItems();
	static bool IsDyeItem(FName ItemId);
	/** Which dye a colour takes (by hue, or ChalkDye for neutrals). */
	static FName DyeForColor(const FLinearColor& Color);
	/** What dyeing one outfit piece this colour costs. */
	static TArray<FGFItemStack> CostForOutfitPiece(const FLinearColor& Color);
	/** What dyeing NumBlocks blocks this colour costs (rounded up; empty for NumBlocks <= 0). */
	static TArray<FGFItemStack> CostForBlocks(const FLinearColor& Color, int32 NumBlocks);
	/** A dye colour must be finite and inside 0..1 (alpha ignored). */
	static bool IsValidDyeColor(const FLinearColor& Color);
};
