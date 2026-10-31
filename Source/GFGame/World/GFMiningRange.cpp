#include "GFMiningRange.h"

#include "GFMiningRangePortal.h"
#include "GFDataTables.h"
#include "GFGameMode.h"
#include "GFGameState.h"
#include "GFHubGateDirector.h"
#include "GFInventoryComponent.h"
#include "GFLog.h"
#include "GFPlayerState.h"
#include "GFReturnToHub.h"
#include "GFVoxelWorld.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

const FName AGFMiningRange::ModeName(TEXT("MiningRange"));
const TCHAR* AGFMiningRange::RangeMap = TEXT("/Game/Maps/L_MiningRange");
bool AGFMiningRange::bTestForceDressingFallback = false;

namespace
{
	static const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* FontPath = TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField");
	static const FColor Ink(0x2A, 0x24, 0x30);
	static const TCHAR* PropRoot = TEXT("/Game/Art/Props/MiningRange/");
	static const TCHAR* MatRoot = TEXT("/Game/Art/Props/MiningRange/Materials/");

	static FString ObjectPath(const FString& Name, const TCHAR* Root)
	{
		if (Name.IsEmpty())
		{
			return FString();
		}
		if (Name.Contains(TEXT("/")))
		{
			return Name.Contains(TEXT(".")) ? Name : Name + TEXT(".") + FPaths::GetBaseFilename(Name);
		}
		return FString::Printf(TEXT("%s%s.%s"), Root, *Name, *Name);
	}

