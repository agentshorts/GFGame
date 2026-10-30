#include "GFGameMode.h"
#include "GFCharacter.h"
#include "GFHUD.h"
#include "GFFerrySpawn.h"
#include "GFPlayerController.h"
#include "GFCraftingSubsystem.h"
#include "GFVoxelWorld.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFSpawnDefaults.h"
#include "GFTerritorySubsystem.h"
#include "GFDataTables.h"
#include "GFOverworldBuild.h"
#include "GFOverworldGround.h"
#include "GFRecipeTypes.h"
#include "GFLog.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameSession.h"
#include "TimerManager.h"

AGFGameMode::AGFGameMode()
{
	StarterFaction = FGFSpawnDefaults::StarterFactionId();
	DefaultPawnClass = AGFCharacter::StaticClass();
	PlayerControllerClass = AGFPlayerController::StaticClass();
	HUDClass = AGFHUD::StaticClass();
	DefaultSpawnAnchor = TEXT("Rime");
	GameStateClass = AGFGameState::StaticClass();
	PlayerStateClass = AGFPlayerState::StaticClass();
	bDelayedStart = false;
	StartupDirectorClasses.Add(FSoftClassPath(TEXT("/Script/GFSocial.GFSocialDirector")));
	StartupDirectorClasses.Add(FSoftClassPath(TEXT("/Script/GFGame.GFPaintVolume")));
}

void AGFGameMode::InitGameState()
{
	Super::InitGameState();
	if (AGFGameState* GS = GetGameState<AGFGameState>())
	{
		GS->StarterFaction = StarterFaction;
	}
	for (const FSoftClassPath& Path : StartupDirectorClasses)
	{
		UClass* Class = Path.TryLoadClass<AActor>();
		if (!Class)
		{
			UE_LOG(LogGF, Warning, TEXT("Net: startup director %s not found"), *Path.ToString());
			continue;
		}
		bool bExists = false;
		for (TActorIterator<AActor> It(GetWorld(), Class); It && !bExists; ++It)
		{
			bExists = true;
		}
		if (!bExists)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			GetWorld()->SpawnActor<AActor>(Class, FTransform::Identity, Params);
		}
	}
}

void AGFGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	const AGFGameState* GS = GetGameState<AGFGameState>();
	const FString Key = UniqueId.IsValid() ? UniqueId->ToString() : FString();
	FString Why;
	if (ErrorMessage.IsEmpty() && GS && !Key.IsEmpty() && GS->IsJoinRefused(Key, Why))
	{
		ErrorMessage = Why.IsEmpty() ? FString(TEXT("You can't join this world.")) : Why;
		UE_LOG(LogGF, Log, TEXT("Net: refused %s at PreLogin (%s)"), *Key, *ErrorMessage);
	}
}

void AGFGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	AGFPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AGFPlayerState>() : nullptr;
	AGFGameState* GS = GetGameState<AGFGameState>();
	if (!PS || !GS)
	{
		return;
	}
	const FUniqueNetIdRepl& NetId = PS->GetUniqueId();
	FString Key = NetId.IsValid() ? NetId->ToString() : FString();
	if (Key.IsEmpty() || GS->FindPlayerByKey(Key))
	{
		Key = FString::Printf(TEXT("%s#%d"), *PS->GetPlayerName(), PS->GetPlayerId());
	}
	PS->PlayerKey = Key;
	PS->bIsHost = NewPlayer->IsLocalController();
	// Ban check on the final key too (PreLogin only sees the raw net id; this also catches the "Name#id" fallback).
	FString Why;
	if (!PS->bIsHost && GS->IsJoinRefused(Key, Why))
	{
		UE_LOG(LogGF, Log, TEXT("Net: refused %s at login (%s)"), *Key, *Why);
		const FText Reason = FText::FromString(Why.IsEmpty() ? FString(TEXT("You can't join this world.")) : Why);
		TWeakObjectPtr<APlayerController> Weak(NewPlayer);
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Weak, Reason]()
		{
			if (APlayerController* PC = Weak.Get(); PC && GameSession)
			{
				GameSession->KickPlayer(PC, Reason);
			}
		}));
		return;
	}
	// Colour: keep the remembered one, else the first free slot.
	TSet<int32> Used;
	for (APlayerState* Other : GS->PlayerArray)
	{
		if (Other != PS)
		{
			Used.Add(CastChecked<AGFPlayerState>(Other)->ColorIndex);
		}
	}
	if (const FGFGuestRecord* Rec = GS->GuestRecords.Find(Key); Rec && !Used.Contains(Rec->ColorIndex))
	{
		PS->ColorIndex = Rec->ColorIndex;
	}
	else
	{
		int32 Slot = 0;
		while (Used.Contains(Slot)) { ++Slot; }
		PS->ColorIndex = Slot;
	}
	PS->ForceNetUpdate();
	PS->UploadLocalCharacterIfNeeded(); // host / standalone: the local profile goes straight in
	UE_LOG(LogGF, Log, TEXT("Net: login %s (host=%d colour=%d)"), *Key, PS->bIsHost ? 1 : 0, PS->ColorIndex);
}

