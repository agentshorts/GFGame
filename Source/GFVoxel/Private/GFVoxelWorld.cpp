#include "GFVoxelWorld.h"
#include "GFAssetMap.h"
#include "GFLog.h"
#include "GFDataTables.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

const FName AGFVoxelWorld::PaintColorParameter(TEXT("Color"));

// ---------------- Fast array callbacks (clients) ----------------

void FGFChunkBaseItem::PostReplicatedAdd(const FGFChunkBaseArray& InArray)
{
	if (InArray.Owner) { InArray.Owner->OnChunkReplicated(Coord); }
}
void FGFChunkBaseItem::PostReplicatedChange(const FGFChunkBaseArray& InArray)
{
	if (InArray.Owner) { InArray.Owner->OnChunkReplicated(Coord); }
}
void FGFChunkDeltaItem::PostReplicatedAdd(const FGFChunkDeltaArray& InArray)
{
	if (InArray.Owner) { InArray.Owner->OnChunkReplicated(Coord); }
}
void FGFChunkDeltaItem::PostReplicatedChange(const FGFChunkDeltaArray& InArray)
{
	if (InArray.Owner) { InArray.Owner->OnChunkReplicated(Coord); }
}

// ---------------- Setup ----------------

AGFVoxelWorld::AGFVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.f);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
	Grid = FGFVoxelGrid(PlotSizeInBlocks);
	ChunkBases.Owner = this;
	ChunkDeltas.Owner = this;
}

void AGFVoxelWorld::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFVoxelWorld, PlotSizeInBlocks);
	DOREPLIFETIME(AGFVoxelWorld, ChunkBases);
	DOREPLIFETIME(AGFVoxelWorld, ChunkDeltas);
}

void AGFVoxelWorld::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ChunkBases.Owner = this;
	ChunkDeltas.Owner = this;
}

void AGFVoxelWorld::EnsureGridSize()
{
	if (Grid.GetSize() != PlotSizeInBlocks)
	{
		Grid = FGFVoxelGrid(PlotSizeInBlocks);
	}
	if (!HasAuthority() && AuthGrid.GetSize() != PlotSizeInBlocks)
	{
		AuthGrid = FGFVoxelGrid(PlotSizeInBlocks);
	}
}

void AGFVoxelWorld::EnsureBlockTable()
{
	if (!BlockTable)
	{
		BlockTable = FGFDataTables::Get<FGFBlockRow>(TEXT("DT_Blocks"));
	}
}

void AGFVoxelWorld::BeginPlay()
{
	Super::BeginPlay();
	EnsureBlockTable();
	EnsureGridSize();
	if (HasAuthority())
	{
		if (Grid.CountSolid() == 0 && StarterGroundLayers > 0 && ChunkBases.Items.Num() == 0)
		{
			FillLayers(StarterGroundLayers, GroundBlock);
		}
		else
		{
			ServerRebaseAll();
		}
	}
	else
	{
		// Anything that arrived before BeginPlay.
		for (const FGFChunkBaseItem& Base : ChunkBases.Items)
		{
			ClientReconcileChunk(Base.Coord);
		}
	}
	for (const FIntVector& C : Grid.GetChunkCoords())
	{
		MarkChunkDirty(C);
	}
	RebuildDirtyChunks();
}

void AGFVoxelWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RebuildDirtyChunks();
}

const FGFBlockRow* AGFVoxelWorld::FindBlockRow(FName BlockId) const
{
	return BlockTable ? BlockTable->FindRow<FGFBlockRow>(BlockId, TEXT("GFVoxelWorld"), false) : nullptr;
}

TArray<FName> AGFVoxelWorld::GetPlaceableBlockIds() const
{
	TArray<FName> Out;
	if (BlockTable)
	{
		for (const FName& RowName : BlockTable->GetRowNames())
		{
			const FGFBlockRow* Row = FindBlockRow(RowName);
			if (Row && Row->bPlaceable)
			{
				Out.Add(RowName);
			}
		}
	}
	if (Out.Num() == 0)
	{
		Out.Add(GroundBlock);
	}
	return Out;
}

