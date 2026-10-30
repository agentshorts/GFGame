#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "GFGameMode.generated.h"

class UDataTable;
class AGFVoxelWorld;

/**
 * Hub game mode. Makes any map playable: spawns the voxel plot (64x64x32, centred on the origin,
 * ground top at Z=0) and a start point if the map has none, and hands the data tables to the systems.
 * Listen-server ready (PLAN.md section 4.1): AGFGameState / AGFPlayerState, player keys and colours at login,
 * the server keeps a guest's character at logout.
 */
UCLASS(Config = Game)
class GFGAME_API AGFGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AGFGameMode();

	UPROPERTY(Config, EditAnywhere, Category = "GF|Data") TSoftObjectPtr<UDataTable> BlockTable;
	UPROPERTY(Config, EditAnywhere, Category = "GF|Data") TSoftObjectPtr<UDataTable> RecipeTable;
	UPROPERTY(Config, EditAnywhere, Category = "GF|Data") TSoftObjectPtr<UDataTable> PlantTable;
	UPROPERTY(Config, EditAnywhere, Category = "GF|Data") TSoftObjectPtr<UDataTable> DinoDietTable;

	/**
	 * Always-on server actors spawned with the game state (by class path, so this module needn't depend on theirs).
	 * Default: the social director (safe-zone powers, jail, kick / ban, emotes).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "GF|Net") TArray<FSoftClassPath> StartupDirectorClasses;

	UFUNCTION(BlueprintCallable, Category = "GF")
	AGFVoxelWorld* EnsureVoxelWorld();

protected:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	/** Spreads players out and lifts each one above the top solid block in its column, so a built-up hub never blocks a join. */
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	/** Refuses banned players (by their net id) before they load in. */
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void InitGameState() override;
	virtual void Logout(AController* Exiting) override;

private:
	UPROPERTY(Transient) TObjectPtr<AActor> FallbackStart;
};
