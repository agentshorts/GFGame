#include "VoxelFinishPalette.h"

FVoxelFinishPalette::FVoxelFinishPalette()
{
	Entries.SetNum(VoxelFinish::MaxPaletteEntries + 1);
}

const FFinish* FVoxelFinishPalette::Find(uint16 FinishId) const
{
	if (!Entries.IsValidIndex(FinishId) || !Entries[FinishId].bOccupied)
	{
		return nullptr;
	}
	return &Entries[FinishId].Finish;
}

int32 FVoxelFinishPalette::GetBlueprintRefs(uint16 FinishId) const
{
	if (!Entries.IsValidIndex(FinishId) || !Entries[FinishId].bOccupied)
	{
		return 0;
	}
	return Entries[FinishId].BlueprintRefs;
}

uint16 FVoxelFinishPalette::AllocateId()
{
	for (int32 Id = 1; Id <= VoxelFinish::MaxPaletteEntries; ++Id)
	{
		if (!Entries[Id].bOccupied)
		{
			return static_cast<uint16>(Id);
		}
	}
	return VoxelFinish::UnpaintedId;
}

void FVoxelFinishPalette::IndexEntry(uint16 Id)
{
	ByHash.FindOrAdd(Entries[Id].Hash).AddUnique(Id);
}

void FVoxelFinishPalette::UnindexEntry(uint16 Id)
{
	const uint32 Hash = Entries[Id].Hash;
	if (TArray<uint16>* Bucket = ByHash.Find(Hash))
	{
		Bucket->Remove(Id);
		if (Bucket->Num() == 0)
		{
			ByHash.Remove(Hash);
		}
	}
}

uint16 FVoxelFinishPalette::FindExisting(const FFinish& Finish) const
{
	if (const TArray<uint16>* Bucket = ByHash.Find(Finish.GetCanonicalHash()))
	{
		for (uint16 Id : *Bucket)
		{
			if (Entries[Id].bOccupied && Entries[Id].Finish.CanonicallyEquals(Finish))
			{
				return Id;
			}
		}
	}
	return VoxelFinish::UnpaintedId;
}

uint16 FVoxelFinishPalette::FindOrAdd(const FFinish& Finish, bool* bOutAdded)
{
	if (bOutAdded)
	{
		*bOutAdded = false;
	}
	const uint32 Hash = Finish.GetCanonicalHash();
	if (const uint16 Existing = FindExisting(Finish))
	{
		return Existing;
	}
	if (OccupiedCount >= VoxelFinish::MaxPaletteEntries)
	{
		return VoxelFinish::UnpaintedId;
	}
	const uint16 Id = AllocateId();
	if (Id == VoxelFinish::UnpaintedId)
	{
		return VoxelFinish::UnpaintedId;
	}
	FEntry& Entry = Entries[Id];
	Entry.Finish = Finish;
	Entry.Hash = Hash;
	Entry.BlueprintRefs = 0;
	Entry.bOccupied = true;
	++OccupiedCount;
	IndexEntry(Id);
	DirtyIds.AddUnique(Id);
	if (bOutAdded)
	{
		*bOutAdded = true;
	}
	return Id;
}

void FVoxelFinishPalette::AddBlueprintRef(uint16 FinishId)
{
	if (!Entries.IsValidIndex(FinishId) || !Entries[FinishId].bOccupied)
	{
		return;
	}
	++Entries[FinishId].BlueprintRefs;
	DirtyIds.AddUnique(FinishId);
}

void FVoxelFinishPalette::ReleaseBlueprintRef(uint16 FinishId)
{
	if (!Entries.IsValidIndex(FinishId) || !Entries[FinishId].bOccupied)
	{
		return;
	}
	Entries[FinishId].BlueprintRefs = FMath::Max(0, Entries[FinishId].BlueprintRefs - 1);
	DirtyIds.AddUnique(FinishId);
}

int32 FVoxelFinishPalette::CollectGarbage(const TMap<uint16, int32>& BlockUses)
{
	int32 Freed = 0;
	for (int32 Id = 1; Id <= VoxelFinish::MaxPaletteEntries; ++Id)
	{
		FEntry& Entry = Entries[Id];
		if (!Entry.bOccupied)
		{
			continue;
		}
		const int32 Blocks = BlockUses.FindRef(static_cast<uint16>(Id));
		if (Blocks > 0 || Entry.BlueprintRefs > 0)
		{
			continue;
		}
		UnindexEntry(static_cast<uint16>(Id));
		Entry = FEntry();
		--OccupiedCount;
		++Freed;
		RemovedIds.AddUnique(static_cast<uint16>(Id));
		DirtyIds.Remove(static_cast<uint16>(Id));
	}
	return Freed;
}

void FVoxelFinishPalette::ApplySlot(uint16 FinishId, const FFinish& Finish, int32 BlueprintRefs)
{
	if (FinishId == VoxelFinish::UnpaintedId || FinishId > VoxelFinish::MaxPaletteEntries)
	{
		return;
	}
	FEntry& Entry = Entries[FinishId];
	if (Entry.bOccupied)
	{
		UnindexEntry(FinishId);
	}
	else
	{
		++OccupiedCount;
	}
	Entry.Finish = Finish;
	Entry.Hash = Finish.GetCanonicalHash();
	Entry.BlueprintRefs = FMath::Max(0, BlueprintRefs);
	Entry.bOccupied = true;
	IndexEntry(FinishId);
}

void FVoxelFinishPalette::RemoveSlot(uint16 FinishId)
{
	if (!Entries.IsValidIndex(FinishId) || !Entries[FinishId].bOccupied)
	{
		return;
	}
	UnindexEntry(FinishId);
	Entries[FinishId] = FEntry();
	--OccupiedCount;
}

void FVoxelFinishPalette::Reset()
{
	Entries.SetNum(0);
	Entries.SetNum(VoxelFinish::MaxPaletteEntries + 1);
	ByHash.Reset();
	OccupiedCount = 0;
	DirtyIds.Reset();
	RemovedIds.Reset();
}

void FVoxelFinishPalette::ExportSparse(TArray<uint16>& OutIds, TArray<FFinish>& OutFinishes, TArray<int32>& OutBlueprintRefs) const
{
	OutIds.Reset(OccupiedCount);
	OutFinishes.Reset(OccupiedCount);
	OutBlueprintRefs.Reset(OccupiedCount);
	for (int32 Id = 1; Id < Entries.Num(); ++Id)
	{
		if (Entries[Id].bOccupied)
		{
			OutIds.Add(static_cast<uint16>(Id));
			OutFinishes.Add(Entries[Id].Finish);
			OutBlueprintRefs.Add(Entries[Id].BlueprintRefs);
		}
	}
}

void FVoxelFinishPalette::ConsumeChanges(TArray<uint16>& OutUpserts, TArray<uint16>& OutRemoved)
{
	OutUpserts = MoveTemp(DirtyIds);
	OutRemoved = MoveTemp(RemovedIds);
	DirtyIds.Reset();
	RemovedIds.Reset();
}
