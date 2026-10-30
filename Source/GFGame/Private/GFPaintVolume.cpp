#include "GFPaintVolume.h"

#include "EngineUtils.h"
#include "GFDyes.h"
#include "GFEconomy.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFVoxelWorld.h"
#include "VoxelFinish.h"

TArray<FLinearColor> FGFPaintCost::VisibleColors(const FFinish& Finish)
{
	auto Shown = [&Finish](const FLinearColor& C)
	{
		return FLinearColor(FMath::Clamp(C.R * Finish.Tint.R, 0.f, 1.f), FMath::Clamp(C.G * Finish.Tint.G, 0.f, 1.f), FMath::Clamp(C.B * Finish.Tint.B, 0.f, 1.f), 1.f);
	};
	TArray<FLinearColor> Out = { Shown(Finish.ColorA) };
	if (Finish.PatternId != VoxelFinish::SolidPatternId)
	{
		Out.Add(Shown(Finish.ColorB));
	}
	return Out;
}

FLinearColor FGFPaintCost::DisplayColor(const FFinish& Finish)
{
	return VisibleColors(Finish)[0];
}

TArray<FName> FGFPaintCost::DyesFor(const FFinish& Finish)
{
	TArray<FName> Out;
	for (const FLinearColor& C : VisibleColors(Finish))
	{
		Out.AddUnique(FGFDyes::DyeForColor(C));
	}
	return Out;
}

bool FGFPaintCredits::TrySpend(const FString& Account, const TArray<FLinearColor>& Colors, TFunctionRef<bool(const TArray<FGFItemStack>&)> Burn)
{
	if (Account.IsEmpty() || Colors.Num() == 0)
	{
		return false;
	}
	TMap<FName, int32>& Mine = Credits.FindOrAdd(Account);
	TArray<FName> Dyes;
	TArray<FGFItemStack> ToBurn;
	for (const FLinearColor& C : Colors)
	{
		const FName Dye = FGFDyes::DyeForColor(C);
		if (Dyes.Contains(Dye))
		{
			continue;
		}
		Dyes.Add(Dye);
		if (Mine.FindRef(Dye) <= 0)
		{
			ToBurn.Append(FGFDyes::CostForBlocks(C, FGFDyes::BlocksPerDye));
		}
	}
	if (ToBurn.Num() > 0)
	{
		if (!Burn(ToBurn))
		{
			return false;
		}
		for (const FGFItemStack& S : ToBurn)
		{
			Mine.FindOrAdd(S.ItemId) += S.Count * FGFDyes::BlocksPerDye;
		}
	}
	for (const FName Dye : Dyes)
	{
		--Mine.FindOrAdd(Dye);
	}
	return true;
}

int32 FGFPaintCredits::GetCredits(const FString& Account, FName DyeItem) const
{
	const TMap<FName, int32>* Mine = Credits.Find(Account);
	return Mine ? Mine->FindRef(DyeItem) : 0;
}

AGFPaintVolume* AGFPaintVolume::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFPaintVolume> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AGFPaintVolume::AGFPaintVolume()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AGFPaintVolume::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!HasAuthority())
	{
		return;
	}
	// The hooks only run inside this actor's own session, so capturing this is safe.
	SetCanAffordAndSpend([this](const FFinish& Finish, APlayerState* Payer) { return ServerSpend(Finish, Payer); });
	SetPaintTargetResolver([this](const FIntVector& Cell, FVector& OutCenter) { return ResolveTarget(Cell, OutCenter); });
}

bool AGFPaintVolume::ServerSpend(const FFinish& Finish, APlayerState* Payer)
{
	const AGFPlayerState* PS = Cast<AGFPlayerState>(Payer);
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	if (!HasAuthority() || !PS || PS->PlayerKey.IsEmpty() || !GS || !GS->HasAuthority())
	{
		return false;
	}
	if (GS->bFreeBuild)
	{
		return true;
	}
	const FString Key = PS->PlayerKey;
	return Credits.TrySpend(Key, FGFPaintCost::VisibleColors(Finish), [GS, &Key](const TArray<FGFItemStack>& Stacks)
	{
		return GS->ServerBurn(FGuid::NewGuid(), Key, Stacks, BurnReason()) == EGFTxResult::Applied;
	});
}

bool AGFPaintVolume::ResolveTarget(const FIntVector& Cell, FVector& OutCenter) const
{
	const AGFVoxelWorld* Voxel = FindVoxelWorld();
	// FGFVoxelGrid::Get is a bounds check plus a chunk lookup: a missing chunk reads as air and is never created.
	if (!Voxel || !Voxel->GetAuthoritativeGrid().IsSolid(Cell))
	{
		return false;
	}
	OutCenter = Voxel->CellToWorld(Cell) + FVector(0.f, 0.f, GFBlockSize * 0.5f);
	return true;
}

