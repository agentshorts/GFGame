#pragma once

// Server owner of The Elder's Errand. Listens for FGFQuestEvents, keeps one line per player key,
// copies step / count / rank onto the player state, and saves the lines in the world mode blob "Quest"
// (UGFSaveSubsystem gathers that blob with the other mode states). No save version bump.
// Also owns the side quest board (QL-a): one FGFSideQuestLine per player key, fed by FGFSideQuestEvents, saved
// in the same blob (FGFQuestSave::SideBoards), with the Active side line copied onto the player state.
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFModeTypes.h"
#include "GFQuestLine.h"
#include "GFSideQuestLine.h"
#include "GFQuestSubsystem.generated.h"

class AGFPlayerState;
enum class EGFElderKind : uint8;

DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnQuestLeader, const FString& /*PlayerKey*/, FName /*FactionId*/);

UCLASS()
class GFGAME_API UGFQuestSubsystem : public UWorldSubsystem, public IGFModeHandler
{
	GENERATED_BODY()

public:
	static const FName ModeName;

	static UGFQuestSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual FName GetModeName() const override { return ModeName; }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual void OnModePlayerReady(AGFPlayerState* PS) override;
	virtual void CaptureModeState(TArray<uint8>& Out) const override;
	virtual void RestoreModeState(const TArray<uint8>& In) override;

	/** Registers with the game state once it exists. Safe to call more than once. */
	void EnsureRegistered();
	/** Server. Applies one event for its player key. Empty keys are ignored. */
	FGFQuestApplyResult ApplyEvent(const FGFQuestEvent& Event);
	const FGFQuestProgress* FindProgress(const FString& PlayerKey) const;
	const FGFQuestCatalog& GetCatalog() const { return Catalog; }

	/** Server. Applies one side quest event for its player key. Empty keys are ignored. Standing comes from the errand. */
	FGFSideQuestApplyResult ApplySideEvent(const FGFSideQuestEvent& Event);
	const FGFSideQuestBoard* FindSideBoard(const FString& PlayerKey) const;
	const FGFSideQuestCatalog& GetSideCatalog() const { return SideCatalog; }

	/**
	 * Rank Leader was recorded for this player.
	 * ServerSetFactionLeader has already run for this win. A player who is not a Champion of the faction does not take the seat.
	 */
	FGFOnQuestLeader OnQuestLeader;

private:
	void OnQuestEvent(UWorld* World, const FGFQuestEvent& Event);
	void OnSideQuestEvent(UWorld* World, const FGFSideQuestEvent& Event);
	/** Board for a key, with the catalog and the errand standing copied in. Creates an empty board. */
	FGFSideQuestLine& SideLineFor(const FString& PlayerKey);
	/** Re-derives Locked / Available after the errand standing moved. */
	void RefreshSide(const FString& PlayerKey);
	void PostSide(EGFElderKind Kind, const FString& PlayerKey, FName LineId) const;
	void OnFactionLeaderSet(FName FactionId, const FString& PlayerKey, const FString& PreviousKey, FName Reason);
	FGFQuestLine& LineFor(const FString& PlayerKey);
	void PushPlayer(AGFPlayerState* PS) const;
	void PushAllPlayers() const;

	FGFQuestCatalog Catalog;
	TMap<FString, FGFQuestLine> Lines;
	FGFSideQuestCatalog SideCatalog;
	TMap<FString, FGFSideQuestLine> SideLines;
	FDelegateHandle EventHandle;
	FDelegateHandle SideEventHandle;
	FDelegateHandle LeaderHandle;
};
