#pragma once

#include "CoreMinimal.h"
#include "GFItemTypes.h"
#include "GFDinoTypes.h"
#include "GFCompanionRoster.h"
#include "GFLandTypes.h"
#include "GFModeTypes.h"
#include "GFCrosshairTypes.h"
#include "GFNetTypes.generated.h"

/**
 * Everything that belongs to a player rather than to a world (PLAN.md sections 2a, 4.1): their inventory,
 * companions, Field Journal and plot blueprints. Each machine keeps its own copy (UGFCharacterSave); a guest
 * uploads it on join and the host writes it back after changes. Revision is bumped by every write-back, so on
 * rejoin the newer of (client copy, host's cached copy) wins and nothing can be spent twice.
 */
USTRUCT(BlueprintType)
struct GFNET_API FGFCharacterData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FString PlayerKey;
	UPROPERTY(SaveGame) FString DisplayName;
	UPROPERTY(SaveGame) int32 Revision = 0;
	/** False for a brand new character: the server hands out the starter kit once. */
	UPROPERTY(SaveGame) bool bInitialized = false;
	UPROPERTY(SaveGame) FGFInventory Inventory;
	UPROPERTY(SaveGame) TArray<FGFCompanionEntry> Companions;
	UPROPERTY(SaveGame) FGFDiscoveredDinoPreferences Journal;
	/** Plot backups from every host world this player has lived in (HostWorldId + PlotId). */
	UPROPERTY(SaveGame) TArray<FGFPlotBlueprint> Blueprints;
	/** Mode progress: pinned recipe, wager setting, arena formation, first tame, ... */
	UPROPERTY(SaveGame) FGFPlayerProgress Progress;
	/** Crosshair editor. A fresh character keeps the original white plus. */
	UPROPERTY(SaveGame) FGFCrosshairSettings Crosshair;
	/** Hold-to-aim, sensitivity slider, sniper scope. */
	UPROPERTY(SaveGame) FGFAimUserSettings AimUser;
};

/** What the host remembers about someone who has visited (server only, saved with the world). */
USTRUCT()
struct GFNET_API FGFGuestRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FString PlayerKey;
	UPROPERTY(SaveGame) FString DisplayName;
	UPROPERTY(SaveGame) int32 ColorIndex = 0;
	UPROPERTY(SaveGame) FDateTime LastSeen;
	UPROPERTY(SaveGame) bool bHasCharacter = false;
	/** The server's copy at logout; wins over an older upload on rejoin. */
	UPROPERTY(SaveGame) FGFCharacterData LastCharacter;
};
