#pragma once

#include "CoreMinimal.h"
#include "VoxelFinish.h"
#include "VoxelFinishTypes.h"

/** CPU bake of the palette lookup texture. One column per FinishId, four rows, nearest sampled. */
class VOXELFINISH_API FVoxelFinishLookup
{
public:
	static void InitPixels(TArray<FColor>& OutPixels);
	static void WriteFinish(TArray<FColor>& Pixels, uint16 FinishId, const FFinish& Finish);
	/** Zeroes one column (a removed palette slot). */
	static void ClearFinish(TArray<FColor>& Pixels, uint16 FinishId);
	static FColor Read(const TArray<FColor>& Pixels, uint16 FinishId, int32 Row);

	/** Copies a whole baked lookup into a texture's first mip (used once, when the transient texture is created). */
	static void Upload(class UTexture2D* Texture, const TArray<FColor>& Pixels);

	/** Material texture parameter the masters sample the lookup through. */
	static const FName ParameterName;

	/** Row 0 ColorA (A = use pattern), row 1 ColorB (A = glow), row 2 Tint (A = intensity/8), row 3 sparkle/pattern. */
	static constexpr int32 RowColorA = 0;
	static constexpr int32 RowColorB = 1;
	static constexpr int32 RowTint = 2;
	static constexpr int32 RowMeta = 3;
};
