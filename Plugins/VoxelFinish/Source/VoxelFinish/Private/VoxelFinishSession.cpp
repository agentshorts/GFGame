#include "VoxelFinishSession.h"

FVoxelFinishSession::FVoxelFinishSession(bool bInAuthority)
	: Grid(bInAuthority)
{
}

void FVoxelFinishSession::SetAuthority(bool bInAuthority)
{
	Grid.SetAuthority(bInAuthority);
}

void FVoxelFinishSession::SetCanAffordAndSpend(TFunction<bool(const FFinish&, APlayerState*)> InFn)
{
	CanAffordAndSpend = MoveTemp(InFn);
}

void FVoxelFinishSession::AllowEffectId(int32 EffectId)
{
	if (EffectId != INDEX_NONE)
	{
		AllowedEffectIds.Add(EffectId);
	}
}

void FVoxelFinishSession::SetAllowedEffectIds(const TSet<int32>& InIds)
{
	AllowedEffectIds = InIds;
	AllowedEffectIds.Remove(INDEX_NONE);
}

bool FVoxelFinishSession::IsEffectIdAllowed(int32 EffectId) const
{
	return EffectId == INDEX_NONE || AllowedEffectIds.Contains(EffectId);
}

namespace VoxelFinishSessionPrivate
{
	static bool IsFiniteColor(const FLinearColor& Color)
	{
		return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
	}

	static FLinearColor ClampColor(const FLinearColor& Color)
	{
		return FLinearColor(FMath::Clamp(Color.R, 0.f, 1.f), FMath::Clamp(Color.G, 0.f, 1.f), FMath::Clamp(Color.B, 0.f, 1.f), FMath::Clamp(Color.A, 0.f, 1.f));
	}
}

bool FVoxelFinishSession::SanitizeFinish(const FFinish& In, FFinish& Out) const
{
	using namespace VoxelFinishSessionPrivate;
	if (!IsFiniteColor(In.Tint) || !IsFiniteColor(In.ColorA) || !IsFiniteColor(In.ColorB) || !FMath::IsFinite(In.Intensity))
	{
		return false;
	}
	const int32 PatternCount = VoxelFinish::PatternAtlasGrid * VoxelFinish::PatternAtlasGrid;
	if (In.PatternId != VoxelFinish::SolidPatternId && (In.PatternId < 0 || In.PatternId >= PatternCount))
	{
		return false;
	}
	if (!IsEffectIdAllowed(In.EffectId))
	{
		return false;
	}
	Out = In;
	Out.Tint = ClampColor(In.Tint);
	Out.ColorA = ClampColor(In.ColorA);
	Out.ColorB = ClampColor(In.ColorB);
	Out.Intensity = FMath::Clamp(In.Intensity, 0.f, 8.f);
	return true;
}

int32 FVoxelFinishSession::CountEffectFinishes(const TMap<uint16, int32>* Uses) const
{
	int32 Count = 0;
	if (Uses)
	{
		for (const TPair<uint16, int32>& Use : *Uses)
		{
			const FFinish* Finish = Palette.Find(Use.Key);
			if (Use.Value > 0 && Finish && Finish->HasEffect())
			{
				++Count;
			}
		}
	}
	return Count;
}

bool FVoxelFinishSession::WouldExceedEffectCaps(const FIntVector& Cell, const FFinish& Finish) const
{
	if (!Finish.HasEffect())
	{
		return false;
	}
	// Uses the grid's cached per-id counts: O(distinct ids in the chunk) + O(distinct ids in the world), no block walk.
	const FIntVector Chunk = VoxelFinish::CellToChunk(Cell);
	const uint16 Prospective = Palette.FindExisting(Finish);
	if (Prospective != VoxelFinish::UnpaintedId && Grid.GetChunkUseCount(Chunk, Prospective) > 0)
	{
		// Already counted in this chunk, and therefore in the world.
		return false;
	}

	const uint16 ExistingId = Grid.GetFinishId(Cell);
	const FFinish* Existing = Palette.Find(ExistingId);
	const bool bExistingIsEffect = Existing && Existing->HasEffect();

	int32 InChunk = CountEffectFinishes(Grid.GetChunkUseCounts(Chunk));
	if (bExistingIsEffect && Grid.GetChunkUseCount(Chunk, ExistingId) <= 1)
	{
		// Overwriting the last block of an effect finish in this chunk frees its slot.
		--InChunk;
	}
	if (InChunk >= VoxelFinish::MaxEffectFinishesPerChunk)
	{
		return true;
	}

	const bool bAlreadyGlobal = Prospective != VoxelFinish::UnpaintedId && Grid.GetUseCount(Prospective) > 0;
	if (!bAlreadyGlobal)
	{
		int32 Global = CountEffectFinishes(&Grid.GetUseCounts());
		if (bExistingIsEffect && Grid.GetUseCount(ExistingId) <= 1)
		{
			--Global;
		}
		if (Global >= VoxelFinish::MaxEffectFinishesGlobal)
		{
			return true;
		}
	}
	return false;
}

