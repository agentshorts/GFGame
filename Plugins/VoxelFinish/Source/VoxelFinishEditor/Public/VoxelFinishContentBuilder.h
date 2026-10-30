#pragma once

#include "CoreMinimal.h"

/**
 * Creates the shared lookup texture, atlases, and the two master materials. No per-block material instances.
 * Saves .uasset files. Run it from the editor console with VoxelFinish.BuildContent; tests only read the assets.
 */
class VOXELFINISHEDITOR_API FVoxelFinishContentBuilder
{
public:
	/** bRebuildMaterials regenerates the two masters even when they already exist (overwrites M_FinishOpaque / M_FinishEmissive). */
	static bool EnsureAssets(FString& OutError, bool bRebuildMaterials = false);
};
