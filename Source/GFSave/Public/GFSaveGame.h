#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GFItemTypes.h"
#include "GFVoxelTypes.h"
#include "GFGardenTypes.h"
#include "GFDinoTypes.h"
#include "GFCompanionRoster.h"
#include "GFNetTypes.h"
#include "GFEconomy.h"
#include "GFSaveGame.generated.h"

/** Save versions (PLAN.md section 7). Add one per format change and a step in UGFSaveSubsystem::Migrate. */
UENUM()
enum class EGFSaveVersion : int32
{
	Initial = 1,
	/** Host world / per-player character split (PLAN.md sections 2a, 4.1). */
	WorldCharacterSplit = 2,
	/** Transfer ledger (open escrows, recent transactions, applied ids) saved with the world. */
	EconomyLedger = 3,
	/** Land plots (tenants, rent), mail and her copies of packed-up blueprints. */
	LandAndMail = 4,
	/** Mini-game mode state (mine rooms, crews, depots, ...) as per-mode blobs. */
	ModeStates = 5,

	// keep last
	VersionPlusOne,
	Latest = VersionPlusOne - 1
};

/**
 * The host's world (PLAN.md section 2a: "the world is the host's"): blocks, garden, who has visited, and
 * (later steps) land plots, rent, mail and packed blueprints. Player-owned things are in UGFCharacterSave.
 */
UCLASS()
class GFSAVE_API UGFSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame) int32 SaveVersion = static_cast<int32>(EGFSaveVersion::Latest);
	UPROPERTY(SaveGame) FDateTime SavedAt;
	UPROPERTY(SaveGame) FString SlotDisplayName;
	UPROPERTY(SaveGame) float PlaytimeSeconds = 0.f;

	UPROPERTY(SaveGame) FString HostWorldId;
	UPROPERTY(SaveGame) FGFVoxelSaveData Voxels;
	UPROPERTY(SaveGame) TArray<FGFPlotSaveData> Plots;
	/** Everyone who has visited, with the server's copy of their character at logout / save time. */
	UPROPERTY(SaveGame) TArray<FGFGuestRecord> Guests;
	/** v3: the economy ledger. Open wager escrows are refunded on load; mail escrows wait to be claimed. */
	UPROPERTY(SaveGame) FGFTransferLedger Ledger;
	/** v4: plots, tenants and rent (an empty plot list = default layout on load). */
	UPROPERTY(SaveGame) FGFLandState Land;
	/** v4: every letter; unclaimed ones still hold their items in the ledger's mail escrows. */
	UPROPERTY(SaveGame) TArray<FGFMailLetter> Mail;
	UPROPERTY(SaveGame) TArray<FGFPlotBlueprint> PackedBlueprints;
	/** v5: persistent mini-game state by mode name (e.g. "Mine"). */
	UPROPERTY(SaveGame) TMap<FName, FGFModeBlob> ModeStates;

	// --- v1 fields (single-player save). Read only by the v1 -> v2 migration. ---
	UPROPERTY(SaveGame) FGFInventory Inventory;
	UPROPERTY(SaveGame) TArray<FGFCompanionEntry> Companions;
	UPROPERTY(SaveGame) FGFDiscoveredDinoPreferences DinoPreferences;

	/** Set by the v1 migration: the old single-player data, to become the host's character. */
	UPROPERTY(Transient) bool bHasLegacyHostCharacter = false;
	UPROPERTY(Transient) FGFCharacterData LegacyHostCharacter;
};

/** One machine's own character (inventory, companions, Field Journal, blueprints). Travels between worlds. */
UCLASS()
class GFSAVE_API UGFCharacterSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame) int32 SaveVersion = static_cast<int32>(EGFSaveVersion::Latest);
	UPROPERTY(SaveGame) FDateTime SavedAt;
	UPROPERTY(SaveGame) FGFCharacterData Character;
};