FIntVector AGFVoxelWorld::WorldToCell(FVector WorldLocation) const
{
	const FVector Local = (WorldLocation - GetActorLocation()) / GFBlockSize;
	return FIntVector(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y), FMath::FloorToInt(Local.Z));
}

FVector AGFVoxelWorld::CellToWorld(FIntVector Cell) const
{
	return GetActorLocation() + FVector(Cell.X + 0.5f, Cell.Y + 0.5f, Cell.Z) * GFBlockSize;
}

void AGFVoxelWorld::HitToCells(FVector HitLocation, FVector HitNormal, FIntVector& OutHitCell, FIntVector& OutAdjacentCell) const
{
	const FVector N = HitNormal.GetSafeNormal();
	OutHitCell = WorldToCell(HitLocation - N * 1.f);
	OutAdjacentCell = WorldToCell(HitLocation + N * 1.f);
}

// ---------------- Server edits ----------------

FGFChunkBaseItem* AGFVoxelWorld::FindBase(const FIntVector& ChunkCoord)
{
	return ChunkBases.Items.FindByPredicate([&](const FGFChunkBaseItem& I) { return I.Coord == ChunkCoord; });
}

FGFChunkDeltaItem* AGFVoxelWorld::FindDelta(const FIntVector& ChunkCoord)
{
	return ChunkDeltas.Items.FindByPredicate([&](const FGFChunkDeltaItem& I) { return I.Coord == ChunkCoord; });
}

int32 AGFVoxelWorld::GetChunkRevision(const FIntVector& ChunkCoord) const
{
	if (HasAuthority())
	{
		const FGFChunkBaseItem* Base = ChunkBases.Items.FindByPredicate([&](const FGFChunkBaseItem& I) { return I.Coord == ChunkCoord; });
		const FGFChunkDeltaItem* Delta = ChunkDeltas.Items.FindByPredicate([&](const FGFChunkDeltaItem& I) { return I.Coord == ChunkCoord; });
		const int32 BaseRev = Base ? Base->BaseRevision : 0;
		return BaseRev + ((Delta && Delta->BaseRevision == BaseRev) ? Delta->Edits.Num() : 0);
	}
	const FChunkApplied* Applied = ClientApplied.Find(ChunkCoord);
	return Applied ? Applied->BaseRevision + Applied->EditsApplied : 0;
}

int64 AGFVoxelWorld::GetTotalRevision() const
{
	int64 Total = 0;
	for (const FGFChunkBaseItem& Base : ChunkBases.Items)
	{
		Total += GetChunkRevision(Base.Coord);
	}
	return Total;
}

void AGFVoxelWorld::ServerRebaseChunk(const FIntVector& ChunkCoord)
{
	FGFChunkBaseItem* Base = FindBase(ChunkCoord);
	FGFChunkDeltaItem* Delta = FindDelta(ChunkCoord);
	const int32 NewRevision = GetChunkRevision(ChunkCoord) + 1; // +1 so a re-base is always a visible change
	if (!Base)
	{
		Base = &ChunkBases.Items.AddDefaulted_GetRef();
		Base->Coord = ChunkCoord;
	}
	Base->BaseRevision = NewRevision;
	Grid.EncodeChunk(ChunkCoord, Base->Palette, Base->RLE);
	ChunkBases.MarkItemDirty(*Base);
	if (Delta)
	{
		Delta->BaseRevision = NewRevision;
		Delta->Edits.Reset();
		ChunkDeltas.MarkItemDirty(*Delta);
	}
}

void AGFVoxelWorld::ServerRebaseAll()
{
	TSet<FIntVector> Coords(Grid.GetChunkCoords());
	for (const FGFChunkBaseItem& Base : ChunkBases.Items)
	{
		Coords.Add(Base.Coord);
	}
	for (const FIntVector& C : Coords)
	{
		ServerRebaseChunk(C);
	}
	ForceNetUpdate();
}

