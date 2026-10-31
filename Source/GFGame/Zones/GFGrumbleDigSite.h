#pragma once

// Greybox Grumble Dig on the existing porch. The world layout director spawns one site at the porch center.
// L_GrumbleDig is not a level: the arch teleports onto the lobby, Start runs a trench, Finish asks the dig
// planner for one stair sample, and the Hub arch returns to the Rime stand. Nothing here edits voxels.

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "GFGrumbleDigSite.generated.h"

class APawn;

UENUM()
enum class EGFGrumbleDigPhase : uint8
{
	Idle,
	Running,
	Results
};

UCLASS()
class GFGAME_API AGFGrumbleDigSite : public AActor
{
	GENERATED_BODY()

public:
	AGFGrumbleDigSite();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** One site per world. Moves an existing site onto Origin. Authority only; clients keep the replicated actor. */
	static AGFGrumbleDigSite* Ensure(UWorld* World, const FVector& Origin);
	static AGFGrumbleDigSite* Get(UWorld* World);

	void BuildIfNeeded();

	/** HUD radius (cm). The porch is far from the Rime stand, so the hub readout stays off. */
	bool IsPlayerNear(const APawn* Pawn) const;

	/** Where the arch puts a pawn: in front of Start, clear of the porch gate box. */
	FVector GetLobbyStand() const;
	/** Rime stand. The player never leaves L_Hub. */
	FVector GetHubReturnLocation() const;

	UBoxComponent* GetSpot(FName Spot) const;
	int32 NumSpots() const { return Spots.Num(); }

	bool IsRunning() const { return Phase == EGFGrumbleDigPhase::Running; }
	bool HasResults() const { return bHasResults; }
	int32 GetCellsDug() const { return CellsDug; }
	int32 GetStoneCount() const { return StoneCount; }
	float GetStepRiserCm() const { return StepRiserCm; }
	float GetAppliedPitchDegrees() const { return AppliedPitchDegrees; }
	FName GetYieldItem() const { return YieldItem; }

	/** Server. Gate entry. Does not start the run. */
	bool ReceiveFromGate(APawn* Pawn);
	/** Server. Idle or a finished board can start. A run already going does not restart. */
	bool TryStart(APawn* Pawn);
	/** Server. One dig sample, then the results board. */
	bool TryFinish(APawn* Pawn);
	/** Server. Teleport to the hub stand. The board stays until the next Start. */
	bool TryReturn(APawn* Pawn);

	void GetPromptLines(TArray<FString>& OutLines) const;

	UPROPERTY(Replicated)
	FString LastPrompt;

	static constexpr float NearRadiusCm = 3500.f;
	/** Matches the no-PlayerStart hub stand. */
	static constexpr float HubStandZCm = 120.f;

protected:
	UFUNCTION()
	void OnRep_Ready();

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bReady = false;

	UPROPERTY(Replicated)
	EGFGrumbleDigPhase Phase = EGFGrumbleDigPhase::Idle;

	UPROPERTY(Replicated)
	bool bHasResults = false;

	UPROPERTY(Replicated)
	int32 CellsDug = 0;

	UPROPERTY(Replicated)
	int32 StoneCount = 0;

	UPROPERTY(Replicated)
	float StepRiserCm = 0.f;

	UPROPERTY(Replicated)
	float AppliedPitchDegrees = 0.f;

	UPROPERTY(Replicated)
	FName YieldItem = NAME_None;

	bool bVisualsBuilt = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UFont> SignFont;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> Spots;

	TArray<FName> SpotNames;
	TArray<TSet<TWeakObjectPtr<APawn>>> SpotInside;

	void BuildVisuals();
	void AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock);
	void AddLabel(const FString& Text, const FVector& Local);
	UBoxComponent* AddSpot(FName Spot, const FVector& Local);
	void AddArch(const FString& Title, const FVector& Local, const FLinearColor& Color);
	void PollSpots();
	bool Accepts(const APawn* Pawn) const;
	void ClearBoard();
};
