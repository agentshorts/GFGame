#include "GFGameMode.h"
#include "GFCharacter.h"
#include "GFPlayerController.h"
#include "GFCraftingSubsystem.h"
#include "GFVoxelWorld.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFDataTables.h"
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
	DefaultPawnClass = AGFCharacter::StaticClass();
	PlayerControllerClass = AGFPlayerController::StaticClass();
	GameStateClass = AGFGameState::StaticClass();
	PlayerStateClass = AGFPlayerState::StaticClass();
	bDelayedStart = false;
	StartupDirectorClasses.Add(FSoftClassPath(TEXT("/Script/GFSocial.GFSocialDirector")));
}

void AGFGameMode::InitGameState()
{
	Super::InitGameState();
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

AGFVoxelWorld* AGFGameMode::EnsureVoxelWorld()
{
	for (TActorIterator<AGFVoxelWorld> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	const FIntVector Size(64, 64, 32);
	const int32 Ground = 1;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	const FVector Origin(-Size.X * 50.f, -Size.Y * 50.f, -Ground * 100.f);
	AGFVoxelWorld* Voxel = GetWorld()->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform(Origin), Params);
	if (Voxel)
	{
		Voxel->BlockTable = FGFDataTables::Get(BlockTable, TEXT("DT_Blocks"), FGFBlockRow::StaticStruct());
		Voxel->PlotSizeInBlocks = Size;
		Voxel->StarterGroundLayers = Ground;
		Voxel->FinishSpawning(FTransform(Origin));
		UE_LOG(LogGF, Log, TEXT("Spawned hub voxel world (block table: %s)"), *GetNameSafe(Voxel->BlockTable));
	}
	return Voxel;
}

AActor* AGFGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	EnsureVoxelWorld();
	AActor* Found = Super::ChoosePlayerStart_Implementation(Player);
	if (Found)
	{
		return Found;
	}
	if (!FallbackStart)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FallbackStart = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FTransform(FVector(0.f, 0.f, 120.f)), Params);
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