bool AGFVoxelWorld::ServerApplyEdit(const FIntVector& Cell, FName BlockId, FName* OutPrevious, FString* OutReason)
{
	auto Fail = [OutReason](const TCHAR* Why) { if (OutReason) { *OutReason = Why; } return false; };
	if (!HasAuthority())
	{
		return Fail(TEXT("not authority"));
	}
	EnsureGridSize();
	if (!Grid.IsInBounds(Cell))
	{
		return Fail(TEXT("out of bounds"));
	}
	const FName Previous = Grid.Get(Cell);
	if (!BlockId.IsNone())
	{
		if (!Previous.IsNone())
		{
			return Fail(TEXT("cell occupied"));
		}
		if (BlockTable)
		{
			const FGFBlockRow* Row = FindBlockRow(BlockId);
			if (!Row || !Row->bPlaceable)
			{
				return Fail(TEXT("unknown or unplaceable block"));
			}
		}
	}
	else if (Previous.IsNone())
	{
		return Fail(TEXT("cell empty"));
	}

	Grid.Set(Cell, BlockId);
	const FIntVector ChunkCoord = FGFVoxelGrid::CellToChunk(Cell);
	FGFChunkBaseItem* Base = FindBase(ChunkCoord);
	if (!Base)
	{
		ServerRebaseChunk(ChunkCoord); // new chunk: its first base already contains this edit
	}
	else
	{
		FGFChunkDeltaItem* Delta = FindDelta(ChunkCoord);
		if (!Delta)
		{
			Delta = &ChunkDeltas.Items.AddDefaulted_GetRef();
			Delta->Coord = ChunkCoord;
			Delta->BaseRevision = Base->BaseRevision;
		}
		FGFVoxelEdit& Edit = Delta->Edits.AddDefaulted_GetRef();
		Edit.LocalIndex = static_cast<uint16>(FGFVoxelGrid::LocalIndex(Cell));
		Edit.BlockId = BlockId;
		ChunkDeltas.MarkItemDirty(*Delta);
		if (Delta->Edits.Num() >= CompactThreshold)
		{
			ServerRebaseChunk(ChunkCoord);
		}
	}
	ForceNetUpdate();
	MarkChunkDirty(ChunkCoord);
	if (OutPrevious)
	{
		*OutPrevious = Previous;
	}
	OnBlockChanged.Broadcast(Cell, BlockId);
	return true;
}

bool AGFVoxelWorld::PlaceBlock(FIntVector Cell, FName BlockId)
{
	if (BlockId.IsNone())
	{
		return false;
	}
	FString Reason;
	const bool bOk = ServerApplyEdit(Cell, BlockId, nullptr, &Reason);
	if (!bOk && Reason.Contains(TEXT("unplaceable")))
	{
		UE_LOG(LogGF, Warning, TEXT("PlaceBlock: %s (%s)"), *Reason, *BlockId.ToString());
	}
	return bOk;
}

FName AGFVoxelWorld::RemoveBlock(FIntVector Cell)
{
	FName Previous;
	return ServerApplyEdit(Cell, NAME_None, &Previous) ? Previous : FName();
}

void AGFVoxelWorld::ServerStamp(TFunctionRef<FName(const FIntVector& Cell)> BlockAt)
{
	if (!HasAuthority())
	{
		return;
	}
	EnsureGridSize();
	Grid.Clear();
	const FIntVector S = Grid.GetSize();
	for (int32 Z = 0; Z < S.Z; ++Z)
	{
		for (int32 Y = 0; Y < S.Y; ++Y)
		{
			for (int32 X = 0; X < S.X; ++X)
			{
				const FIntVector Cell(X, Y, Z);
				const FName Id = BlockAt(Cell);
				if (!Id.IsNone())
				{
					Grid.Set(Cell, Id);
				}
			}
		}
	}
	ServerRebaseAll();
	for (const FIntVector& C : Grid.GetChunkCoords())
	{
		MarkChunkDirty(C);
	}
	OnGridReset.Broadcast();
}