	static UStaticMesh* LoadMesh(const FString& Name)
	{
		const FString Path = ObjectPath(Name, PropRoot);
		return Path.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Path);
	}

	static UMaterialInterface* LoadMat(const FString& Name)
	{
		const FString Path = ObjectPath(Name, MatRoot);
		return Path.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *Path);
	}

	static UStaticMesh* Cube()
	{
		return LoadObject<UStaticMesh>(nullptr, CubePath);
	}

	static void StampLayout(AGFVoxelWorld* Grid, const FGFMiningRangeLayout& Layout)
	{
		TMap<FIntVector, FName> ByRange;
		ByRange.Reserve(Layout.Cells.Num());
		for (const FGFMiningCell& Cell : Layout.Cells)
		{
			ByRange.Add(Cell.Range, Cell.Stamp);
		}
		const FIntVector Origin = Layout.Origin;
		Grid->ServerStamp([&ByRange, Origin](const FIntVector& Cell) -> FName
		{
			if (const FName* Id = ByRange.Find(Cell + Origin))
			{
				return *Id;
			}
			return NAME_None;
		});
	}

	static AGFMiningRange* FindRange(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AGFMiningRange> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static void ExecMineRange(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const FString Verb = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("go"));
		if (World->GetNetMode() == NM_Client)
		{
			APlayerController* PC = World->GetFirstPlayerController();
			AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
			if (!PS)
			{
				UE_LOG(LogGF, Warning, TEXT("Mining range: no player to ask the server"));
				return;
			}
			FGFModeRequest Request = FGFModeRequest::Make(AGFMiningRange::ModeName, FName(*Verb));
			if (Verb == TEXT("row") && Args.Num() > 1)
			{
				Request.Names.Add(FName(*Args[1]));
			}
			PS->RequestMode(Request);
			return;
		}
		if (AGFMiningRange* Range = FindRange(World))
		{
			if (Verb == TEXT("reset"))
			{
				Range->ResetAll();
			}
			else if (Verb == TEXT("row") && Args.Num() > 1)
			{
				Range->ResetRow(Args[1]);
			}
			else if (Verb == TEXT("hud"))
			{
				Range->ToggleHud();
			}
			else
			{
				Range->HandleModeRequest(nullptr, FGFModeRequest::Make(AGFMiningRange::ModeName, TEXT("go")));
			}
			return;
		}
		if (Verb == TEXT("go"))
		{
			GFTravelToMap(World, AGFMiningRange::RangeMap, TFunction<void(const FString&)>());
			return;
		}
		UE_LOG(LogGF, Warning, TEXT("Mining range: %s needs the range map"), *Verb);
	}

	static FAutoConsoleCommandWithWorldAndArgs GMineRangeCmd(
		TEXT("gf.MineRange"),
		TEXT("Mining range. No args travels there. reset rebuilds every row. row <Name> rebuilds one. hud toggles the drop counter."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExecMineRange));

	// 5x7 glyphs, top bit is the left column. Missing characters leave a gap.
	static const uint8* Glyph(TCHAR C)
	{
		static const uint8 Space[] = { 0, 0, 0, 0, 0, 0, 0 };
		static const uint8 A[] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
		static const uint8 B[] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
		static const uint8 Cc[] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
		static const uint8 D[] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
		static const uint8 E[] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
		static const uint8 F[] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
		static const uint8 G[] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E };
		static const uint8 H[] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
		static const uint8 I[] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
		static const uint8 J[] = { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
		static const uint8 K[] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
		static const uint8 L[] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
		static const uint8 M[] = { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 };
		static const uint8 N[] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
		static const uint8 O[] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
		static const uint8 P[] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
		static const uint8 R[] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
		static const uint8 S[] = { 0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E };
		static const uint8 T[] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
		static const uint8 U[] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
		static const uint8 V[] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
		static const uint8 W[] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
		static const uint8 X[] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
		static const uint8 Y[] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
		static const uint8 Z[] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
		static const uint8 D0[] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
		static const uint8 D1[] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
		static const uint8 D2[] = { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F };
		static const uint8 D3[] = { 0x1F, 0x01, 0x02, 0x06, 0x01, 0x11, 0x0E };
		static const uint8 D4[] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
		static const uint8 D5[] = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
		static const uint8 D6[] = { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
		static const uint8 D7[] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
		static const uint8 D8[] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
		static const uint8 D9[] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
		static const uint8 Dot[] = { 0, 0, 0, 0, 0, 0, 0x04 };
		static const uint8 Slash[] = { 0x01, 0x02, 0x04, 0x04, 0x08, 0x10, 0x10 };
		static const uint8 Colon[] = { 0, 0x04, 0, 0, 0x04, 0, 0 };
		static const uint8 Dash[] = { 0, 0, 0, 0x1F, 0, 0, 0 };
		static const uint8 Under[] = { 0, 0, 0, 0, 0, 0, 0x1F };
		switch (C)
		{
		case ' ': return Space;
		case 'A': return A; case 'B': return B; case 'C': return Cc; case 'D': return D; case 'E': return E;
		case 'F': return F; case 'G': return G; case 'H': return H; case 'I': return I; case 'J': return J;
		case 'K': return K; case 'L': return L; case 'M': return M; case 'N': return N; case 'O': return O;
		case 'P': return P; case 'R': return R; case 'S': return S; case 'T': return T; case 'U': return U;
		case 'V': return V; case 'W': return W; case 'X': return X; case 'Y': return Y; case 'Z': return Z;
		case '0': return D0; case '1': return D1; case '2': return D2; case '3': return D3; case '4': return D4;
		case '5': return D5; case '6': return D6; case '7': return D7; case '8': return D8; case '9': return D9;
		case '.': return Dot; case '/': return Slash; case ':': return Colon; case '-': return Dash; case '_': return Under;
		default: return nullptr;
		}
	}

	static void StampText(TArray<FColor>& Pixels, int32 Width, int32 Height, int32 X, int32 Y, const FString& Line)
	{
		const int32 Scale = 2;
		for (int32 i = 0; i < Line.Len(); ++i)
		{
			const uint8* Rows = Glyph(FChar::ToUpper(Line[i]));
			if (!Rows)
			{
				continue;
			}
			for (int32 Row = 0; Row < 7; ++Row)
			{
				for (int32 Col = 0; Col < 5; ++Col)
				{
					if ((Rows[Row] & (1 << (4 - Col))) == 0)
					{
						continue;
					}
					for (int32 Sy = 0; Sy < Scale; ++Sy)
					{
						for (int32 Sx = 0; Sx < Scale; ++Sx)
						{
							const int32 Px = X + (i * 6 + Col) * Scale + Sx;
							const int32 Py = Y + (Row * Scale) + Sy;
							if (Px >= 0 && Py >= 0 && Px < Width && Py < Height)
							{
								Pixels[Py * Width + Px] = FColor::White;
							}
						}
					}
				}
			}
		}
	}

	static bool WriteBmp(const FString& Path, const TArray<FColor>& Pixels, int32 Width, int32 Height)
	{
		if (Pixels.Num() < Width * Height || Width <= 0 || Height <= 0)
		{
			return false;
		}
		const int32 RowStride = (Width * 3 + 3) & ~3;
		TArray<uint8> File;
		File.SetNumZeroed(54 + RowStride * Height);
		File[0] = 'B';
		File[1] = 'M';
		const int32 Size = File.Num();
		FMemory::Memcpy(File.GetData() + 2, &Size, 4);
		const int32 Off = 54;
		FMemory::Memcpy(File.GetData() + 10, &Off, 4);
		const int32 Dib = 40;
		FMemory::Memcpy(File.GetData() + 14, &Dib, 4);
		FMemory::Memcpy(File.GetData() + 18, &Width, 4);
		FMemory::Memcpy(File.GetData() + 22, &Height, 4);
		const uint16 Planes = 1;
		const uint16 Bpp = 24;
		FMemory::Memcpy(File.GetData() + 26, &Planes, 2);
		FMemory::Memcpy(File.GetData() + 28, &Bpp, 2);
		uint8* Body = File.GetData() + 54;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			uint8* Row = Body + (Height - 1 - Y) * RowStride;
			for (int32 X = 0; X < Width; ++X)
			{
				const FColor& C = Pixels[Y * Width + X];
				Row[X * 3 + 0] = C.B;
				Row[X * 3 + 1] = C.G;
				Row[X * 3 + 2] = C.R;
			}
		}
		return FFileHelper::SaveArrayToFile(File, *Path);
	}
}