EVoxelFinishPaintResult FVoxelFinishSession::ServerPaint(const FIntVector& Cell, const FFinish& Requested, APlayerState* Player)
{
	if (!Grid.IsAuthority())
	{
		return EVoxelFinishPaintResult::RejectedNotAuthority;
	}
	FFinish Finish;
	if (!SanitizeFinish(Requested, Finish))
	{
		return EVoxelFinishPaintResult::RejectedInvalidFinish;
	}
	const uint16 ExistingId = Palette.FindExisting(Finish);
	if (ExistingId != VoxelFinish::UnpaintedId && Grid.GetFinishId(Cell) == ExistingId)
	{
		return EVoxelFinishPaintResult::AlreadyPainted;
	}
	if (WouldExceedEffectCaps(Cell, Finish))
	{
		return EVoxelFinishPaintResult::RejectedEffectCap;
	}

	const bool bExists = ExistingId != VoxelFinish::UnpaintedId;
	if (!bExists && Palette.NumOccupied() >= VoxelFinish::MaxPaletteEntries)
	{
		return EVoxelFinishPaintResult::RejectedPaletteFull;
	}
	// Fail closed: with no hook bound nobody can pay, so nothing is painted for free.
	if (!CanAffordAndSpend || !CanAffordAndSpend(Finish, Player))
	{
		return EVoxelFinishPaintResult::RejectedCannotAfford;
	}

	const uint16 Id = Palette.FindOrAdd(Finish);
	if (Id == VoxelFinish::UnpaintedId)
	{
		return EVoxelFinishPaintResult::RejectedPaletteFull;
	}
	Grid.SetFinishId(Cell, Id);
	Grid.MarkChunkDirty(VoxelFinish::CellToChunk(Cell));
	return EVoxelFinishPaintResult::Applied;
}

int32 FVoxelFinishSession::ServerClearCells(const TArray<FIntVector>& Cells)
{
	if (!Grid.IsAuthority())
	{
		return 0;
	}
	int32 Cleared = 0;
	TSet<uint16> Released;
	for (const FIntVector& Cell : Cells)
	{
		const uint16 Id = Grid.GetFinishId(Cell);
		if (Id == VoxelFinish::UnpaintedId)
		{
			continue;
		}
		Grid.SetFinishId(Cell, VoxelFinish::UnpaintedId);
		Grid.MarkChunkDirty(VoxelFinish::CellToChunk(Cell));
		Released.Add(Id);
		++Cleared;
	}
	if (Released.Num() > 0)
	{
		// Only the finishes these cells held: other unused slots stay until the game collects garbage itself.
		Palette.CollectGarbage(Grid.GetUseCounts(), &Released);
	}
	return Cleared;
}

uint16 FVoxelFinishSession::RestoreBlueprintFinish(const FFinish& Stored)
{
	if (!Grid.IsAuthority())
	{
		return VoxelFinish::UnpaintedId;
	}
	const uint16 Id = Palette.FindOrAdd(Stored);
	if (Id == VoxelFinish::UnpaintedId)
	{
		return VoxelFinish::UnpaintedId;
	}
	Palette.AddBlueprintRef(Id);
	BlueprintFinishes.Add(Stored);
	return Id;
}

void FVoxelFinishSession::ReleaseBlueprint(uint16 FinishId)
{
	Palette.ReleaseBlueprintRef(FinishId);
}

int32 FVoxelFinishSession::CollectGarbage()
{
	TMap<uint16, int32> Uses;
	Grid.CountBlockUses(Uses);
	return Palette.CollectGarbage(Uses);
}

FVoxelFinishSaveData FVoxelFinishSession::ToSaveData() const
{
	FVoxelFinishSaveData Data;
	Data.Version = FVoxelFinishSaveData::SparseVersion;
	Palette.ExportSparse(Data.SlotIds, Data.SlotFinishes, Data.SlotBlueprintRefs);
	Grid.ForEachPaintedBlock([&](FIntVector Cell, uint16 FinishId)
	{
		Data.Cells.Add(Cell);
		Data.CellFinishIds.Add(FinishId);
	});
	Data.BlueprintFinishes = BlueprintFinishes;
	return Data;
}