void AGFVoxelWorld::FillLayers(int32 NumLayers, FName BlockId)
{
	if (!HasAuthority())
	{
		return;
	}
	EnsureGridSize();
	const FIntVector S = Grid.GetSize();
	// Blocks that were already there are replaced: listeners hear about each one (e.g. its paint is cleared).
	TArray<FIntVector> Overwritten;
	for (int32 Z = 0; Z < FMath::Min(NumLayers, S.Z); ++Z)
	{
		for (int32 Y = 0; Y < S.Y; ++Y)
		{
			for (int32 X = 0; X < S.X; ++X)
			{
				const FIntVector Cell(X, Y, Z);
				if (Grid.IsSolid(Cell))
				{
					Overwritten.Add(Cell);
				}
				Grid.Set(Cell, BlockId);
			}
		}
	}
	ServerRebaseAll();
	for (const FIntVector& C : Grid.GetChunkCoords())
	{
		MarkChunkDirty(C);
	}
	for (const FIntVector& Cell : Overwritten)
	{
		OnBlockChanged.Broadcast(Cell, BlockId);
	}
}

int32 AGFVoxelWorld::ClearRegion(const FIntVector& Min, const FIntVector& Max)
{
	int32 Removed = 0;
	TArray<FIntVector> Cells;
	Grid.ForEachSolid([&](const FIntVector& C, FName)
	{
		if (C.X >= Min.X && C.Y >= Min.Y && C.Z >= Min.Z && C.X <= Max.X && C.Y <= Max.Y && C.Z <= Max.Z)
		{
			Cells.Add(C);
		}
	});
	for (const FIntVector& C : Cells)
	{
		Removed += ServerApplyEdit(C, NAME_None) ? 1 : 0;
	}
	return Removed;
}

bool AGFVoxelWorld::ApplyRegion(const FIntVector& Min, const FGFVoxelSaveData& Data, const FIntVector& RegionSize)
{
	if (!HasAuthority())
	{
		return false;
	}
	FGFVoxelGrid Temp(RegionSize);
	if (!Temp.FromSaveData(Data))
	{
		return false;
	}
	ClearRegion(Min, Min + RegionSize - FIntVector(1, 1, 1));
	bool bAll = true;
	Temp.ForEachSolid([&](const FIntVector& C, FName Id)
	{
		bAll &= ServerApplyEdit(Min + C, Id);
	});
	return bAll;
}

void AGFVoxelWorld::ClearAll()
{
	if (!HasAuthority())
	{
		return;
	}
	for (const FIntVector& C : Grid.GetChunkCoords())
	{
		MarkChunkDirty(C);
	}
	Grid.Clear();
	ServerRebaseAll();
	OnGridReset.Broadcast();
}

bool AGFVoxelWorld::LoadData(const FGFVoxelSaveData& Data)
{
	if (!HasAuthority())
	{
		return false;
	}
	for (const FIntVector& C : Grid.GetChunkCoords())
	{
		MarkChunkDirty(C);
	}
	const bool bOk = Grid.FromSaveData(Data);
	PlotSizeInBlocks = Grid.GetSize();
	ServerRebaseAll();
	for (const FIntVector& C : Grid.GetChunkCoords())
	{
		MarkChunkDirty(C);
	}
	OnGridReset.Broadcast();
	return bOk;
}

// ---------------- Client side ----------------

void AGFVoxelWorld::OnChunkReplicated(const FIntVector& ChunkCoord)
{
	if (HasAuthority())
	{
		return;
	}
	EnsureGridSize();
	ClientReconcileChunk(ChunkCoord);
}

void AGFVoxelWorld::ClientReconcileChunk(const FIntVector& ChunkCoord)
{
	const FGFChunkBaseItem* Base = FindBase(ChunkCoord);
	if (!Base)
	{
		return; // deltas without their base: wait for the base
	}
	FChunkApplied& Applied = ClientApplied.FindOrAdd(ChunkCoord);
	if (Applied.BaseRevision != Base->BaseRevision)
	{
		if (!AuthGrid.DecodeChunk(ChunkCoord, Base->Palette, Base->RLE))
		{
			UE_LOG(LogGF, Warning, TEXT("Voxel: malformed base for chunk %s"), *ChunkCoord.ToString());
		}
		Applied.BaseRevision = Base->BaseRevision;
		Applied.EditsApplied = 0;
	}
	if (const FGFChunkDeltaItem* Delta = FindDelta(ChunkCoord))
	{
		if (Delta->BaseRevision == Applied.BaseRevision)
		{
			if (Delta->Edits.Num() < Applied.EditsApplied)
			{
				// Should not happen (deltas only grow per base); rebuild from the base to be safe.
				AuthGrid.DecodeChunk(ChunkCoord, Base->Palette, Base->RLE);
				Applied.EditsApplied = 0;
			}
			for (int32 i = Applied.EditsApplied; i < Delta->Edits.Num(); ++i)
			{
				const FGFVoxelEdit& E = Delta->Edits[i];
				AuthGrid.Set(FGFVoxelGrid::ChunkLocalToCell(ChunkCoord, E.LocalIndex), E.BlockId);
			}
			Applied.EditsApplied = Delta->Edits.Num();
		}
		// Delta for a newer base: keep showing what we have until that base arrives.
	}
	RefreshDisplayedChunk(ChunkCoord);
}