AGFMiningRange::AGFMiningRange()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AGFMiningRange::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFMiningRange, HudRows);
	DOREPLIFETIME(AGFMiningRange, HudGains);
	DOREPLIFETIME(AGFMiningRange, bHudOn);
	DOREPLIFETIME(AGFMiningRange, HandlePitch);
	DOREPLIFETIME(AGFMiningRange, ResetServerTime);
}

void AGFMiningRange::BeginPlay()
{
	Super::BeginPlay();
	if (!Layout.LoadProject())
	{
		UE_LOG(LogGF, Warning, TEXT("Mining range: layout did not load (%s)"), *Layout.Error);
		return;
	}
	if (bTestForceDressingFallback)
	{
		Layout.Dressing = FGFMiningDressing();
	}
	UDataTable* Table = nullptr;
	if (const AGFGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AGFGameMode>() : nullptr)
	{
		Table = FGFDataTables::Get(Mode->BlockTable, TEXT("DT_Blocks"), FGFBlockRow::StaticStruct());
	}
	if (!Table || !Table->FindRow<FGFBlockRow>(TEXT("ClayBed"), TEXT("MineRange"), false))
	{
		Table = FGFDataTables::FromCsv(TEXT("DT_Blocks"), FGFBlockRow::StaticStruct());
	}
	Layout.ApplyBlockTable(Table);
	ClearAt.Init(-1.f, Layout.Rows.Num());
	if (HasAuthority())
	{
		BuildWorld();
		EnsurePlayerStart();
		RegisterHandler();
		if (AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr)
		{
			ResetServerTime = GS->GetServerWorldTimeSeconds();
		}
		PollGains();
	}
	BuildVisuals();
	CaptureIfEditor();
}

void AGFMiningRange::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr)
		{
			GS->UnregisterModeHandler(this);
		}
		bRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}

void AGFMiningRange::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (UStaticMeshComponent* Handle = HandleMesh.Get())
	{
		Handle->SetRelativeRotation(FRotator(HandlePitch, 0.f, 0.f));
	}
	if (!HasAuthority())
	{
		return;
	}
	if (Lever != ELever::Rest)
	{
		LeverAlpha += DeltaSeconds / 0.28f;
		HandlePitch = FMath::Lerp(LeverFrom, LeverTo, FMath::Clamp(LeverAlpha, 0.f, 1.f));
		if (LeverAlpha >= 1.f)
		{
			if (Lever == ELever::Pulling)
			{
				ResetAll();
				Lever = ELever::Spring;
				LeverFrom = Layout.Dressing.Lever.bPresent ? Layout.Dressing.Lever.PulledPitch : 35.f;
				LeverTo = Layout.Dressing.Lever.bPresent ? Layout.Dressing.Lever.RestPitch : -35.f;
				LeverAlpha = 0.f;
				HandlePitch = LeverFrom;
			}
			else
			{
				Lever = ELever::Rest;
			}
		}
	}
	PollGains();
}

void AGFMiningRange::BuildWorld()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AGFVoxelWorld> It(World); It; ++It)
	{
		Voxel = *It;
		break;
	}
	const FVector Origin(Layout.Origin.X * 100.f, Layout.Origin.Y * 100.f, Layout.Origin.Z * 100.f);
	if (!Voxel.IsValid())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		AGFVoxelWorld* Spawned = World->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform(Origin), Params);
		if (!Spawned)
		{
			UE_LOG(LogGF, Warning, TEXT("Mining range: voxel world did not spawn"));
			return;
		}
		UDataTable* Table = FGFDataTables::FromCsv(TEXT("DT_Blocks"), FGFBlockRow::StaticStruct());
		if (const AGFGameMode* Mode = World->GetAuthGameMode<AGFGameMode>())
		{
			UDataTable* Loaded = FGFDataTables::Get(Mode->BlockTable, TEXT("DT_Blocks"), FGFBlockRow::StaticStruct());
			if (Loaded && Loaded->FindRow<FGFBlockRow>(TEXT("ClayBed"), TEXT("MineRange"), false))
			{
				Table = Loaded;
			}
		}
		Spawned->BlockTable = Table;
		Spawned->PlotSizeInBlocks = Layout.GridSize;
		Spawned->StarterGroundLayers = 0;
		Spawned->FinishSpawning(FTransform(Origin));
		Voxel = Spawned;
	}
	else
	{
		Voxel->SetActorLocation(Origin);
		Voxel->PlotSizeInBlocks = Layout.GridSize;
		Voxel->StarterGroundLayers = 0;
	}
	StampLayout(Voxel.Get(), Layout);
	if (AGFGameState* GS = World->GetGameState<AGFGameState>())
	{
		GS->bEnforcePlotPermissions = false;
	}
	EnsureReturnGate();
}

