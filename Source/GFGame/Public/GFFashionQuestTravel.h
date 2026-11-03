#pragma once

// Carries The Elder's Errand and the faction seats from one map to the next.
// The game instance survives OpenLevel. The runway gate and the hub return both call NotifyMapTravel
// while the old world is still up. The next world's quest subsystem takes the carry once.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFFashionQuestTravel.generated.h"

class UGFFashionGateSubsystem;
class UWorld;

UCLASS()
class GFGAME_API UGFFashionQuestTravel : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Binds to the fashion gate once it exists. Safe to call more than once. */
	void TryBind();
	/** Takes a held carry into this world. False when there is nothing to take or the world is not ready. */
	bool RestoreHere(UWorld* World);

	/** Copies quest bytes, seats and session factions onto the gate. Clients are ignored. */
	static void CaptureInto(UGFFashionGateSubsystem* Gate, UWorld* World);
	/** Moves a held carry into World. Leaves the carry in place when World is not ready. */
	static bool RestoreFrom(UGFFashionGateSubsystem* Gate, UWorld* World);

private:
	void OnMapTravel(UWorld* Leaving);

	FDelegateHandle TravelHandle;
};
