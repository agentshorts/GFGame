#pragma once
// Acceleration Hop tutorial director (#348). Lives on L_AccelHop_Tutorial_v2: the map script places one, and the hub
// director spawns one if it is missing (PlacedClass of the AccelHopTutorial side gate).
// - Listens to judged hop tile landings on this machine (UGFCharacterMovementComponent::OnAccelHopLanded) and runs the
//   pure lesson rules (FGFAccelHopTutorialRules) for the local player, so every player has their own prompts.
// - Walking onto a later lesson's stretch skips ahead to it.
// - Puts a second HUB return gate at the finish (FinishGateLocation, set by the map script), after the canonical one.
// - Opts the map into stamina, so sprint works like it does on the world road.
// - Plays the hop FX on this machine only (fx_hooks_v1): a boost burst at the feet on an applied lit or Boost landing,
//   a pop on the lit tile, and one idle shimmer attached to the road's lit tile. Skipped on a dedicated server.
// The HUD (AGFHUD::DrawAccelHopTutorial) draws GetPrompt.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFAccelHopTutorialRules.h"
#include "GFAccelHopTutorial.generated.h"

class AGFAccelHopRoad;
class AGFAccelHopTile;
class APawn;
class UNiagaraComponent;
class UNiagaraSystem;
class UGFCharacterMovementComponent;
struct FGFAccelHopLandingEvent;

UCLASS()
class GFGAME_API AGFAccelHopTutorial : public AActor
{
	GENERATED_BODY()

public:
	AGFAccelHopTutorial();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The tutorial in World, or null. */
	static AGFAccelHopTutorial* Find(const UWorld* World);

	/** Tag on the finish return gate (the canonical one by the marker stays untagged). */
	static FName FinishGateTag();

	/** Server: the HUB gate at the end of the run. No-op until the canonical return gate exists, or with no finish set. */
	void PlaceFinishGate();

	/** Feeds one landing to the local player's progress (the delegate and the tests call this). True on a lesson change. */
	bool ApplyLanding(const FGFAccelHopTutorialLanding& Landing);

	/** The landing as the lesson rules see it: the tile's lesson tag and its side of the road. */
	FGFAccelHopTutorialLanding DescribeLanding(const AGFAccelHopTile* Tile, EGFJudgement Judgement, bool bLit) const;

	/** Re-reads the tiles: each lesson's first tile along the road. BeginPlay and the first tick call it. */
	void RefreshLessons();

	/** The local player walked to Along (cm along the road). Skips forward to that lesson. */
	bool ReachAlong(float Along);

	const FGFAccelHopTutorialProgress& GetProgress() const { return Progress; }
	FGFAccelHopTutorialPrompt GetPrompt() const { return FGFAccelHopTutorialRules::PromptFor(Progress); }
	const TArray<float>& GetLessonStarts() const { return LessonStartAlong; }

	/** Soft paths of the hop FX systems (/Game/FX/AccelHop). */
	static const TCHAR* BoostFXPath();
	static const TCHAR* LitPopFXPath();
	static const TCHAR* LitIdleFXPath();

	/** Loads a Niagara system by soft path, or null (logs once per path) when the package is missing. */
	static UNiagaraSystem* LoadFXSystem(const TCHAR* SystemPath);

	/** Spawns the landing FX for Pawn (HandleLanded calls this for the local pawn). Nothing unless Event.bApplied. */
	void PlayLandingFX(const APawn* Pawn, const FGFAccelHopLandingEvent& Event);

	/** Niagara systems this director has spawned (landing bursts and idle shimmers). */
	int32 GetFXSpawnCount() const { return FXSpawnCount; }

	/** The idle shimmer on the lit tile, or null. */
	UNiagaraComponent* GetLitIdleFX() const { return LitIdleFX.Get(); }

	/** World seconds of the last lesson change (the HUD flashes "Nice!" for a moment), or a large negative. */
	double GetLastAdvanceTime() const { return LastAdvanceTime; }

	/** Where the finish HUB gate stands. The map script sets both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop|Tutorial")
	bool bHasFinishGate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop|Tutorial")
	FVector FinishGateLocation = FVector::ZeroVector;

	/** Show the prompt panel. Off hides it (a cutscene, a photo mode). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop|Tutorial")
	bool bShowPrompts = true;

private:
	void HandleLanded(const UGFCharacterMovementComponent* Movement, const FGFAccelHopLandingEvent& Event);
	AGFAccelHopRoad* FindRoad() const;
	bool LocalPawnLocation(FVector& Out) const;
	void NoteChange(bool bChanged);
	bool PlaysFX() const;
	void UpdateLitIdleFX();

	FGFAccelHopTutorialProgress Progress;
	TArray<float> LessonStartAlong;
	double LastAdvanceTime = -1.0e9;
	float NextReachCheck = 0.f;
	FDelegateHandle LandedHandle;
	TWeakObjectPtr<AGFAccelHopRoad> Road;
	/** The road's lit tile the idle shimmer is on, and the shimmer (at most one). */
	TWeakObjectPtr<AGFAccelHopTile> LitIdleTile;
	TWeakObjectPtr<UNiagaraComponent> LitIdleFX;
	int32 FXSpawnCount = 0;
};