void AGFMiningRange::EnsureReturnGate()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	FVector Rime = FVector::ZeroVector;
	if (const AGFHubGateDirector* Hub = GetDefault<AGFHubGateDirector>())
	{
		Rime = Hub->RimeAnchor(World);
	}
	const FTransform Xform(FRotator(0.f, Layout.ReturnYaw, 0.f), Layout.ReturnCm);
	if (AGFReturnGate* Existing = AGFReturnGate::Find(World, ModeName))
	{
		Existing->Configure(ModeName, Rime, true);
		Existing->SetActorLocationAndRotation(Layout.ReturnCm, FRotator(0.f, Layout.ReturnYaw, 0.f));
		Existing->ApplyVisuals();
		return;
	}
	AGFReturnGate* Gate = World->SpawnActorDeferred<AGFReturnGate>(AGFReturnGate::StaticClass(), Xform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Gate)
	{
		return;
	}
	Gate->Configure(ModeName, Rime, true);
	Gate->FinishSpawning(Xform);
	Gate->ApplyVisuals();
}

void AGFMiningRange::EnsurePlayerStart()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector At = Layout.SpawnCm + FVector(0.f, 0.f, 120.f);
	const FRotator Face(0.f, Layout.SpawnYaw, 0.f);
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (It->PlayerStartTag == FName(TEXT("Rime")))
		{
			It->SetActorLocationAndRotation(At, Face);
			return;
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (APlayerStart* Start = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FTransform(Face, At), Params))
	{
		Start->PlayerStartTag = TEXT("Rime");
	}
}

void AGFMiningRange::RegisterHandler()
{
	if (AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr)
	{
		GS->RegisterModeHandler(this);
		bRegistered = true;
	}
}

