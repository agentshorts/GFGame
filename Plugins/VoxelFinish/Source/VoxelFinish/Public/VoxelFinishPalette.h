#pragma once

#include "CoreMinimal.h"
#include "VoxelFinish.h"
#include "VoxelFinishTypes.h"

/** Deduped per-world palette. Id 0 is unpainted. Cap is 4095 occupied finishes (ids 1..4095). */
class VOXELFINISH_API FVoxelFinishPalette
{
public:
	FVoxelFinishPalette();

	int32 NumOccupied() const { return OccupiedCount; }
	const FFinish* Find(uint16 FinishId) const;
	int32 GetBlueprintRefs(uint16 FinishId) const;

	/** Returns an existing id or a new one. 0 means the palette is full. */
	uint16 FindOrAdd(const FFinish& Finish, bool* bOutAdded = nullptr);

	/** Hash lookup without adding. 0 when the finish is not in the palette. */
	uint16 FindExisting(const FFinish& Finish) const;

	void AddBlueprintRef(uint16 FinishId);
	void ReleaseBlueprintRef(uint16 FinishId);

	/**
	 * Drops finishes with no painted blocks and no blueprint refs.
	 * BlockUses is the count of cells currently storing each id (from the mesh source).
	 * Only the ids in OnlyIds are considered when it is given.
	 * Returns how many slots were freed.
	 */
	int32 CollectGarbage(const TMap<uint16, int32>& BlockUses, const TSet<uint16>* OnlyIds = nullptr);

	/** Places a replicated slot at a server-assigned id. */
	void ApplySlot(uint16 FinishId, const FFinish& Finish, int32 BlueprintRefs);
	void RemoveSlot(uint16 FinishId);

	void Reset();
	/** Occupied slots only, in id order. */
	void ExportSparse(TArray<uint16>& OutIds, TArray<FFinish>& OutFinishes, TArray<int32>& OutBlueprintRefs) const;

	const TArray<uint16>& GetDirtyIds() const { return DirtyIds; }
	const TArray<uint16>& GetRemovedIds() const { return RemovedIds; }
	void ConsumeChanges(TArray<uint16>& OutUpserts, TArray<uint16>& OutRemoved);

private:
	struct FEntry
	{
		FFinish Finish;
		uint32 Hash = 0;
		int32 BlueprintRefs = 0;
		bool bOccupied = false;
	};

	uint16 AllocateId();
	void IndexEntry(uint16 Id);
	void UnindexEntry(uint16 Id);

	TArray<FEntry> Entries;
	TMap<uint32, TArray<uint16>> ByHash;
	int32 OccupiedCount = 0;
	TArray<uint16> DirtyIds;
	TArray<uint16> RemovedIds;
};
