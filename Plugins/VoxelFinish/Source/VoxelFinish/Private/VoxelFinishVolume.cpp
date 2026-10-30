#include "VoxelFinishVolume.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "TextureResource.h"
#include "Net/UnrealNetwork.h"
#include "VoxelFinishEffectGraph.h"
#include "VoxelFinishLookup.h"

void FVoxelFinishPaletteItem::PreReplicatedRemove(const FVoxelFinishPaletteArray& InArray)
{
	if (InArray.Owner)
	{
		InArray.Owner->NotifyPaletteItemRemoved(*this, InArray);
	}
}

void FVoxelFinishPaletteItem::PostReplicatedAdd(const FVoxelFinishPaletteArray& InArray)
{
	if (InArray.Owner)
	{
		InArray.Owner->NotifyPaletteItem(*this);
	}
}

void FVoxelFinishPaletteItem::PostReplicatedChange(const FVoxelFinishPaletteArray& InArray)
{
	if (InArray.Owner)
	{
		InArray.Owner->NotifyPaletteItem(*this);
	}
}

void FVoxelFinishChunkItem::PostReplicatedAdd(const FVoxelFinishChunkArray& InArray)
{
	if (InArray.Owner)
	{
		InArray.Owner->NotifyChunkItem(*this);
	}
}

void FVoxelFinishChunkItem::PostReplicatedChange(const FVoxelFinishChunkArray& InArray)
{
	if (InArray.Owner)
	{
		InArray.Owner->NotifyChunkItem(*this);
	}
}

AVoxelFinishVolume::AVoxelFinishVolume()
	: Session(true)
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.f);
	PaletteRep.Owner = this;
	ChunkRep.Owner = this;
}

void AVoxelFinishVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVoxelFinishVolume, PaletteRep);
	DOREPLIFETIME(AVoxelFinishVolume, ChunkRep);
}

void AVoxelFinishVolume::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	PaletteRep.Owner = this;
	ChunkRep.Owner = this;
	Session.SetAuthority(HasAuthority());
	if (HasAuthority())
	{
		RegisterAllowedEffects();
	}
}

void AVoxelFinishVolume::RegisterAllowedEffects()
{
	TSet<int32> Allowed;
	Allowed.Add(VoxelFinish::SparkleEffectId);
	FEffectGraphRegistry Registry;
	Registry.LoadStarterGraphs();
	Registry.CollectAcceptedEffectIndexes(Allowed);
	Session.SetAllowedEffectIds(Allowed);
}

void AVoxelFinishVolume::AllowEffectId(int32 EffectId)
{
	Session.AllowEffectId(EffectId);
}

void AVoxelFinishVolume::SetCanAffordAndSpend(TFunction<bool(const FFinish&, APlayerState*)> InFn)
{
	Session.SetCanAffordAndSpend(MoveTemp(InFn));
}

void AVoxelFinishVolume::SetPaintTargetResolver(TFunction<bool(const FIntVector&, FVector&)> InFn)
{
	PaintTargetResolver = MoveTemp(InFn);
}

bool AVoxelFinishVolume::ResolvePaintTarget(const FIntVector& Cell, FVector& OutCenter) const
{
	// Fail closed: without a resolver there is no way to know the cell is a real block.
	return PaintTargetResolver && PaintTargetResolver(Cell, OutCenter);
}

EVoxelFinishPaintResult AVoxelFinishVolume::AuthorityPaint(const FIntVector& Cell, const FFinish& Finish, APlayerState* Payer)
{
	if (!HasAuthority())
	{
		return EVoxelFinishPaintResult::RejectedNotAuthority;
	}
	Session.SetAuthority(true);
	const EVoxelFinishPaintResult Result = Session.ServerPaint(Cell, Finish, Payer);
	if (Result == EVoxelFinishPaintResult::Applied)
	{
		PushSessionChanges();
	}
	return Result;
}

int32 AVoxelFinishVolume::AuthorityClearCells(const TArray<FIntVector>& Cells)
{
	if (!HasAuthority() || Cells.Num() == 0)
	{
		return 0;
	}
	Session.SetAuthority(true);
	const int32 Cleared = Session.ServerClearCells(Cells);
	if (Cleared > 0)
	{
		PushSessionChanges();
	}
	return Cleared;
}

int32 AVoxelFinishVolume::AuthorityClearUnresolvable()
{
	if (!HasAuthority())
	{
		return 0;
	}
	TArray<FIntVector> Refused;
	Session.GetGrid().ForEachPaintedBlock([this, &Refused](FIntVector Cell, uint16)
	{
		FVector Center;
		if (!ResolvePaintTarget(Cell, Center))
		{
			Refused.Add(Cell);
		}
	});
	return AuthorityClearCells(Refused);
}