void AGFMiningRange::BuildVisuals()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UFont* Font = LoadObject<UFont>(nullptr, FontPath);
	const bool bWantArt = Layout.Dressing.bLoaded;
	UStaticMesh* SignMesh = bWantArt && Layout.Dressing.Signs.Num() > 0 ? LoadMesh(Layout.Dressing.Signs[0].Mesh) : nullptr;
	UStaticMesh* BadgeMesh = bWantArt && Layout.Dressing.Badges.Num() > 0 ? LoadMesh(Layout.Dressing.Badges[0].Mesh) : nullptr;
	UStaticMesh* BaseMesh = bWantArt && Layout.Dressing.Lever.bPresent ? LoadMesh(Layout.Dressing.Lever.BaseMesh) : nullptr;
	UStaticMesh* HandleAsset = bWantArt && Layout.Dressing.Lever.bPresent ? LoadMesh(Layout.Dressing.Lever.HandleMesh) : nullptr;
	UStaticMesh* TrimMesh = bWantArt && Layout.Dressing.Trims.Num() > 0 ? LoadMesh(Layout.Dressing.Trims[0].Mesh) : nullptr;
	UStaticMesh* FloorMesh = bWantArt && Layout.Dressing.Tiles.Num() > 0 ? LoadMesh(Layout.Dressing.Tiles[0].Mesh) : nullptr;
	bArtDressing = bWantArt && SignMesh && BadgeMesh && BaseMesh && HandleAsset && TrimMesh && FloorMesh;
	if (bWantArt && !bArtDressing)
	{
		UE_LOG(LogGF, Warning, TEXT("Mining range: a dressing mesh did not load, using row signs and a cube lever"));
	}

	auto Own = [this](UActorComponent* Component)
	{
		if (Component)
		{
			SpawnedBits.Add(Component);
		}
		return Component;
	};
	auto TextOn = [&](USceneComponent* Parent, const FString& Line, const FVector& Rel)
	{
		UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this);
		if (!Text)
		{
			return;
		}
		Text->SetupAttachment(Parent);
		Text->SetRelativeLocation(Rel);
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetWorldSize(16.f);
		Text->SetTextRenderColor(Ink);
		Text->SetText(FText::FromString(Line));
		if (Font)
		{
			Text->SetFont(Font);
		}
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Text->RegisterComponent();
		Own(Text);
	};

	for (const FGFMiningRow& Row : Layout.Rows)
	{
		const FGFDressingSign* Dress = bArtDressing ? Layout.Dressing.FindSign(Row.Id) : nullptr;
		const FVector At = Dress ? Dress->LocationCm : Row.SignCm;
		const float Yaw = Dress ? Dress->YawDeg : Row.SignYaw;
		UStaticMeshComponent* Sign = NewObject<UStaticMeshComponent>(this);
		if (!Sign)
		{
			continue;
		}
		Sign->SetupAttachment(Root);
		Sign->SetWorldLocationAndRotation(At, FRotator(0.f, Yaw, 0.f));
		Sign->SetMobility(EComponentMobility::Movable);
		Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (bArtDressing)
		{
			Sign->SetStaticMesh(SignMesh);
		}
		else if (UStaticMesh* Box = Cube())
		{
			Sign->SetStaticMesh(Box);
			Sign->SetWorldScale3D(FVector(0.07f, 1.40f, 0.95f));
		}
		Sign->RegisterComponent();
		Own(Sign);
		++SignsBuilt;
		const FString Line = Row.Sign1 + (Row.Sign2.IsEmpty() ? FString() : TEXT("\n") + Row.Sign2) + (Row.Sign3.IsEmpty() ? FString() : TEXT("\n") + Row.Sign3);
		TextOn(Sign, Line, FVector(6.f, 0.f, 0.f));
		if (!bArtDressing && !Row.StandInMesh.IsEmpty() && Voxel.IsValid() && !Voxel->FindBlockRow(Row.Block))
		{
			if (UStaticMesh* Ore = LoadObject<UStaticMesh>(nullptr, *Row.StandInMesh))
			{
				UStaticMeshComponent* Sample = NewObject<UStaticMeshComponent>(this);
				Sample->SetupAttachment(Sign);
				Sample->SetRelativeLocation(FVector(40.f, 0.f, -40.f));
				Sample->SetRelativeScale3D(FVector(0.45f));
				Sample->SetStaticMesh(Ore);
				Sample->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Sample->RegisterComponent();
				Own(Sample);
			}
		}
		if (bArtDressing)
		{
			if (const FGFDressingBadge* Badge = Layout.Dressing.FindBadge(Row.Id))
			{
				UStaticMeshComponent* Mark = NewObject<UStaticMeshComponent>(this);
				Mark->SetupAttachment(Sign);
				Mark->SetRelativeLocation(Badge->OffsetCm);
				Mark->SetRelativeRotation(FRotator(0.f, Badge->YawDeg - Yaw, 0.f));
				Mark->SetStaticMesh(BadgeMesh);
				Mark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Mark->RegisterComponent();
				Own(Mark);
				++BadgesBuilt;
			}
		}
	}

	const bool bLeverArt = bArtDressing && Layout.Dressing.Lever.bPresent;
	const FVector LeverAt = bLeverArt ? Layout.Dressing.Lever.LocationCm : Layout.ResetCm;
	const float LeverYaw = bLeverArt ? Layout.Dressing.Lever.YawDeg : Layout.ResetYaw;
	HandlePitch = bLeverArt ? Layout.Dressing.Lever.RestPitch : -35.f;
	UStaticMeshComponent* Base = NewObject<UStaticMeshComponent>(this);
	if (Base)
	{
		Base->SetupAttachment(Root);
		Base->SetWorldLocationAndRotation(LeverAt, FRotator(0.f, LeverYaw, 0.f));
		Base->SetMobility(EComponentMobility::Movable);
		if (bLeverArt)
		{
			Base->SetStaticMesh(BaseMesh);
			Base->SetCollisionProfileName(TEXT("BlockAll"));
		}
		else if (UStaticMesh* Box = Cube())
		{
			Base->SetStaticMesh(Box);
			Base->SetWorldScale3D(FVector(0.7f));
			Base->SetCollisionProfileName(TEXT("BlockAll"));
		}
		Base->RegisterComponent();
		Own(Base);
		UStaticMeshComponent* Handle = NewObject<UStaticMeshComponent>(this);
		if (Handle)
		{
			Handle->SetupAttachment(Base);
			Handle->SetRelativeLocation(bLeverArt ? Layout.Dressing.Lever.HandleOffsetCm : FVector(0.f, 0.f, 73.f));
			Handle->SetRelativeRotation(FRotator(HandlePitch, 0.f, 0.f));
			Handle->SetMobility(EComponentMobility::Movable);
			Handle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			if (bLeverArt)
			{
				Handle->SetStaticMesh(HandleAsset);
			}
			else if (UStaticMesh* Box = Cube())
			{
				Handle->SetStaticMesh(Box);
				Handle->SetRelativeScale3D(FVector(0.18f, 0.18f, 0.7f));
			}
			Handle->RegisterComponent();
			Own(Handle);
			HandleMesh = Handle;
		}
		UBoxComponent* Trigger = NewObject<UBoxComponent>(this);
		if (Trigger)
		{
			Trigger->SetupAttachment(Base);
			Trigger->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
			Trigger->SetBoxExtent(FVector(80.f, 80.f, 90.f));
			Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
			Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
			Trigger->SetGenerateOverlapEvents(true);
			Trigger->OnComponentBeginOverlap.AddDynamic(this, &AGFMiningRange::OnResetOverlap);
			Trigger->RegisterComponent();
			Own(Trigger);
			ResetTrigger = Trigger;
		}
	}

	if (bArtDressing)
	{
		UInstancedStaticMeshComponent* Trims = NewObject<UInstancedStaticMeshComponent>(this);
		if (Trims)
		{
			Trims->SetupAttachment(Root);
			Trims->SetStaticMesh(TrimMesh);
			Trims->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Trims->RegisterComponent();
			for (const FGFDressingProp& Prop : Layout.Dressing.Trims)
			{
				Trims->AddInstance(FTransform(FRotator(0.f, Prop.YawDeg, 0.f), Prop.LocationCm));
			}
			Own(Trims);
			TrimsBuilt = Layout.Dressing.Trims.Num();
		}
		TMap<FString, UInstancedStaticMeshComponent*> Floors;
		for (const FGFDressingProp& Prop : Layout.Dressing.Tiles)
		{
			UInstancedStaticMeshComponent*& Ism = Floors.FindOrAdd(Prop.Material);
			if (!Ism)
			{
				Ism = NewObject<UInstancedStaticMeshComponent>(this);
				Ism->SetupAttachment(Root);
				Ism->SetStaticMesh(FloorMesh);
				Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				if (UMaterialInterface* Mat = LoadMat(Prop.Material))
				{
					Ism->SetMaterial(0, Mat);
				}
				Ism->RegisterComponent();
				Own(Ism);
			}
			Ism->AddInstance(FTransform(FRotator(0.f, Prop.YawDeg, 0.f), Prop.LocationCm));
			++TilesBuilt;
		}
	}

}

