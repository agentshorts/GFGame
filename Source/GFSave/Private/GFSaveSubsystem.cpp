#include "GFSaveSubsystem.h"
#include "GFSaveGame.h"
#include "GFLog.h"
#include "GFVoxelWorld.h"
#include "GFGardenPlot.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFLocalProfileSubsystem.h"
#include "VoxelFinishVolume.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString UGFSaveSubsystem::SlotPath(const FString& SlotName)
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".sav"));
}

UGFSaveGame* UGFSaveSubsystem::CaptureWorld(UWorld* World)
{
	UGFSaveGame* Save = NewObject<UGFSaveGame>();
	Save->SavedAt = FDateTime::Now();
	if (!World)
	{
		return Save;
	}
	for (TActorIterator<AGFVoxelWorld> It(World); It; ++It)
	{
		Save->Voxels = It->SaveData();
		break; // one hub grid
	}
	for (TActorIterator<AVoxelFinishVolume> It(World); It; ++It)
	{
		Save->Paint = It->GetSession().ToSaveData();
		break; // one paint volume, on the hub grid
	}
	for (TActorIterator<AGFGardenPlot> It(World); It; ++It)
	{
		Save->Plots.Add(It->ToSaveData());
	}
	if (const AGFGameState* GS = World->GetGameState<AGFGameState>())
	{
		Save->HostWorldId = GS->HostWorldId;
		TMap<FString, FGFGuestRecord> Records = GS->GuestRecords;
		// People still here: the server's live copy of their character.
		for (APlayerState* PS : GS->PlayerArray)
		{
			const AGFPlayerState* GPS = Cast<AGFPlayerState>(PS);
			if (GPS && GPS->bCharacterReady && !GPS->PlayerKey.IsEmpty())
			{
				FGFGuestRecord& Rec = Records.FindOrAdd(GPS->PlayerKey);
				Rec.PlayerKey = GPS->PlayerKey;
				Rec.DisplayName = GPS->GetPlayerName();
				Rec.ColorIndex = GPS->ColorIndex;
				Rec.LastSeen = FDateTime::UtcNow();
				Rec.bHasCharacter = true;
				Rec.LastCharacter = GPS->BuildCharacterData();
			}
		}
		Records.GenerateValueArray(Save->Guests);
		Save->Guests.Sort([](const FGFGuestRecord& A, const FGFGuestRecord& B) { return A.PlayerKey < B.PlayerKey; });
		Save->Ledger = GS->Ledger;
		Save->Ledger.PruneClosedEscrows();
		Save->Land = GS->Land;
		Save->Mail = GS->Mail;
		Save->PackedBlueprints = GS->PackedBlueprints;
		Save->ModeStates = GS->GatherModeStates();
	}
	return Save;
}

bool UGFSaveSubsystem::ApplyToWorld(UWorld* World, const UGFSaveGame* Save)
{
	if (!World || !Save)
	{
		return false;
	}
	bool bOk = true;
	for (TActorIterator<AGFVoxelWorld> It(World); It; ++It)
	{
		bOk &= It->LoadData(Save->Voxels);
		break;
	}
	// Paint after the blocks: cells that are not solid blocks any more load unpainted.
	for (TActorIterator<AVoxelFinishVolume> It(World); It; ++It)
	{
		It->AuthorityRestore(Save->Paint);
		break;
	}
	// Plots: reuse existing actors in order, spawn the rest. TODO(M1): match by stable id.
	TArray<AGFGardenPlot*> Existing;
	for (TActorIterator<AGFGardenPlot> It(World); It; ++It)
	{
		Existing.Add(*It);
	}
	for (int32 i = 0; i < Save->Plots.Num(); ++i)
	{
		AGFGardenPlot* Plot = Existing.IsValidIndex(i) ? Existing[i] : World->SpawnActor<AGFGardenPlot>(AGFGardenPlot::StaticClass(), Save->Plots[i].Transform);
		if (Plot)
		{
			Plot->FromSaveData(Save->Plots[i]);
		}
	}
	if (AGFGameState* GS = World->GetGameState<AGFGameState>())
	{
		if (!Save->HostWorldId.IsEmpty())
		{
			GS->HostWorldId = Save->HostWorldId;
		}
		GS->GuestRecords.Reset();
		for (const FGFGuestRecord& Rec : Save->Guests)
		{
			GS->GuestRecords.Add(Rec.PlayerKey, Rec);
		}
		GS->Ledger = Save->Ledger;
		GS->Ledger.RebuildIndex();
		// A wager can't survive a host restart: both stakes go home (PLAN.md M6 "a disconnect or quit refunds both").
		GS->Ledger.RefundOpen(GS->Accounts(), TEXT("Wager"));
		// Land: tenants, rent and statuses come back exactly; rent never removes anything, so nothing to undo.
		if (Save->Land.Plots.Num() > 0)
		{
			GS->Land = Save->Land;
		}
		GS->Mail = Save->Mail;
		GS->PackedBlueprints = Save->PackedBlueprints;
		GS->RestoreModeStates(Save->ModeStates);
		for (APlayerState* PS : GS->PlayerArray)
		{
			const AGFPlayerState* GPS = Cast<AGFPlayerState>(PS);
			if (GPS && GPS->bIsHost && !GPS->bLoggedOut)
			{
				GS->ServerSetLandlord(GPS->PlayerKey);
			}
		}
		GS->MarkLandChanged();
		GS->ServerRefreshAllMailboxes();
	}
	if (Save->bHasLegacyHostCharacter)
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UGFLocalProfileSubsystem* Profile = GI->GetSubsystem<UGFLocalProfileSubsystem>(); Profile && !Profile->GetCharacter().bInitialized)
			{
				Profile->SetCharacter(Save->LegacyHostCharacter);
			}
		}
	}
	return bOk;
}

