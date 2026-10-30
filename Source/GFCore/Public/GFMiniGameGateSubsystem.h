#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFItemTypes.h"
#include "GFMiniGameGateSubsystem.generated.h"

/** Result a mini-game hands back to the hub when a run ends. */
USTRUCT(BlueprintType)
struct GFCORE_API FGFMiniGameResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MiniGame")
	int32 Score = 0;

	/** Reward tier row name in the mode's rewards table (e.g. S/A/B/C, Win/Loss). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MiniGame")
	FName Tier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MiniGame")
	TArray<FGFItemStack> Rewards;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGFOnMiniGameFinished, FName, GateId, const FGFMiniGameResult&, Result);

/**
 * Base for the map gates (taming, fashion, MOBA, Beat Hop). A gate owns entering its map
 * from the hub and queueing rewards that the hub grants on return. Stub: no reward tables yet.
 * Depths, the dino arena, the mine and the lab are hub sims with pads, not subclasses of this.
 */
UCLASS(Abstract)
class GFCORE_API UGFMiniGameGateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "GF|MiniGame")
	virtual FName GetGateId() const PURE_VIRTUAL(UGFMiniGameGateSubsystem::GetGateId, return NAME_None;);

	/** Map the gate loads, e.g. /Game/Maps/L_Valley. */
	UFUNCTION(BlueprintPure, Category = "GF|MiniGame")
	virtual FName GetMapName() const PURE_VIRTUAL(UGFMiniGameGateSubsystem::GetMapName, return NAME_None;);

	/** Sign text. Beat Hop stays "Beat Hop". The platformer is Depths, and it is not this name. */
	virtual FText GetDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "GF|MiniGame")
	virtual bool IsUnlocked() const { return true; }

	/** Opens the mini-game map. The hub must be saved first (PLAN.md section 4); hook OnBeforeEnter for that. */
	UFUNCTION(BlueprintCallable, Category = "GF|MiniGame")
	virtual bool EnterMiniGame();

	/** Called by the mini-game when a run ends. Queues rewards for the hub. */
	UFUNCTION(BlueprintCallable, Category = "GF|MiniGame")
	virtual void ReportResult(const FGFMiniGameResult& Result);

	/** Hub calls this on return: moves pending rewards into the given inventory. */
	void ClaimPendingRewards(FGFInventory& Into);

	const TArray<FGFItemStack>& GetPendingRewards() const { return PendingRewards; }

	UPROPERTY(BlueprintAssignable, Category = "GF|MiniGame")
	FGFOnMiniGameFinished OnMiniGameFinished;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnBeforeEnter, FName /*GateId*/);
	FOnBeforeEnter OnBeforeEnter;

protected:
	TArray<FGFItemStack> PendingRewards;
};