void FVoxelFinishSession::RestoreFromSave(const FVoxelFinishSaveData& Data)
{
	Palette.Reset();
	Grid.Reset();
	BlueprintFinishes.Reset();

	// Saved slots whose id has no place in this palette (id 0 or past MaxPaletteEntries, e.g. slot 4096 of a
	// version 0 save). Their finishes are re-added under a free id once every in-range slot is placed.
	struct FOrphanSlot
	{
		int32 SavedId = 0;
		FFinish Finish;
		int32 BlueprintRefs = 0;
	};
	TArray<FOrphanSlot> Orphans;
	auto PlaceSlot = [this, &Orphans](int32 SavedId, const FFinish& Finish, int32 BlueprintRefs)
	{
		if (SavedId > VoxelFinish::UnpaintedId && SavedId <= VoxelFinish::MaxPaletteEntries)
		{
			Palette.ApplySlot(static_cast<uint16>(SavedId), Finish, BlueprintRefs);
		}
		else if (SavedId > VoxelFinish::UnpaintedId)
		{
			Orphans.Add({SavedId, Finish, BlueprintRefs});
		}
	};

	if (Data.Version >= FVoxelFinishSaveData::SparseVersion)
	{
		const int32 Slots = FMath::Min3(Data.SlotIds.Num(), Data.SlotFinishes.Num(), Data.SlotBlueprintRefs.Num());
		for (int32 Index = 0; Index < Slots; ++Index)
		{
			PlaceSlot(Data.SlotIds[Index], Data.SlotFinishes[Index], Data.SlotBlueprintRefs[Index]);
		}
	}
	else
	{
		const int32 Slots = FMath::Min3(Data.Palette.Num(), Data.Occupied.Num(), Data.BlueprintRefs.Num());
		for (int32 Id = 1; Id < Slots; ++Id)
		{
			if (Data.Occupied[Id] != 0)
			{
				PlaceSlot(Id, Data.Palette[Id], Data.BlueprintRefs[Id]);
			}
		}
	}

	// Saved id -> id in this palette. 0 means the palette is full and those cells load unpainted.
	TMap<int32, uint16> Remap;
	for (const FOrphanSlot& Orphan : Orphans)
	{
		const uint16 NewId = Palette.FindOrAdd(Orphan.Finish);
		if (NewId != VoxelFinish::UnpaintedId && Orphan.BlueprintRefs > 0)
		{
			Palette.ApplySlot(NewId, Orphan.Finish, Palette.GetBlueprintRefs(NewId) + Orphan.BlueprintRefs);
		}
		Remap.Add(Orphan.SavedId, NewId);
	}

	// Every cell must point at a restored palette slot: remap orphaned ids, clear anything else unknown.
	const int32 Cells = FMath::Min(Data.Cells.Num(), Data.CellFinishIds.Num());
	for (int32 Index = 0; Index < Cells; ++Index)
	{
		uint16 FinishId = Data.CellFinishIds[Index];
		if (FinishId == VoxelFinish::UnpaintedId)
		{
			continue;
		}
		if (!Palette.Find(FinishId))
		{
			const uint16* Mapped = Remap.Find(FinishId);
			FinishId = Mapped ? *Mapped : VoxelFinish::UnpaintedId;
		}
		if (FinishId != VoxelFinish::UnpaintedId)
		{
			Grid.WriteReplicated(Data.Cells[Index], FinishId);
		}
	}
	// Blueprints store full finishes. Re-resolve ids against the restored palette (or add them).
	for (const FFinish& Stored : Data.BlueprintFinishes)
	{
		const uint16 Id = Palette.FindOrAdd(Stored);
		if (Id != VoxelFinish::UnpaintedId)
		{
			BlueprintFinishes.Add(Stored);
		}
	}
	Grid.ClearDirty();
	TArray<uint16> IgnoredUpserts;
	TArray<uint16> IgnoredRemoved;
	Palette.ConsumeChanges(IgnoredUpserts, IgnoredRemoved);
}

void FVoxelFinishSession::ExportChunkDelta(const FIntVector& ChunkCoord, TArray<TPair<uint16, uint16>>& OutLocalAndId) const
{
	Grid.ExportChunk(ChunkCoord, OutLocalAndId);
}

void FVoxelFinishSession::ApplyReplicatedPaletteSlot(uint16 FinishId, const FFinish& Finish, int32 BlueprintRefs)
{
	Palette.ApplySlot(FinishId, Finish, BlueprintRefs);
}

void FVoxelFinishSession::ApplyReplicatedPaletteRemoval(uint16 FinishId)
{
	Palette.RemoveSlot(FinishId);
}

void FVoxelFinishSession::ApplyReplicatedChunk(const FIntVector& ChunkCoord, const TArray<TPair<uint16, uint16>>& LocalAndId)
{
	Grid.ClearChunk(ChunkCoord);
	for (const TPair<uint16, uint16>& Cell : LocalAndId)
	{
		Grid.WriteReplicated(VoxelFinish::ChunkLocalToCell(ChunkCoord, Cell.Key), Cell.Value);
	}
	Grid.MarkChunkDirty(ChunkCoord);
}
