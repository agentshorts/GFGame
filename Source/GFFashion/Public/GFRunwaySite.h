#pragma once

// Playable fashion show on L_Runway. The hub gate already opens that map and the director
// already places AGFRunwayStage. This actor builds the greybox (theme, look, start, finish, hub)
// out of engine cubes and drives the stage. Scores and rewards stay on the stage.

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "GFRunwaySite.generated.h"

class UFont;
class UMaterialInterface;
class UStaticMesh;
class AGFPlayerState;
class APawn;

UCLASS()
class GFFASHION_API AGFRunwaySite : public AActor
{
	GENERATED_BODY()

public:
	AGFRunwaySite();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Spots and labels. Safe to call again. Authority and clients both build their own meshes. */
	void BuildIfNeeded();

	static AGFRunwaySite* Get(UWorld* World);

	/** HUD radius (cm). The catwalk is shorter than this. */
	bool IsPlayerNear(const APawn* Pawn) const;

	bool HasVerb(FName Verb) const;
	UBoxComponent* GetVerbTrigger(FName Verb) const;
	UBoxComponent* GetThemeTrigger(FName ThemeId) const;
	UBoxComponent* GetLookTrigger(FName LookId) const;
	int32 NumThemeSpots() const;
	int32 NumLookSpots() const;

	FName GetSelectedTheme() const { return SelectedTheme; }
	FName GetSelectedLook() const { return SelectedLook; }

	/** Server. Theme and look can be picked before the show. Start opens it. Finish scores it. */
	bool TrySelectTheme(APawn* Pawn, FName ThemeId);
	bool TrySelectLook(APawn* Pawn, FName LookId);
	bool TryStart(APawn* Pawn);
	bool TryFinish(APawn* Pawn);
	/** Server. Opens the hub map. A travel override records the map instead (tests). */
	bool TryReturn(APawn* Pawn);

	void SetTravelOverride(TFunction<void(const FString&)> InOverride) { TravelOverride = MoveTemp(InOverride); }
	const FString& GetLastTravelMap() const { return LastTravelMap; }
	bool HasTravelled() const { return bTravelled; }

	/**
	 * Lines for the HUD. Always names Start, Finish and Hub.
	 * LastPrompt is the last spot result.
	 */
	void GetPromptLines(const AGFPlayerState* PS, TArray<FString>& OutLines) const;

	UPROPERTY(Replicated)
	FString LastPrompt;

	/** Phase lengths the next Start writes onto the stage. The stage's own defaults stay until then. */
	static constexpr float PlayDressingSeconds = 20.f;
	static constexpr float PlayWalkSeconds = 6.f;
	static constexpr float PlayJudgingSeconds = 2.f;

	/** Results stay up this long (s) before everyone goes back to the hub. The hub arch leaves sooner. */
	float ResultsSeconds = 8.f;

	static constexpr float NearRadiusCm = 4500.f;

protected:
	UFUNCTION()
	void OnRep_Ready();

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bReady = false;

	UPROPERTY(Replicated)
	FName SelectedTheme;

	UPROPERTY(Replicated)
	FName SelectedLook;

	bool bVisualsBuilt = false;
	bool bTravelled = false;
	double ResultsShownAt = -1.0;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UFont> SignFont;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> Triggers;

	TArray<FName> TriggerVerbs;
	TArray<FName> TriggerNames;
	TArray<TSet<TWeakObjectPtr<APawn>>> SpotInside;
	TSet<TWeakObjectPtr<APawn>> Seated;

	FString LastTravelMap;
	TFunction<void(const FString&)> TravelOverride;

	void BuildVisuals();
	void AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock);
	void AddLabel(const FString& Text, const FVector& Local);
	UBoxComponent* AddTrigger(FName Verb, FName Name, const FVector& Local, const FVector& Extent);
	void PollSpots();
	void SeatFarPawns();
	void TravelToHub();
	AGFPlayerState* PlayerOf(APawn* Pawn) const;
	bool ApplyLook(const FString& PlayerKey);
};
