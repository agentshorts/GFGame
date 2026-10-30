#pragma once

#include "CoreMinimal.h"
#include "VoxelFinishTypes.generated.h"

/**
 * One baked finish. Same canonical fields always hash equal and share one palette id.
 * Colors are quantized to 8-bit and intensity to thousandths of a unit over 0..8.
 */
USTRUCT(BlueprintType)
struct VOXELFINISH_API FFinish
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish")
	FLinearColor Tint = FLinearColor::White;

	/** 0-15 pattern atlas cell, or 255 for a solid colour (no pattern). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish")
	int32 PatternId = 255;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish")
	FLinearColor ColorA = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish")
	FLinearColor ColorB = FLinearColor::White;

	/** INDEX_NONE means paint with no effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish")
	int32 EffectId = INDEX_NONE;

	/** Effect strength, clamped to 0..8 when hashed. Materials are spent by the game, not by this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish")
	float Intensity = 1.f;

	/** Baked glow flag. Read from the lookup texture; never evaluated live per block. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish")
	bool bGlow = false;

	bool HasEffect() const { return EffectId != INDEX_NONE && Intensity > 0.f; }

	uint32 GetCanonicalHash() const;
	bool CanonicallyEquals(const FFinish& Other) const;
};

/**
 * Saved world paint. Blueprints also keep the full FFinish values and re-resolve ids on restore.
 * Version 2 stores only occupied palette slots (SlotIds / SlotFinishes / SlotBlueprintRefs).
 * Version 0 saves used the dense Palette / Occupied / BlueprintRefs arrays (one entry per id); they still load.
 */
USTRUCT()
struct VOXELFINISH_API FVoxelFinishSaveData
{
	GENERATED_BODY()

	static constexpr int32 SparseVersion = 2;

	UPROPERTY()
	int32 Version = 0;

	UPROPERTY()
	TArray<uint16> SlotIds;

	UPROPERTY()
	TArray<FFinish> SlotFinishes;

	UPROPERTY()
	TArray<int32> SlotBlueprintRefs;

	/** Legacy dense layout (version 0). Empty in new saves. */
	UPROPERTY()
	TArray<FFinish> Palette;

	UPROPERTY()
	TArray<uint8> Occupied;

	UPROPERTY()
	TArray<int32> BlueprintRefs;

	UPROPERTY()
	TArray<FIntVector> Cells;

	UPROPERTY()
	TArray<uint16> CellFinishIds;

	/** Full finish values held by blueprints. Restored by re-resolving, not by trusting a stored id. */
	UPROPERTY()
	TArray<FFinish> BlueprintFinishes;
};
