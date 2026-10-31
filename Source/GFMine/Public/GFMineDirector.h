#pragma once

// Server side of the Pebble Mine (PLAN.md M8). Players talk to it through AGFPlayerState::RequestMode (Mode "Mine").
// Each player's crew lives in their saved progress (Progress.Crew) and only works while they're in the world;
// when they come back the crew's offline haul is added (capped). Hauls that reach home go into their bag.
//
// Verbs:
//   Capture  Floats = [approach speed cm/s]   (needs their first tame; > 200 startles it, it hides 5 s)
//   Assign   Ids = [pebbling], Names = [room]     Haul  Ids = [pebbling] (empty = everyone)     Recall Ids = [pebbling]
//   Buy      Names = [upgrade]  (materials burned)  -> Ints = [new level]
//   Status   -> Stacks = last offline haul, Ints = [offline minutes]
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "GFMineTypes.h"
#include "GFModeTypes.h"
#include "GFMineDirector.generated.h"

class AGFGameState;

UCLASS()
class GFMINE_API AGFMineDirector : public AActor, public IGFModeHandler
{
	GENERATED_BODY()

public:
	AGFMineDirector();

	static const FName ModeName;
	static AGFMineDirector* Get(UWorld* World, bool bSpawn = true);

	virtual FName GetModeName() const override { return ModeName; }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual void OnModePlayerReady(AGFPlayerState* PS) override;
	virtual void OnModePlayerLeft(AGFPlayerState* PS) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Crew time multiplier (tests fast-forward). */
	float SimSpeed = 1.f;
	/** Tests: how long a startled pebbling hides. */
	float HideSeconds = FGFMineRules::HideSeconds;
	/** Tests: the UTC clock (0 = real). */
	int64 UtcNowTicksOverride = 0;

	int64 UtcNowTicks() const;
	/** Applies the offline haul since Crew.LastSeenUtcTicks and restarts the clock. */
	FGFMineOffline CollectOffline(AGFPlayerState* PS);

	/** Spawns the playable rooms and verb spots on this pad (#216). Safe to call again. */
	void EnsurePlayableSite();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	TMap<FString, double> HiddenUntil;
	FRandomStream Rng;
	float SeenTimer = 0.f;

	AGFGameState* GS() const;
	static FName CodeOf(EGFMineOrder O);
};