void AGFVoxelWorld::RefreshDisplayedChunk(const FIntVector& ChunkCoord)
{
	if (HasAuthority())
	{
		MarkChunkDirty(ChunkCoord);
		return;
	}
	const int32 Revision = GetChunkRevision(ChunkCoord);
	Grid.CopyChunkFrom(AuthGrid, ChunkCoord);
	for (auto It = Predictions.CreateIterator(); It; ++It)
	{
		const FPrediction& P = It->Value;
		if (FGFVoxelGrid::CellToChunk(P.Cell) != ChunkCoord)
		{
			continue;
		}
		if (P.bAcked && Revision >= P.AckRevision)
		{
			It.RemoveCurrent(); // the server state now includes it
			continue;
		}
		Grid.Set(P.Cell, P.BlockId);
	}
	MarkChunkDirty(ChunkCoord);
}

void AGFVoxelWorld::PredictEdit(int32 EditId, const FIntVector& Cell, FName BlockId)
{
	if (HasAuthority() || !Grid.IsInBounds(Cell))
	{
		return;
	}
	EnsureGridSize();
	FPrediction& P = Predictions.Add(EditId);
	P.Cell = Cell;
	P.BlockId = BlockId;
	Grid.Set(Cell, BlockId);
	MarkChunkDirty(FGFVoxelGrid::CellToChunk(Cell));
	OnBlockChanged.Broadcast(Cell, BlockId);
}

void AGFVoxelWorld::ResolvePrediction(int32 EditId, bool bAccepted, int32 ServerChunkRevision)
{
	FPrediction* P = Predictions.Find(EditId);
	if (!P)
	{
		return;
	}
	const FIntVector ChunkCoord = FGFVoxelGrid::CellToChunk(P->Cell);
	if (bAccepted)
	{
		P->bAcked = true;
		P->AckRevision = ServerChunkRevision;
	}
	else
	{
		Predictions.Remove(EditId);
	}
	RefreshDisplayedChunk(ChunkCoord);
}

// ---------------- Rendering ----------------

void AGFVoxelWorld::MarkChunkDirty(const FIntVector& ChunkCoord)
{
	DirtyChunks.Add(ChunkCoord);
}

void AGFVoxelWorld::RebuildDirtyChunks()
{
	if (DirtyChunks.Num() == 0 || !HasActorBegunPlay())
	{
		return;
	}
	const TSet<FIntVector> ToBuild = MoveTemp(DirtyChunks);
	DirtyChunks.Reset();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	for (const FIntVector& C : ToBuild)
	{
		RebuildChunkMeshes(C);
	}
}

namespace
{
	const TCHAR* ArtMeshPath(FName BlockId)
	{
		if (BlockId == FName(TEXT("Grass"))) { return TEXT("/Game/Art/Blocks/SM_Block_Grass.SM_Block_Grass"); }
		if (BlockId == FName(TEXT("Dirt"))) { return TEXT("/Game/Art/Blocks/SM_Block_Dirt.SM_Block_Dirt"); }
		if (BlockId == FName(TEXT("WoodPlank"))) { return TEXT("/Game/Art/Blocks/SM_Block_WoodPlank.SM_Block_WoodPlank"); }
		if (BlockId == FName(TEXT("Stone"))) { return TEXT("/Game/Art/Blocks/SM_Block_Stone.SM_Block_Stone"); }
		if (BlockId == FName(TEXT("Glass"))) { return TEXT("/Game/Art/Blocks/SM_Block_Glass.SM_Block_Glass"); }
		if (BlockId == FName(TEXT("Fabric"))) { return TEXT("/Game/Art/Blocks/SM_Block_Fabric.SM_Block_Fabric"); }
		if (BlockId == FName(TEXT("Basalt"))) { return TEXT("/Game/Art/Blocks/SM_Block_Cobblestone.SM_Block_Cobblestone"); }
		if (BlockId == FName(TEXT("Farmland"))) { return TEXT("/Game/Art/Blocks/SM_Block_Farmland.SM_Block_Farmland"); }
		if (BlockId == FName(TEXT("Log"))) { return TEXT("/Game/Art/Blocks/SM_Block_Log.SM_Block_Log"); }
		if (BlockId == FName(TEXT("Leaves"))) { return TEXT("/Game/Art/Blocks/SM_Block_Leaves.SM_Block_Leaves"); }
		return nullptr;
	}

