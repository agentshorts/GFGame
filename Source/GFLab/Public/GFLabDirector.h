#pragma once

// Server side of Overtime, the after-hours lab. Players talk to it through AGFPlayerState::RequestMode (Mode "Lab").
// Verbs:
//   Enter                       start a shift (one per player; friends run their own at the same time)
//   Search   Ints = [pile]      search a junk pile (takes its SearchSeconds; one at a time)
//   Contain  Ints = [anomaly]   contain a live anomaly (takes its ContainSeconds; not while searching)
//   Extract                     walk out before lockdown with everything -> Stacks = what came home
//   Craft    Names = [recipe]   Lab Bench recipe from DT_Recipes (Station LabBench) -> Stacks = [output]
// Lockdown (end of overtime) and leaving/dropping mid-shift bank 75 %. Anomaly drops sit in Unbanked, so they
// follow the same rule. Live anomalies get a tinted engine cube (no art, no damage).
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFLabTypes.h"
#include "GFModeTypes.h"
#include "GFLabDirector.generated.h"

class AGFGameState;

UCLASS()
class GFLAB_API AGFLabDirector : public AActor, public IGFModeHandler
{
	GENERATED_BODY()

public:
	AGFLabDirector();

	static const FName ModeName;
	static AGFLabDirector* Get(UWorld* World, bool bSpawn = true);

	virtual FName GetModeName() const override { return ModeName; }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual void OnModePlayerLeft(AGFPlayerState* PS) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Lab")
	TArray<FGFLabRun> Runs;

	const FGFLabRun* FindRun(const FString& Key) const { return Runs.FindByPredicate([&](const FGFLabRun& R) { return R.Key == Key; }); }

	/** Tests: shift clock multiplier, fixed seed, and direct edits. */
	float SimSpeed = 1.f;
	int32 ForcedSeed = 0;
	FGFLabRun* EditRunForTests(const FString& Key) { return Runs.FindByPredicate([&](const FGFLabRun& R) { return R.Key == Key; }); }

	/** Server: last shift result per player (what came home and how it ended), for the UI and tests. */
	TMap<FString, TArray<FGFItemStack>> LastBanked;
	TMap<FString, EGFLabEnd> LastEnd;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	AGFGameState* GS() const;
	TArray<FGFItemStack> EndRun(const FString& Key, EGFLabEnd How);
	FGFModeResult Craft(AGFPlayerState* By, FName RecipeId);
	void SyncAnomalyMarkers(const FGFLabRun& Run);
	void DestroyAnomalyMarkers(const FString& Key);
	static FName CodeOf(EGFLabOrder O);

	struct FAnomalyMarkerRef
	{
		FString Key;
		int32 Index = INDEX_NONE;
		TWeakObjectPtr<AActor> Actor;
	};
	TArray<FAnomalyMarkerRef> AnomalyMarkers;
};