void AVoxelFinishVolume::AuthorityRestore(const FVoxelFinishSaveData& Data)
{
	if (!HasAuthority())
	{
		return;
	}
	Session.SetAuthority(true);
	// Everything painted before and after the restore is re-sent and reported to the display hooks.
	TSet<FIntVector> Chunks;
	TSet<uint16> Ids;
	auto Collect = [this, &Chunks, &Ids]()
	{
		Session.GetGrid().ForEachPaintedBlock([&Chunks](FIntVector Cell, uint16)
		{
			Chunks.Add(VoxelFinish::CellToChunk(Cell));
		});
		TArray<uint16> SlotIds;
		TArray<FFinish> SlotFinishes;
		TArray<int32> SlotRefs;
		Session.GetPalette().ExportSparse(SlotIds, SlotFinishes, SlotRefs);
		Ids.Append(SlotIds);
	};
	Collect();
	Session.RestoreFromSave(Data);
	Collect();

	for (uint16 Id : Ids)
	{
		if (Session.GetPalette().Find(Id))
		{
			UpsertPaletteItem(Id);
		}
		else
		{
			RemovePaletteItem(Id);
		}
		MarkLookupDirty(Id);
	}
	for (const FIntVector& Chunk : Chunks)
	{
		UpsertChunk(Chunk);
	}
	FlushLookup();
	for (uint16 Id : Ids)
	{
		OnPaletteSlotChanged(Id);
	}
	if (Chunks.Num() > 0)
	{
		OnPaintChunksChanged(Chunks.Array());
	}
	AuthorityClearUnresolvable();
}

void AVoxelFinishVolume::PushSessionChanges()
{
	TArray<uint16> Upserts;
	TArray<uint16> Removed;
	Session.GetPalette().ConsumeChanges(Upserts, Removed);
	for (uint16 Id : Removed)
	{
		RemovePaletteItem(Id);
		MarkLookupDirty(Id);
	}
	for (uint16 Id : Upserts)
	{
		UpsertPaletteItem(Id);
		MarkLookupDirty(Id);
	}
	const TArray<FIntVector> Chunks = Session.GetGrid().ConsumeDirtyChunks();
	for (const FIntVector& Chunk : Chunks)
	{
		UpsertChunk(Chunk);
	}
	FlushLookup();
	for (uint16 Id : Upserts)
	{
		OnPaletteSlotChanged(Id);
	}
	for (uint16 Id : Removed)
	{
		OnPaletteSlotChanged(Id);
	}
	if (Chunks.Num() > 0)
	{
		OnPaintChunksChanged(Chunks);
	}
}

void AVoxelFinishVolume::UpsertPaletteItem(uint16 FinishId)
{
	const FFinish* Finish = Session.GetPalette().Find(FinishId);
	if (!Finish)
	{
		return;
	}
	for (FVoxelFinishPaletteItem& Item : PaletteRep.Items)
	{
		if (Item.FinishId == FinishId)
		{
			Item.Finish = *Finish;
			Item.BlueprintRefs = Session.GetPalette().GetBlueprintRefs(FinishId);
			PaletteRep.MarkItemDirty(Item);
			return;
		}
	}
	FVoxelFinishPaletteItem& Item = PaletteRep.Items.AddDefaulted_GetRef();
	Item.FinishId = FinishId;
	Item.Finish = *Finish;
	Item.BlueprintRefs = Session.GetPalette().GetBlueprintRefs(FinishId);
	PaletteRep.MarkItemDirty(Item);
}

void AVoxelFinishVolume::RemovePaletteItem(uint16 FinishId)
{
	const int32 Index = PaletteRep.Items.IndexOfByPredicate([FinishId](const FVoxelFinishPaletteItem& Item)
	{
		return Item.FinishId == FinishId;
	});
	if (Index != INDEX_NONE)
	{
		PaletteRep.Items.RemoveAt(Index);
		PaletteRep.MarkArrayDirty();
	}
}

void AVoxelFinishVolume::UpsertChunk(const FIntVector& ChunkCoord)
{
	TArray<TPair<uint16, uint16>> Cells;
	Session.ExportChunkDelta(ChunkCoord, Cells);
	FVoxelFinishChunkItem* Item = nullptr;
	for (FVoxelFinishChunkItem& Existing : ChunkRep.Items)
	{
		if (Existing.ChunkCoord == ChunkCoord)
		{
			Item = &Existing;
			break;
		}
	}
	if (!Item)
	{
		Item = &ChunkRep.Items.AddDefaulted_GetRef();
		Item->ChunkCoord = ChunkCoord;
		Item->Revision = 0;
	}
	++Item->Revision;
	Item->Cells.Reset();
	Item->Cells.Reserve(Cells.Num());
	for (const TPair<uint16, uint16>& Cell : Cells)
	{
		FVoxelFinishCellRep Rep;
		Rep.LocalIndex = Cell.Key;
		Rep.FinishId = Cell.Value;
		Item->Cells.Add(Rep);
	}
	ChunkRep.MarkItemDirty(*Item);
}