	FLinearColor BlockTint(FName BlockId, const FGFBlockRow* Row)
	{
		if (Row)
		{
			return Row->DebugColor;
		}
		if (BlockId == FName(TEXT("Ice")))
		{
			return FLinearColor(0.75f, 0.88f, 0.96f, 1.f);
		}
		if (BlockId == FName(TEXT("Lava")))
		{
			return FLinearColor(217.f / 255.f, 84.f / 255.f, 43.f / 255.f, 1.f);
		}
		return FLinearColor(0.3f, 0.7f, 0.3f, 1.f);
	}
}

UStaticMesh* AGFVoxelWorld::ResolveDrawMesh(FName BlockId, bool& bOutBottomPivot)
{
	if (const FCachedBlockDraw* Cached = DrawCache.Find(BlockId))
	{
		if (Cached->bReady)
		{
			bOutBottomPivot = Cached->bBottomPivot;
			return Cached->Mesh;
		}
	}

	const FGFBlockRow* Row = FindBlockRow(BlockId);
	FCachedBlockDraw Draw;
	if (Row && !Row->Mesh.IsNull())
	{
		Draw.Mesh = Row->Mesh.LoadSynchronous();
		if (Draw.Mesh)
		{
			Draw.bBottomPivot = Row->bPivotIsBottomCenter;
		}
	}
	if (!Draw.Mesh)
	{
		if (const TCHAR* Path = ArtMeshPath(BlockId))
		{
			Draw.Mesh = LoadObject<UStaticMesh>(nullptr, Path);
			Draw.bBottomPivot = Draw.Mesh != nullptr;
		}
	}
	if (!Draw.Mesh)
	{
		Draw.Mesh = FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube);
		Draw.bBottomPivot = false;
	}
	Draw.bReady = true;
	DrawCache.Add(BlockId, Draw);
	bOutBottomPivot = Draw.bBottomPivot;
	return Draw.Mesh;
}

UMaterialInterface* AGFVoxelWorld::GetBlockMaterial(FName BlockId)
{
	if (TObjectPtr<UMaterialInterface>* Found = MaterialByBlock.Find(BlockId))
	{
		return *Found;
	}
	const FGFBlockRow* Row = FindBlockRow(BlockId);
	UMaterialInterface* Mat = (Row && !Row->MaterialOverride.IsNull()) ? Row->MaterialOverride.LoadSynchronous() : nullptr;
	bool bBottomPivot = false;
	const UStaticMesh* Mesh = ResolveDrawMesh(BlockId, bBottomPivot);
	const UStaticMesh* Cube = FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube);
	const bool bArtMesh = Mesh && Mesh != Cube;
	if (!Mat && !bArtMesh)
	{
		// Ice and lava have no art mesh: engine cube tinted with the row colour.
		Mat = FGFAssetMap::MakeTint(this, BlockTint(BlockId, Row));
	}
	MaterialByBlock.Add(BlockId, Mat);
	return Mat;
}