void AGFMiningRange::OnResetOverlap(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	(void)Overlapped; (void)OtherComp; (void)OtherBodyIndex; (void)bFromSweep; (void)Sweep;
	if (HasAuthority() && Cast<APawn>(Other) && Lever == ELever::Rest)
	{
		StartPull();
	}
}

void AGFMiningRange::StartPull()
{
	if (!HasAuthority() || Lever != ELever::Rest)
	{
		return;
	}
	Lever = ELever::Pulling;
	LeverFrom = HandlePitch;
	LeverTo = Layout.Dressing.Lever.bPresent ? Layout.Dressing.Lever.PulledPitch : 35.f;
	LeverAlpha = 0.f;
}

void AGFMiningRange::ResetAll()
{
	AGFVoxelWorld* Grid = Voxel.Get();
	if (!HasAuthority() || !Grid)
	{
		return;
	}
	StampLayout(Grid, Layout);
	for (TPair<FString, FGainTracker>& Pair : Trackers)
	{
		Pair.Value.Gained.Reset();
	}
	for (float& At : ClearAt)
	{
		At = -1.f;
	}
	if (AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr)
	{
		ResetServerTime = GS->GetServerWorldTimeSeconds();
	}
	PublishStats();
}

bool AGFMiningRange::ResetRow(const FString& RowId)
{
	AGFVoxelWorld* Grid = Voxel.Get();
	const int32 Index = Layout.RowIndexOf(RowId);
	if (!HasAuthority() || !Grid || Index == INDEX_NONE)
	{
		return false;
	}
	for (const FGFMiningCell& Cell : Layout.Cells)
	{
		if (Cell.RowIndex == Index)
		{
			Grid->ServerAuthorCell(Layout.ToGrid(Cell.Range), Cell.Stamp);
		}
	}
	if (ClearAt.IsValidIndex(Index))
	{
		ClearAt[Index] = -1.f;
	}
	PublishStats();
	return true;
}

void AGFMiningRange::ToggleHud()
{
	if (!HasAuthority())
	{
		return;
	}
	bHudOn = !bHudOn;
	ForceNetUpdate();
}

FString AGFMiningRange::PlayerLabel(const AGFPlayerState* PS) const
{
	if (!PS)
	{
		return FString();
	}
	if (!PS->PlayerKey.IsEmpty())
	{
		return PS->PlayerKey;
	}
	const FString Name = PS->GetPlayerName();
	return Name.IsEmpty() ? FString::FromInt(PS->GetPlayerId()) : Name;
}

int32 AGFMiningRange::GainedCount(const FString& PlayerKey, FName Item) const
{
	if (const FGainTracker* Found = Trackers.Find(PlayerKey))
	{
		if (const int32* Count = Found->Gained.Find(Item))
		{
			return *Count;
		}
	}
	return 0;
}