UTexture2D* AVoxelFinishVolume::GetLookupTexture()
{
	if (LookupTexture)
	{
		return LookupTexture;
	}
	if (!FApp::CanEverRender() || GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(VoxelFinish::LookupTextureWidth, VoxelFinish::LookupTextureHeight, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}
	Texture->Filter = TF_Nearest;
	Texture->SRGB = false;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	Texture->LODGroup = TEXTUREGROUP_Pixels2D;

	FVoxelFinishLookup::InitPixels(LookupPixels);
	for (int32 Id = 1; Id <= VoxelFinish::MaxPaletteEntries; ++Id)
	{
		if (const FFinish* Finish = Session.GetPalette().Find(static_cast<uint16>(Id)))
		{
			FVoxelFinishLookup::WriteFinish(LookupPixels, static_cast<uint16>(Id), *Finish);
		}
	}
	FVoxelFinishLookup::Upload(Texture, LookupPixels);
	PendingLookupIds.Reset();
	LookupTexture = Texture;
	return LookupTexture;
}

void AVoxelFinishVolume::ApplyLookupTo(UMaterialInstanceDynamic* Material)
{
	if (Material)
	{
		if (UTexture2D* Texture = GetLookupTexture())
		{
			Material->SetTextureParameterValue(FVoxelFinishLookup::ParameterName, Texture);
		}
	}
}

void AVoxelFinishVolume::MarkLookupDirty(uint16 FinishId)
{
	if (FinishId != VoxelFinish::UnpaintedId && FinishId < VoxelFinish::LookupTextureWidth)
	{
		PendingLookupIds.Add(FinishId);
	}
}

void AVoxelFinishVolume::FlushLookup()
{
	if (!LookupTexture)
	{
		// Nothing renders the lookup yet. GetLookupTexture bakes the whole palette when it is first asked for.
		PendingLookupIds.Reset();
		return;
	}
	if (PendingLookupIds.Num() == 0)
	{
		return;
	}
	TArray<uint16> Ids = PendingLookupIds.Array();
	PendingLookupIds.Reset();
	Ids.Sort();
	for (uint16 Id : Ids)
	{
		if (const FFinish* Finish = Session.GetPalette().Find(Id))
		{
			FVoxelFinishLookup::WriteFinish(LookupPixels, Id, *Finish);
		}
		else
		{
			FVoxelFinishLookup::ClearFinish(LookupPixels, Id);
		}
	}

	// One region per run of adjacent columns, each the full 4 rows.
	TArray<FUpdateTextureRegion2D> Runs;
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		if (Runs.Num() > 0 && Runs.Last().DestX + Runs.Last().Width == Ids[Index])
		{
			++Runs.Last().Width;
			continue;
		}
		Runs.Emplace(Ids[Index], 0, Ids[Index], 0, 1, VoxelFinish::LookupTextureHeight);
	}

	// The render thread reads the copies after this returns; the cleanup callback frees them.
	const int32 Bytes = LookupPixels.Num() * sizeof(FColor);
	uint8* Source = new uint8[Bytes];
	FMemory::Memcpy(Source, LookupPixels.GetData(), Bytes);
	FUpdateTextureRegion2D* Regions = new FUpdateTextureRegion2D[Runs.Num()];
	for (int32 Index = 0; Index < Runs.Num(); ++Index)
	{
		Regions[Index] = Runs[Index];
	}
	LookupTexture->UpdateTextureRegions(0, static_cast<uint32>(Runs.Num()), Regions,
		static_cast<uint32>(VoxelFinish::LookupTextureWidth * sizeof(FColor)), static_cast<uint32>(sizeof(FColor)), Source,
		[](uint8* InSource, const FUpdateTextureRegion2D* InRegions)
		{
			delete[] InSource;
			delete[] InRegions;
		});
}

void AVoxelFinishVolume::NotifyPaletteItem(const FVoxelFinishPaletteItem& Item)
{
	Session.ApplyReplicatedPaletteSlot(Item.FinishId, Item.Finish, Item.BlueprintRefs);
	MarkLookupDirty(Item.FinishId);
	FlushLookup();
	OnPaletteSlotChanged(Item.FinishId);
}

void AVoxelFinishVolume::NotifyPaletteItemRemoved(const FVoxelFinishPaletteItem& Item, const FVoxelFinishPaletteArray& InArray)
{
	// The server may free an id and hand it to a new finish in the same update. If another live item already
	// carries this id, that item owns the slot and its add/change callback writes it; do not clear it here.
	for (const FVoxelFinishPaletteItem& Other : InArray.Items)
	{
		if (&Other != &Item && Other.FinishId == Item.FinishId && Other.ReplicationID != Item.ReplicationID)
		{
			return;
		}
	}
	Session.ApplyReplicatedPaletteRemoval(Item.FinishId);
	MarkLookupDirty(Item.FinishId);
	FlushLookup();
	OnPaletteSlotChanged(Item.FinishId);
}

void AVoxelFinishVolume::NotifyChunkItem(const FVoxelFinishChunkItem& Item)
{
	TArray<TPair<uint16, uint16>> Cells;
	Cells.Reserve(Item.Cells.Num());
	for (const FVoxelFinishCellRep& Cell : Item.Cells)
	{
		Cells.Emplace(Cell.LocalIndex, Cell.FinishId);
	}
	Session.ApplyReplicatedChunk(Item.ChunkCoord, Cells);
	OnPaintChunksChanged({ Item.ChunkCoord });
}
