#pragma once

// Playable Hallow Night on the hub Depths pad. The hub gate already drops a player on this pad.
// Engine cubes mark a short lane (the Z / X / C course drawn small) plus Start, Finish, Results
// and a return arch. The real run still goes through AGFDepthsDirector (Enter, exit drop, LiftHome).
// There is no new map: the gate tests keep Depths as a sim.

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "GFItemTypes.h"
#include "GFModeTypes.h"
#include "GFDepthsSite.generated.h"

class UFont;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class AGFPlayerState;
class APawn;

UCLASS()
class GFDEPTHS_API AGFDepthsSite : public AActor
{
	GENERATED_BODY()

public:
	AGFDepthsSite();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Lane, spots and labels. Safe to call again. Authority and clients both build their own meshes. */
	void BuildIfNeeded();

	static AGFDepthsSite* Get(UWorld* World);

	/** HUD radius (cm). The pad is 40 m across; the Rime stand is much farther than this. */
	bool IsPlayerNear(const APawn* Pawn) const;

	/** Where the return arch puts a player: the Rime anchor, plus the no-PlayerStart stand height. */
	FVector GetHubReturnLocation() const;

	int32 NumSpots() const { return TriggerVerbs.Num(); }
	bool HasVerb(FName Verb) const;
	UBoxComponent* GetVerbTrigger(FName Verb) const;

	/** Server. Enter the next depth (1, then 2, then 3). Mode verb "Enter". */
	FGFModeResult TryStart(APawn* Pawn);
	/**
	 * Server. Presents the runner at the exit, lets the sim roll the exit drop, then LiftHome.
	 * That is what clears the depth. A lift from the entrance would not.
	 */
	FGFModeResult TryFinish(APawn* Pawn);
	/** Server. Refreshes the results line. Not a mode verb. */
	FGFModeResult TryResults(APawn* Pawn);
	/**
	 * Server. Teleports back to the hub stand.
	 * An open run is Leave first, so walking home cannot leave the player stuck inside it.
	 */
	bool TryReturn(APawn* Pawn);

	/** Lines for the HUD. Always names Start, Finish, Results and Hub. */
	void GetPromptLines(const AGFPlayerState* PS, TArray<FString>& OutLines) const;

	/** Last spot result, replicated so a client HUD can show it. */
	UPROPERTY(Replicated)
	FString LastPrompt;

	UPROPERTY(Replicated)
	bool bResultsReady = false;

	UPROPERTY(Replicated)
	int32 LastDepthFinished = 0;

	UPROPERTY(Replicated)
	int32 LastDepthsReached = 0;

	UPROPERTY(Replicated)
	TArray<FGFItemStack> LastBanked;

	static constexpr float NearRadiusCm = 4500.f;
	/** Matches the no-PlayerStart hub stand (FGFPlayerStartRules::FallbackZ). */
	static constexpr float HubStandZCm = 120.f;

protected:
	UFUNCTION()
	void OnRep_Ready();

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bReady = false;

	bool bVisualsBuilt = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UFont> SignFont;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> Triggers;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Runner;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> ResultsBoard;

	TArray<FName> TriggerVerbs;
	/** Pawns already handled for this stay, one set per trigger. */
	TArray<TSet<TWeakObjectPtr<APawn>>> SpotInside;

	void BuildVisuals();
	void AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock);
	void AddLabel(const FString& Text, const FVector& Local, float WorldSize = 42.f);
	UBoxComponent* AddTrigger(FName Verb, const FVector& Local, const FVector& Extent);
	void AddArch(const FString& Title, FName Verb, const FVector& At, const FLinearColor& Color);
	void PollSpots();
	void UpdateRunner();
	void UpdateBoard();
	AGFPlayerState* PlayerOf(APawn* Pawn) const;
	/** Depth is sent only for Enter. */
	FGFModeResult Send(AGFPlayerState* PS, FName Verb, int32 Depth);
	FString ResultsLine() const;
	int32 NextDepth(const AGFPlayerState* PS) const;
};