void AGFMiningRange::PollGains()
{
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	AGFVoxelWorld* Grid = Voxel.Get();
	if (!HasAuthority() || !GS || !Grid)
	{
		return;
	}
	const float Now = GS->GetServerWorldTimeSeconds();
	for (APlayerState* State : GS->PlayerArray)
	{
		AGFPlayerState* PS = Cast<AGFPlayerState>(State);
		if (!PS || !PS->Inventory)
		{
			continue;
		}
		const FString Key = PlayerLabel(PS);
		FGainTracker& Tracker = Trackers.FindOrAdd(Key);
		Tracker.Label = PS->GetPlayerName().IsEmpty() ? Key : PS->GetPlayerName();
		TSet<FName> Seen;
		if (!Tracker.bSeeded)
		{
			Tracker.bSeeded = true;
			for (const FGFItemStack& Stack : PS->Inventory->GetInventory().Items)
			{
				Tracker.Last.Add(Stack.ItemId, Stack.Count);
			}
			continue;
		}
		for (const FGFItemStack& Stack : PS->Inventory->GetInventory().Items)
		{
			Seen.Add(Stack.ItemId);
			int32& Last = Tracker.Last.FindOrAdd(Stack.ItemId);
			if (Stack.Count > Last)
			{
				Tracker.Gained.FindOrAdd(Stack.ItemId) += Stack.Count - Last;
			}
			Last = Stack.Count;
		}
		for (TPair<FName, int32>& Last : Tracker.Last)
		{
			if (!Seen.Contains(Last.Key))
			{
				Last.Value = 0;
			}
		}
	}
	for (int32 Index = 0; Index < Layout.Rows.Num(); ++Index)
	{
		const int32 Total = Layout.BreakableCount(Index);
		int32 Broken = 0;
		for (const FGFMiningCell& Cell : Layout.Cells)
		{
			if (Cell.RowIndex == Index && Cell.bBreakable && Grid->GetBlock(Layout.ToGrid(Cell.Range)).IsNone())
			{
				++Broken;
			}
		}
		if (ClearAt.IsValidIndex(Index))
		{
			if (Total > 0 && Broken >= Total)
			{
				if (ClearAt[Index] < 0.f)
				{
					ClearAt[Index] = FMath::Max(0.f, Now - ResetServerTime);
				}
			}
			else
			{
				ClearAt[Index] = -1.f;
			}
		}
	}
	PublishStats();
}

void AGFMiningRange::PublishStats()
{
	HudRows.Reset();
	for (int32 Index = 0; Index < Layout.Rows.Num(); ++Index)
	{
		FGFMiningHudRow& Row = HudRows.AddDefaulted_GetRef();
		Row.RowId = Layout.Rows[Index].Id;
		Row.Total = Layout.BreakableCount(Index);
		Row.Broken = 0;
		if (AGFVoxelWorld* Grid = Voxel.Get())
		{
			for (const FGFMiningCell& Cell : Layout.Cells)
			{
				if (Cell.RowIndex == Index && Cell.bBreakable && Grid->GetBlock(Layout.ToGrid(Cell.Range)).IsNone())
				{
					++Row.Broken;
				}
			}
		}
		Row.ClearSeconds = ClearAt.IsValidIndex(Index) ? ClearAt[Index] : -1.f;
	}
	HudGains.Reset();
	for (const TPair<FString, FGainTracker>& Pair : Trackers)
	{
		for (const TPair<FName, int32>& Gain : Pair.Value.Gained)
		{
			if (Gain.Value <= 0)
			{
				continue;
			}
			FGFMiningHudGain& Line = HudGains.AddDefaulted_GetRef();
			Line.Player = Pair.Value.Label;
			Line.Item = Gain.Key.ToString();
			Line.Count = Gain.Value;
		}
	}
	ForceNetUpdate();
}

TArray<FString> AGFMiningRange::HudLines() const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Mining Range"));
	float Now = ResetServerTime;
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GS = World->GetGameState())
		{
			Now = GS->GetServerWorldTimeSeconds();
		}
	}
	const float Span = FMath::Max(0.25f, Now - ResetServerTime);
	for (const FGFMiningHudGain& Gain : HudGains)
	{
		Lines.Add(FString::Printf(TEXT("%s  %s x%d  %.1f/s"), *Gain.Player.ToUpper(), *Gain.Item.ToUpper(), Gain.Count, Gain.Count / Span));
	}
	for (const FGFMiningHudRow& Row : HudRows)
	{
		if (Row.Total <= 0)
		{
			Lines.Add(FString::Printf(TEXT("%s  spell-proof"), *Row.RowId.ToUpper()));
			continue;
		}
		FString Line = FString::Printf(TEXT("%s  %d/%d"), *Row.RowId.ToUpper(), Row.Broken, Row.Total);
		if (Row.ClearSeconds >= 0.f)
		{
			Line += FString::Printf(TEXT("  %.1fs"), Row.ClearSeconds);
		}
		Lines.Add(Line);
	}
	return Lines;
}