UGFCharacterSave* UGFSaveSubsystem::CaptureCharacter(UGameInstance* GI)
{
	UGFCharacterSave* Save = NewObject<UGFCharacterSave>();
	Save->SavedAt = FDateTime::Now();
	if (!GI)
	{
		return Save;
	}
	if (const UGFLocalProfileSubsystem* Profile = GI->GetSubsystem<UGFLocalProfileSubsystem>())
	{
		Save->Character = Profile->GetCharacter();
	}
	// Host / standalone: the live player state is the freshest copy.
	if (const UWorld* World = GI->GetWorld())
	{
		const APlayerController* PC = GI->GetFirstLocalPlayerController(World);
		const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
		if (PS && PS->HasAuthority() && PS->bCharacterReady)
		{
			Save->Character = PS->BuildCharacterData();
		}
	}
	return Save;
}

bool UGFSaveSubsystem::ApplyCharacter(UGameInstance* GI, const UGFCharacterSave* Save)
{
	UGFLocalProfileSubsystem* Profile = GI ? GI->GetSubsystem<UGFLocalProfileSubsystem>() : nullptr;
	if (!Profile || !Save)
	{
		return false;
	}
	Profile->SetCharacter(Save->Character);
	return true;
}

bool UGFSaveSubsystem::Migrate(UGFSaveGame* Save)
{
	if (!Save)
	{
		return false;
	}
	if (Save->SaveVersion > static_cast<int32>(EGFSaveVersion::Latest))
	{
		UE_LOG(LogGF, Error, TEXT("Save is from a newer version (%d)"), Save->SaveVersion);
		return false;
	}
	if (Save->SaveVersion < static_cast<int32>(EGFSaveVersion::WorldCharacterSplit))
	{
		// v1 was single-player: its inventory and companions become the host's character.
		Save->bHasLegacyHostCharacter = true;
		Save->LegacyHostCharacter.bInitialized = true;
		Save->LegacyHostCharacter.Inventory = Save->Inventory;
		Save->LegacyHostCharacter.Companions = Save->Companions;
		Save->LegacyHostCharacter.Journal = Save->DinoPreferences;
		Save->Inventory = FGFInventory();
		Save->Companions.Reset();
		Save->DinoPreferences = FGFDiscoveredDinoPreferences();
		Save->SaveVersion = static_cast<int32>(EGFSaveVersion::WorldCharacterSplit);
	}
	// v2-v5 -> v6: nothing to convert. Paint is empty in those saves, so the world loads with no paint.
	// v6 -> v7: crosshair lives on the character save, not the world. Old characters load the default plus.
	Save->SaveVersion = static_cast<int32>(EGFSaveVersion::Latest);
	return true;
}

bool UGFSaveSubsystem::WriteAtomic(USaveGame* Save, const FString& SlotName)
{
	TArray<uint8> Bytes;
	if (!Save || !UGameplayStatics::SaveGameToMemory(Save, Bytes))
	{
		return false;
	}
	const FString Final = SlotPath(SlotName);
	const FString Temp = Final + TEXT(".tmp");
	const FString Backup = Final + TEXT(".bak");
	if (!FFileHelper::SaveArrayToFile(Bytes, *Temp))
	{
		return false;
	}
	IFileManager& FM = IFileManager::Get();
	if (FM.FileExists(*Final))
	{
		FM.Move(*Backup, *Final, /*bReplace*/ true);
	}
	return FM.Move(*Final, *Temp, true);
}

USaveGame* UGFSaveSubsystem::ReadSlot(const FString& SlotName)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *SlotPath(SlotName)))
	{
		return nullptr;
	}
	return UGameplayStatics::LoadGameFromMemory(Bytes);
}

bool UGFSaveSubsystem::SaveToSlot(const FString& SlotName)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	bool bOk = SaveCharacter();
	if (World && World->GetNetMode() != NM_Client)
	{
		UGFSaveGame* Save = CaptureWorld(World);
		Save->SlotDisplayName = SlotName;
		bOk &= WriteAtomic(Save, SlotName);
	}
	return bOk;
}

bool UGFSaveSubsystem::LoadFromSlot(const FString& SlotName)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	LoadCharacter();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	UGFSaveGame* Save = Cast<UGFSaveGame>(ReadSlot(SlotName));
	if (!Migrate(Save))
	{
		return false;
	}
	return ApplyToWorld(World, Save);
}

bool UGFSaveSubsystem::SaveCharacter(const FString& SlotName)
{
	return WriteAtomic(CaptureCharacter(GetGameInstance()), SlotName);
}

bool UGFSaveSubsystem::LoadCharacter(const FString& SlotName)
{
	return ApplyCharacter(GetGameInstance(), Cast<UGFCharacterSave>(ReadSlot(SlotName)));
}