void AGFGameMode::Logout(AController* Exiting)
{
	if (AGFPlayerState* PS = Exiting ? Exiting->GetPlayerState<AGFPlayerState>() : nullptr)
	{
		PS->SnapshotToGuestRecord();
	}
	Super::Logout(Exiting);
}

void AGFGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGFCraftingSubsystem* Crafting = GI->GetSubsystem<UGFCraftingSubsystem>())
		{
			Crafting->RecipeTable = FGFDataTables::Get(RecipeTable, TEXT("DT_Recipes"), FGFRecipeRow::StaticStruct());
		}
	}
}

void AGFGameMode::BindOverworldEditQuery()
{
	if (!AGFOverworldGround::IsVoxelOverworldMap(GetWorld()))
	{
		return;
	}
	AGFGameState* GS = GetGameState<AGFGameState>();
	if (!GS || GS->OverworldEditQuery.IsBound())
	{
		return;
	}
	TWeakObjectPtr<AGFGameMode> Weak(this);
	GS->OverworldEditQuery.BindLambda([Weak](FName FactionId, const FVector& Where)
	{
		const AGFGameMode* Mode = Weak.Get();
		const UWorld* World = Mode ? Mode->GetWorld() : nullptr;
		UGFTerritorySubsystem* Territory = World ? UGFTerritorySubsystem::Get(World) : nullptr;
		if (!Territory)
		{
			return EGFOverworldEdit::Deny;
		}
		Territory->GetZoneAt(Where);
		return FGFOverworldBuild::Classify(Territory->GetMap(), FactionId, Where);
	});
}

AGFVoxelWorld* AGFGameMode::EnsureVoxelWorld()
{
	for (TActorIterator<AGFVoxelWorld> It(GetWorld()); It; ++It)
	{
		BindOverworldEditQuery();
		return *It;
	}

	const bool bHub = AGFOverworldGround::IsVoxelOverworldMap(GetWorld());
	FGFOverworldLayout Layout;
	FIntVector Size(64, 64, 32);
	int32 Ground = 1;
	FVector Origin(-Size.X * 50.f, -Size.Y * 50.f, -static_cast<float>(Ground) * 100.f);
	if (bHub)
	{
		const UGFTerritorySubsystem* Territory = GetDefault<UGFTerritorySubsystem>();
		const AGFOverworldGround* GroundDefaults = GetDefault<AGFOverworldGround>();
		const float Margin = GroundDefaults ? GroundDefaults->Margin : FGFOverworldBuild::DefaultMarginCm;
		Layout = Territory
			? FGFOverworldBuild::Make(Territory->Curve, Territory->FactionLayout, Territory->Hazards, Margin)
			: FGFOverworldBuild::Make({}, {}, {}, Margin);
		Size = Layout.GridSize;
		Ground = 0;
		Origin = Layout.Origin;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	AGFVoxelWorld* Voxel = GetWorld()->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform(Origin), Params);
	if (Voxel)
	{
		UDataTable* Table = FGFDataTables::Get(BlockTable, TEXT("DT_Blocks"), FGFBlockRow::StaticStruct());
		if (bHub && (!Table || !Table->FindRow<FGFBlockRow>(FName(TEXT("Ice")), TEXT("GFOverworld"), false)))
		{
			Table = FGFDataTables::FromCsv(TEXT("DT_Blocks"), FGFBlockRow::StaticStruct());
		}
		Voxel->BlockTable = Table;
		Voxel->PlotSizeInBlocks = Size;
		Voxel->StarterGroundLayers = Ground;
		Voxel->FinishSpawning(FTransform(Origin));
		if (bHub)
		{
			const FGFOverworldLayout Stamp = Layout;
			Voxel->ServerStamp([&Stamp](const FIntVector& Cell) { return Stamp.BlockAt(Cell); });
		}
		UE_LOG(LogGF, Log, TEXT("Spawned %s voxel world %dx%dx%d (block table: %s)"),
			bHub ? TEXT("overworld") : TEXT("starter"), Size.X, Size.Y, Size.Z, *GetNameSafe(Voxel->BlockTable));
	}
	BindOverworldEditQuery();
	return Voxel;
}

