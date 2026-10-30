#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GFVoxelReplication.generated.h"

class AGFVoxelWorld;

/**
 * Voxel replication (PLAN.md section 4.1): server-authoritative, per chunk.
 *  - FGFChunkBaseArray: one snapshot per chunk (palette + RLE) at BaseRevision. This is the join-in-progress
 *    snapshot: a late joiner receives every base, then the deltas recorded since.
 *  - FGFChunkDeltaArray: per chunk, the edits made since its base. The server re-bases (compacts) a chunk
 *    after CompactThreshold edits, so the delta lists stay short.
 * Both are fast arrays, so only changed chunks are sent and packet loss just delays to the latest state.
 * Chunk revision = BaseRevision + number of deltas; it only grows.
 */
USTRUCT()
struct GFVOXEL_API FGFVoxelEdit
{
	GENERATED_BODY()

	UPROPERTY() uint16 LocalIndex = 0;
	/** None = air (removed). */
	UPROPERTY() FName BlockId;
};

USTRUCT()
struct GFVOXEL_API FGFChunkBaseItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY() FIntVector Coord = FIntVector::ZeroValue;
	UPROPERTY() int32 BaseRevision = 0;
	UPROPERTY() TArray<FName> Palette;
	UPROPERTY() TArray<uint16> RLE;

	void PostReplicatedAdd(const struct FGFChunkBaseArray& InArray);
	void PostReplicatedChange(const struct FGFChunkBaseArray& InArray);
};

USTRUCT()
struct GFVOXEL_API FGFChunkBaseArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY() TArray<FGFChunkBaseItem> Items;
	UPROPERTY(NotReplicated) TObjectPtr<AGFVoxelWorld> Owner;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FGFChunkBaseItem, FGFChunkBaseArray>(Items, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FGFChunkBaseArray> : public TStructOpsTypeTraitsBase2<FGFChunkBaseArray>
{
	enum { WithNetDeltaSerializer = true };
};

USTRUCT()
struct GFVOXEL_API FGFChunkDeltaItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY() FIntVector Coord = FIntVector::ZeroValue;
	/** The base these edits apply on top of. */
	UPROPERTY() int32 BaseRevision = 0;
	UPROPERTY() TArray<FGFVoxelEdit> Edits;

	void PostReplicatedAdd(const struct FGFChunkDeltaArray& InArray);
	void PostReplicatedChange(const struct FGFChunkDeltaArray& InArray);
};

USTRUCT()
struct GFVOXEL_API FGFChunkDeltaArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY() TArray<FGFChunkDeltaItem> Items;
	UPROPERTY(NotReplicated) TObjectPtr<AGFVoxelWorld> Owner;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FGFChunkDeltaItem, FGFChunkDeltaArray>(Items, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FGFChunkDeltaArray> : public TStructOpsTypeTraitsBase2<FGFChunkDeltaArray>
{
	enum { WithNetDeltaSerializer = true };
};