UInstancedStaticMeshComponent* AGFVoxelWorld::GetOrCreateIsm(const FIntVector& ChunkCoord, FName BlockId, uint32 PaintKey)
{
	const TTuple<FIntVector, FName, uint32> Key(ChunkCoord, BlockId, PaintKey);
	if (UInstancedStaticMeshComponent** Found = IsmIndex.Find(Key))
	{
		return *Found;
	}
	// Same name on server and clients, so movement bases on blocks resolve over the network.
	// Painted meshes add the colour, which every machine derives from the same replicated paint.
	FString IsmString = FString::Printf(TEXT("Ism_%d_%d_%d_%s"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, *BlockId.ToString());
	if (PaintKey != 0)
	{
		IsmString += FString::Printf(TEXT("_P%08X"), PaintKey);
	}
	const FName IsmName(*IsmString);
	const bool bNameFree = FindObjectFast<UObject>(this, IsmName) == nullptr;
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, bNameFree ? IsmName : NAME_None);
	if (bNameFree)
	{
		Ism->SetNetAddressable();
	}
	Ism->SetupAttachment(Root);
	Ism->SetCollisionProfileName(TEXT("BlockAll"));
	Ism->SetMobility(EComponentMobility::Static);
	bool bBottomPivot = false;
	UStaticMesh* Mesh = ResolveDrawMesh(BlockId, bBottomPivot);
	Ism->SetStaticMesh(Mesh ? Mesh : FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube));
	(void)bBottomPivot;
	if (UMaterialInterface* Mat = PaintKey != 0 ? GetPaintedMaterial(BlockId, PaintKey) : GetBlockMaterial(BlockId))
	{
		Ism->SetMaterial(0, Mat);
	}
	Ism->RegisterComponent();
	AllIsms.Add(Ism);
	IsmIndex.Add(Key, Ism);
	return Ism;
}

uint32 AGFVoxelWorld::PaintKeyOf(const FLinearColor& Color)
{
	FColor Packed = FLinearColor(Color.R, Color.G, Color.B, 1.f).GetClamped().ToFColor(/*bSRGB*/ true);
	Packed.A = 255;
	return Packed.ToPackedRGBA(); // never 0: alpha is 255
}

UMaterialInterface* AGFVoxelWorld::GetPaintedMaterial(FName BlockId, uint32 PaintKey)
{
	const FName CacheKey(*FString::Printf(TEXT("%s_P%08X"), *BlockId.ToString(), PaintKey));
	if (TObjectPtr<UMaterialInterface>* Found = PaintedMaterials.Find(CacheKey))
	{
		return *Found;
	}
	UMaterialInterface* Base = GetBlockMaterial(BlockId);
	if (!Base)
	{
		// Art mesh without a material override: paint over the mesh's own material.
		bool bBottomPivot = false;
		const UStaticMesh* Mesh = ResolveDrawMesh(BlockId, bBottomPivot);
		(void)bBottomPivot;
		Base = Mesh ? Mesh->GetMaterial(0) : nullptr;
	}
	if (const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Base))
	{
		Base = Dynamic->Parent; // a placeholder tint: copy its parent rather than stacking dynamic instances
	}
	UMaterialInstanceDynamic* Painted = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (Painted)
	{
		Painted->SetVectorParameterValue(PaintColorParameter, FLinearColor(FColor(
			static_cast<uint8>(PaintKey >> 24), static_cast<uint8>(PaintKey >> 16), static_cast<uint8>(PaintKey >> 8), 255)));
	}
	PaintedMaterials.Add(CacheKey, Painted);
	return Painted;
}

void AGFVoxelWorld::SetCellPaint(const FIntVector& Cell, const FLinearColor& Color)
{
	const uint32 Key = PaintKeyOf(Color);
	const FColor Packed(static_cast<uint8>(Key >> 24), static_cast<uint8>(Key >> 16), static_cast<uint8>(Key >> 8), 255);
	FColor& Slot = PaintedCells.FindOrAdd(Cell, FColor(0, 0, 0, 0));
	if (Slot != Packed)
	{
		Slot = Packed;
		MarkChunkDirty(FGFVoxelGrid::CellToChunk(Cell));
	}
}

void AGFVoxelWorld::ClearCellPaint(const FIntVector& Cell)
{
	if (PaintedCells.Remove(Cell) > 0)
	{
		MarkChunkDirty(FGFVoxelGrid::CellToChunk(Cell));
	}
}

