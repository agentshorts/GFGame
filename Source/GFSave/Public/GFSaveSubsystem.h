#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFSaveSubsystem.generated.h"

class UGFSaveGame;
class USaveGame;
class UGFCharacterSave;
class UGameInstance;

/**
 * Two saves (PLAN.md sections 2a, 4.1):
 *  - the world save (host only): blocks, garden, guests (and later plots, rent, mail, blueprints);
 *  - the character save (every machine): your inventory, companions, Field Journal, blueprints.
 * Writes go to a temp file then swap, keeping the previous file as .bak (PLAN.md section 7).
 */
UCLASS()
class GFSAVE_API UGFSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Host: world to SlotName (+ the host's own character). Guest: only the character. */
	UFUNCTION(BlueprintCallable, Category = "GF|Save")
	bool SaveToSlot(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "GF|Save")
	bool LoadFromSlot(const FString& SlotName);

	UFUNCTION(BlueprintCallable, Category = "GF|Save")
	bool SaveCharacter(const FString& SlotName = TEXT("Character"));

	UFUNCTION(BlueprintCallable, Category = "GF|Save")
	bool LoadCharacter(const FString& SlotName = TEXT("Character"));

	/** Pure-ish halves, used by tests. World: server only. */
	static UGFSaveGame* CaptureWorld(UWorld* World);
	static bool ApplyToWorld(UWorld* World, const UGFSaveGame* Save);
	/** This machine's character: the live player state if we're the host, else the local profile. */
	static UGFCharacterSave* CaptureCharacter(UGameInstance* GameInstance);
	static bool ApplyCharacter(UGameInstance* GameInstance, const UGFCharacterSave* Save);

	static bool Migrate(UGFSaveGame* Save);
	static FString SlotPath(const FString& SlotName);
	static bool WriteAtomic(USaveGame* Save, const FString& SlotName);
	static USaveGame* ReadSlot(const FString& SlotName);
};
