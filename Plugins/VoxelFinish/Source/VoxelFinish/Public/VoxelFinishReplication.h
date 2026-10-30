#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "VoxelFinishTypes.h"
#include "VoxelFinishReplication.generated.h"

class AVoxelFinishVolume;

/** One painted cell inside a chunk delta. LocalIndex is 0..4095. */
USTRUCT()
struct VOXELFINISH_API FVoxelFinishCellRep
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 LocalIndex = 0;

	UPROPERTY()
	uint16 FinishId = 0;
};

USTRUCT()
struct VOXELFINISH_API FVoxelFinishPaletteItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 FinishId = 0;

	UPROPERTY()
	FFinish Finish;

	UPROPERTY()
	int32 BlueprintRefs = 0;

	/** Client side: the server garbage-collected this slot. Clears it from the palette and the lookup. */
	void PreReplicatedRemove(const struct FVoxelFinishPaletteArray& InArray);
	void PostReplicatedAdd(const struct FVoxelFinishPaletteArray& InArray);
	void PostReplicatedChange(const struct FVoxelFinishPaletteArray& InArray);
};

USTRUCT()
struct VOXELFINISH_API FVoxelFinishPaletteArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVoxelFinishPaletteItem> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<AVoxelFinishVolume> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FVoxelFinishPaletteItem, FVoxelFinishPaletteArray>(Items, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FVoxelFinishPaletteArray> : public TStructOpsTypeTraitsBase2<FVoxelFinishPaletteArray>
{
	enum { WithNetDeltaSerializer = true };
};

USTRUCT()
struct VOXELFINISH_API FVoxelFinishChunkItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FIntVector ChunkCoord = FIntVector::ZeroValue;

	UPROPERTY()
	int32 Revision = 0;

	UPROPERTY()
	TArray<FVoxelFinishCellRep> Cells;

	void PostReplicatedAdd(const struct FVoxelFinishChunkArray& InArray);
	void PostReplicatedChange(const struct FVoxelFinishChunkArray& InArray);
};

/** One entry per dirty chunk. MarkItemDirty is called only for the chunk a paint touched. */
USTRUCT()
struct VOXELFINISH_API FVoxelFinishChunkArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVoxelFinishChunkItem> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<AVoxelFinishVolume> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FVoxelFinishChunkItem, FVoxelFinishChunkArray>(Items, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FVoxelFinishChunkArray> : public TStructOpsTypeTraitsBase2<FVoxelFinishChunkArray>
{
	enum { WithNetDeltaSerializer = true };
};
