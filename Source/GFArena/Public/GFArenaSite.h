#pragma once

// Greybox Dino Arena on the hub sim pad. The hub gate already drops a player on this pad.
// Engine cubes mark Start, Finish, Results and a return arch back to the Rime stand.
// The bout itself is the existing Arena mode (AutoPlace, NoWager, Fight). No new map.

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "GFModeTypes.h"
#include "GFArenaSite.generated.h"

class UFont;
class UMaterialInterface;
class UStaticMesh;
class AGFPlayerState;
class AGFArenaDirector;
class APawn;
class ACameraActor;

UCLASS()
class GFARENA_API AGFArenaSite : public AActor
{
	GENERATED_BODY()

public:
	AGFArenaSite();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Spots and labels. Safe to call again. Authority and clients both build their own meshes. */
	void BuildIfNeeded();

	static AGFArenaSite* Get(UWorld* World);

	/** HUD radius (cm). The pad is 40 m across; the Rime stand is much farther than this. */
	bool IsPlayerNear(const APawn* Pawn) const;

	/** Where the return arch puts a player: the Rime anchor, plus the no-PlayerStart stand height. */
	FVector GetHubReturnLocation() const;

	bool HasVerb(FName Verb) const;
	UBoxComponent* GetVerbTrigger(FName Verb) const;

	/** Server. Empty roster gets one sparring dino, then AutoPlace, NoWager and Fight. */
	FGFModeResult TryStart(APawn* Pawn);
	/** Server. Runs the current bout to its real outcome. */
	FGFModeResult TryFinish(APawn* Pawn);
	/** Server. Reads the finished entry. Does not roll rewards again. */
	FGFModeResult TryResults(APawn* Pawn);
	/** Server. Teleports a player pawn back to the hub stand. Not a mode verb. */
	bool TryReturn(APawn* Pawn);

	/**
	 * Lines for the HUD. Always names Start, Finish, Results and Return.
	 * LastPrompt is the last spot result.
	 */
	void GetPromptLines(const AGFPlayerState* PS, TArray<FString>& OutLines) const;

	/** Last spot result, replicated so a client HUD can show it. */
	UPROPERTY(Replicated)
	FString LastPrompt;

	/** Server-spawned, replicated. Clients build the balcony mesh themselves and watch through this camera. */
	UPROPERTY(Replicated)
	TObjectPtr<ACameraActor> BalconyCamera;

	static constexpr float NearRadiusCm = 4500.f;
	/** Matches the no-PlayerStart hub stand (FGFPlayerStartRules::FallbackZ). */
	static constexpr float HubStandZCm = 120.f;

	/** Greybox balcony. The camera sits above the south rail and looks at the sand. Local to the site. */
	static FVector BalconyCameraLocal();
	static FVector BalconyLookLocal();
	static FRotator BalconyCameraRotation();
	static FVector BalconyStandLocal();

	ACameraActor* GetBalconyCamera() const { return BalconyCamera; }
	/** Points a local controller at the balcony camera. False when there is no controller. */
	bool ApplyBalconyView(APawn* Pawn);
	void ReleaseBalconyView(APawn* Pawn);

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

	TArray<FName> TriggerVerbs;
	/** Pawns already handled for this stay, one set per trigger. */
	TArray<TSet<TWeakObjectPtr<APawn>>> SpotInside;

	void BuildVisuals();
	void AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock);
	void AddLabel(const FString& Text, const FVector& Local);
	UBoxComponent* AddTrigger(FName Verb, const FVector& Local, const FVector& Extent);
	void AddArch(const FString& Title, FName Verb, const FVector& At, const FLinearColor& Color);
	void PollSpots();
	AGFPlayerState* PlayerOf(APawn* Pawn) const;
	AGFArenaDirector* Director() const;
	FName LoanerSpeciesId() const;
	bool EnsureLoaner(AGFPlayerState* PS);
	FGFModeResult Send(AGFPlayerState* PS, FName Verb);
};