void AGFMiningRange::CaptureShotNames(TArray<FString>& Out)
{
	Out.Reset();
	Out.Add(TEXT("Common"));
	Out.Add(TEXT("Verdant"));
	Out.Add(TEXT("Rime"));
	Out.Add(TEXT("Cinderwrights"));
	Out.Add(TEXT("Ore"));
	Out.Add(TEXT("Wide"));
	Out.Add(TEXT("Wide_Hud"));
}

void AGFMiningRange::CaptureIfEditor()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (GIsAutomationTesting)
	{
		return;
	}
#endif
	if (!FApp::CanEverRender() || !GFMiningMaps::IsMiningRange(GetWorld()))
	{
		return;
	}
	const FString RootDir = TEXT("C:/DevPersonal/GFGame-captures");
	if (!FPaths::DirectoryExists(RootDir))
	{
		return;
	}
	const FString Dir = RootDir / TEXT("mining-range");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const bool bWasOn = bHudOn;
	bHudOn = false;

	auto Shoot = [this, &Dir](const FString& Name, const FVector& From, const FVector& At, bool bStampHud)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		if (!Target)
		{
			return;
		}
		Target->InitAutoFormat(960, 540);
		Target->UpdateResourceImmediate(true);
		USceneCaptureComponent2D* Cap = NewObject<USceneCaptureComponent2D>(this);
		if (!Cap)
		{
			return;
		}
		Cap->SetupAttachment(Root);
		Cap->RegisterComponent();
		Cap->TextureTarget = Target;
		Cap->CaptureSource = SCS_FinalColorLDR;
		Cap->bCaptureEveryFrame = false;
		Cap->bCaptureOnMovement = false;
		Cap->FOVAngle = 70.f;
		Cap->SetWorldLocationAndRotation(From, (At - From).Rotation());
		Cap->CaptureScene();
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		if (FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource())
		{
			Resource->ReadPixels(Pixels);
		}
		if (bStampHud)
		{
			int32 Y = 16;
			for (const FString& Line : HudLines())
			{
				StampText(Pixels, 960, 540, 16, Y, Line);
				Y += 18;
			}
		}
		if (!WriteBmp(Dir / (Name + TEXT(".bmp")), Pixels, 960, 540))
		{
			UE_LOG(LogGF, Warning, TEXT("Mining range: capture %s was not written"), *Name);
		}
		Cap->DestroyComponent();
	};

	for (const FGFMiningCam& Cam : Layout.Cams)
	{
		const bool bWide = Cam.Name.Equals(TEXT("Wide"), ESearchCase::IgnoreCase);
		Shoot(bWide ? TEXT("Wide") : Cam.Name, Cam.LocationCm, Cam.LookAtCm, false);
		if (bWide)
		{
			bHudOn = true;
			PublishStats();
			Shoot(TEXT("Wide_Hud"), Cam.LocationCm, Cam.LookAtCm, true);
		}
	}
	bHudOn = true;
	if (!bWasOn)
	{
		bHudOn = false;
	}
	ForceNetUpdate();
}

FGFModeResult AGFMiningRange::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request)
{
	const FString Verb = Request.Verb.ToString().ToLower();
	if (Verb == TEXT("reset"))
	{
		ResetAll();
		return FGFModeResult::Ok();
	}
	if (Verb == TEXT("row"))
	{
		const FString Id = Request.Names.Num() > 0 ? Request.Names[0].ToString() : FString();
		return ResetRow(Id) ? FGFModeResult::Ok() : FGFModeResult::Fail(TEXT("NoSuchRow"));
	}
	if (Verb == TEXT("hud"))
	{
		ToggleHud();
		return FGFModeResult::Ok();
	}
	if (APawn* Pawn = By ? By->GetPawn() : nullptr)
	{
		Pawn->SetActorLocationAndRotation(Layout.SpawnCm + FVector(0.f, 0.f, 120.f), FRotator(0.f, Layout.SpawnYaw, 0.f));
		return FGFModeResult::Ok();
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			Pawn->SetActorLocationAndRotation(Layout.SpawnCm + FVector(0.f, 0.f, 120.f), FRotator(0.f, Layout.SpawnYaw, 0.f));
		}
	}
	return FGFModeResult::Ok();
}

FName AGFMiningRange::EditBlockReason(const AGFPlayerState* By, const FVector& Location) const
{
	(void)By;
	const AGFVoxelWorld* Grid = Voxel.Get();
	if (!Grid)
	{
		return NAME_None;
	}
	const FIntVector RangeCell = Grid->WorldToCell(Location) + Layout.Origin;
	for (const FGFMiningCell& Cell : Layout.Cells)
	{
		if (Cell.Range == RangeCell)
		{
			if (Cell.bSpellProof)
			{
				return TEXT("SpellProof");
			}
			if (Cell.bProtected)
			{
				return TEXT("Protected");
			}
			return NAME_None;
		}
	}
	return NAME_None;
}