AActor* AGFGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	EnsureVoxelWorld();

	TArray<APlayerStart*> Starts;
	TArray<FGFMapPlayerStart> Listed;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		FGFMapPlayerStart Row;
		Row.Tag = It->PlayerStartTag;
		Row.Location = It->GetActorLocation();
		Listed.Add(Row);
		Starts.Add(*It);
	}
	const int32 Index = FGFPlayerStartRules::ChooseStartIndex(Listed, DefaultSpawnAnchor);
	if (Index != INDEX_NONE)
	{
		const bool bTagged = !DefaultSpawnAnchor.IsNone() && Starts[Index]->PlayerStartTag == DefaultSpawnAnchor;
		if (bTagged)
		{
			return Starts[Index];
		}
		if (AActor* Found = Super::ChoosePlayerStart_Implementation(Player))
		{
			return Found;
		}
		return Starts[Index];
	}

	// No PlayerStart. This is the starter anchor centre plus 120 cm, the same point as
	// FGFSpawnDefaults::StarterFallback. DefaultSpawnAnchor stays Rime, the same id as StarterFaction.
	// The ferry perch (EmptyMapStart, (0, -300, 100), yaw 36) stays off this path.
	TArray<FGFTerritoryFaction> Layout;
	if (const UGFTerritorySubsystem* Live = UGFTerritorySubsystem::Get(this))
	{
		Layout = Live->FactionLayout;
	}
	else if (const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>())
	{
		Layout = Defaults->FactionLayout;
	}
	const FVector Where = FGFPlayerStartRules::FallbackLocation(DefaultSpawnAnchor, Layout);
	if (!FallbackStart)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FallbackStart = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FTransform(Where), Params);
	}
	else
	{
		FallbackStart->SetActorLocation(Where);
	}
	return FallbackStart;
}

APawn* AGFGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	int32 Index = 0;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Index += It->GetPlayerState() ? 1 : 0;
	}
	FVector Loc = SpawnTransform.GetLocation() + FVector((Index % 4) * 150.f, (Index / 4) * 150.f, 0.f);
	if (AGFVoxelWorld* Voxel = EnsureVoxelWorld())
	{
		FIntVector Cell = Voxel->WorldToCell(Loc);
		int32 Top = -1;
		for (int32 Z = Voxel->PlotSizeInBlocks.Z - 1; Z >= 0; --Z)
		{
			if (!Voxel->GetBlock(FIntVector(Cell.X, Cell.Y, Z)).IsNone())
			{
				Top = Z;
				break;
			}
		}
		Cell.Z = Top + 1;
		Loc.Z = FMath::Max(Loc.Z, Voxel->CellToWorld(Cell).Z + 100.f);
	}
	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	return GetWorld()->SpawnActor<APawn>(PawnClass, FTransform(SpawnTransform.GetRotation(), Loc), Params);
}