// ---------------- Display ----------------

AGFVoxelWorld* AGFPaintVolume::FindVoxelWorld() const
{
	const UWorld* World = GetWorld();
	const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	if (AGFVoxelWorld* Voxel = GS ? GS->GetVoxelWorld() : nullptr)
	{
		return Voxel;
	}
	if (World)
	{
		for (TActorIterator<AGFVoxelWorld> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

void AGFPaintVolume::OnPaintChunksChanged(const TArray<FIntVector>& ChunkCoords)
{
	PendingChunks.Append(ChunkCoords);
}

void AGFPaintVolume::OnPaletteSlotChanged(uint16 FinishId)
{
	PendingSlots.Add(FinishId);
}

void AGFPaintVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FlushPaintDisplay();
}

void AGFPaintVolume::BindVoxelWorld(AGFVoxelWorld* Voxel)
{
	if (!HasAuthority() || !Voxel || BoundTo.Get() == Voxel)
	{
		return;
	}
	if (AGFVoxelWorld* Old = BoundTo.Get())
	{
		Old->OnBlockChanged.RemoveDynamic(this, &AGFPaintVolume::HandleBlockChanged);
		Old->OnGridReset.Remove(GridResetHandle);
	}
	BoundTo = Voxel;
	Voxel->OnBlockChanged.AddUniqueDynamic(this, &AGFPaintVolume::HandleBlockChanged);
	GridResetHandle = Voxel->OnGridReset.AddUObject(this, &AGFPaintVolume::HandleGridReset);
	// Blocks may have gone while nobody was listening.
	AuthorityClearUnresolvable();
}

void AGFPaintVolume::HandleBlockChanged(FIntVector Cell, FName BlockId)
{
	// A removed or replaced block (FillLayers) loses its paint. A block placed on an empty cell has none to lose.
	if (HasAuthority())
	{
		AuthorityClearCells({ Cell });
	}
}

void AGFPaintVolume::HandleGridReset()
{
	AuthorityClearUnresolvable();
}

void AGFPaintVolume::DrawPaintChunk(AGFVoxelWorld* Voxel, const FIntVector& PaintChunk)
{
	const FIntVector Min = PaintChunk * VoxelFinish::ChunkSize;
	Voxel->ClearPaintInRegion(Min, Min + FIntVector(VoxelFinish::ChunkSize - 1));
	TArray<TPair<uint16, uint16>> Cells;
	GetSession().GetGrid().ExportChunk(PaintChunk, Cells);
	for (const TPair<uint16, uint16>& Cell : Cells)
	{
		// A client can learn of a cell before its palette slot; the slot's own callback draws it.
		if (const FFinish* Finish = GetSession().GetPalette().Find(Cell.Value))
		{
			Voxel->SetCellPaint(VoxelFinish::ChunkLocalToCell(PaintChunk, Cell.Key), FGFPaintCost::DisplayColor(*Finish));
		}
	}
}

void AGFPaintVolume::FlushPaintDisplay()
{
	AGFVoxelWorld* Voxel = FindVoxelWorld();
	if (!Voxel)
	{
		return; // keep the queue: everything is redrawn once a voxel world shows up
	}
	if (DrawnOn.Get() != Voxel)
	{
		// First fill (or a new voxel world): draw every painted cell.
		DrawnOn = Voxel;
		BindVoxelWorld(Voxel);
		PendingChunks.Reset();
		PendingSlots.Reset();
		Voxel->ClearAllPaint();
		GetSession().GetGrid().ForEachPaintedBlock([this, Voxel](FIntVector Cell, uint16 FinishId)
		{
			if (const FFinish* Finish = GetSession().GetPalette().Find(FinishId))
			{
				Voxel->SetCellPaint(Cell, FGFPaintCost::DisplayColor(*Finish));
			}
		});
		return;
	}
	if (PendingChunks.Num() > 0)
	{
		const TSet<FIntVector> Chunks = MoveTemp(PendingChunks);
		PendingChunks.Reset();
		for (const FIntVector& Chunk : Chunks)
		{
			DrawPaintChunk(Voxel, Chunk);
		}
	}
	if (PendingSlots.Num() > 0)
	{
		// One pass over the painted cells for every slot that changed since the last flush.
		const TSet<uint16> Slots = MoveTemp(PendingSlots);
		PendingSlots.Reset();
		GetSession().GetGrid().ForEachPaintedBlock([this, Voxel, &Slots](FIntVector Cell, uint16 FinishId)
		{
			if (!Slots.Contains(FinishId))
			{
				return;
			}
			if (const FFinish* Finish = GetSession().GetPalette().Find(FinishId))
			{
				Voxel->SetCellPaint(Cell, FGFPaintCost::DisplayColor(*Finish));
			}
			else
			{
				Voxel->ClearCellPaint(Cell);
			}
		});
	}
}