void AGFVoxelWorld::ClearPaintInRegion(const FIntVector& Min, const FIntVector& Max)
{
	for (auto It = PaintedCells.CreateIterator(); It; ++It)
	{
		const FIntVector& C = It.Key();
		if (C.X >= Min.X && C.Y >= Min.Y && C.Z >= Min.Z && C.X <= Max.X && C.Y <= Max.Y && C.Z <= Max.Z)
		{
			MarkChunkDirty(FGFVoxelGrid::CellToChunk(C));
			It.RemoveCurrent();
		}
	}
}

void AGFVoxelWorld::ClearAllPaint()
{
	for (const TPair<FIntVector, FColor>& Pair : PaintedCells)
	{
		MarkChunkDirty(FGFVoxelGrid::CellToChunk(Pair.Key));
	}
	PaintedCells.Reset();
}

bool AGFVoxelWorld::GetCellPaint(const FIntVector& Cell, FLinearColor& OutColor) const
{
	if (const FColor* Found = PaintedCells.Find(Cell))
	{
		OutColor = FLinearColor(*Found);
		return true;
	}
	return false;
}

int32 AGFVoxelWorld::CountDrawnInstances(const FIntVector& ChunkCoord, FName BlockId, bool bPainted, const FLinearColor& Color) const
{
	const TTuple<FIntVector, FName, uint32> Key(ChunkCoord, BlockId, bPainted ? PaintKeyOf(Color) : 0u);
	const UInstancedStaticMeshComponent* const* Found = IsmIndex.Find(Key);
	return Found && *Found ? (*Found)->GetInstanceCount() : 0;
}

UMaterialInterface* AGFVoxelWorld::GetDrawnMaterial(const FIntVector& ChunkCoord, FName BlockId, bool bPainted, const FLinearColor& Color) const
{
	const TTuple<FIntVector, FName, uint32> Key(ChunkCoord, BlockId, bPainted ? PaintKeyOf(Color) : 0u);
	const UInstancedStaticMeshComponent* const* Found = IsmIndex.Find(Key);
	return Found && *Found ? (*Found)->GetMaterial(0) : nullptr;
}

void AGFVoxelWorld::RebuildChunkMeshes(const FIntVector& ChunkCoord)
{
	using FMeshKey = TTuple<FName, uint32>;
	TMap<FMeshKey, TArray<FTransform>> ByType;
	const FIntVector Origin = ChunkCoord * GFChunkSize;
	if (Grid.HasChunk(ChunkCoord))
	{
		for (int32 i = 0; i < GFChunkVolume; ++i)
		{
			const FIntVector Cell = Origin + FIntVector(i % GFChunkSize, (i / GFChunkSize) % GFChunkSize, i / (GFChunkSize * GFChunkSize));
			const FName Id = Grid.Get(Cell);
			if (Id.IsNone())
			{
				continue;
			}
			bool bBottomPivot = false;
			ResolveDrawMesh(Id, bBottomPivot);
			const FVector MeshPivotOffset = bBottomPivot ? FVector::ZeroVector : FVector(0, 0, GFBlockSize * 0.5f);
			const FColor* Paint = PaintedCells.Find(Cell);
			const uint32 PaintKey = Paint ? Paint->ToPackedRGBA() : 0u;
			ByType.FindOrAdd(FMeshKey(Id, PaintKey)).Emplace(FRotator::ZeroRotator, FVector(Cell.X + 0.5f, Cell.Y + 0.5f, Cell.Z) * GFBlockSize + MeshPivotOffset);
		}
	}
	for (TPair<TTuple<FIntVector, FName, uint32>, UInstancedStaticMeshComponent*>& Pair : IsmIndex)
	{
		if (Pair.Key.Get<0>() == ChunkCoord && !ByType.Contains(FMeshKey(Pair.Key.Get<1>(), Pair.Key.Get<2>())) && Pair.Value->GetInstanceCount() > 0)
		{
			Pair.Value->ClearInstances();
		}
	}
	for (TPair<FMeshKey, TArray<FTransform>>& Pair : ByType)
	{
		UInstancedStaticMeshComponent* Ism = GetOrCreateIsm(ChunkCoord, Pair.Key.Get<0>(), Pair.Key.Get<1>());
		Ism->ClearInstances();
		Ism->AddInstances(Pair.Value, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);
	}
}
