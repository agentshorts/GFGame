#pragma once

// Holds the revenge-gang board on the game instance, which stays up across OpenLevel.
// Travel calls BeginTravel before the map load and ConsumeTravel after it. The pending
// FGFRoamingHandoff is the only copy that has to survive; BeginTravel drops the live board.
// Same include-path note as GFRoamingPolicyComponent.h.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFRoamingPolicy.h"
#include "GFRoamingSubsystem.generated.h"

UCLASS()
class GFGAME_API UGFRoamingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UGFRoamingSubsystem* Get(const UObject* WorldContext);

	TSharedPtr<FGFRevengeGangBoard> EnsureBoard();
	TSharedPtr<FGFRevengeGangBoard> GetBoard() const { return Board; }
	const FGFRoamingHandoff& GetPending() const { return Pending; }

	/**
	 * Copies the gang into Pending and releases the board. False when there is no target
	 * (the board is left in place). Call before UGFMiniGameGateSubsystem opens its map.
	 */
	bool BeginTravel(FName ToMap);

	/**
	 * Builds a new board from Pending and clears the record. False when nothing is pending,
	 * including a second call. Call once the destination map is up.
	 */
	bool ConsumeTravel();

private:
	TSharedPtr<FGFRevengeGangBoard> Board;
	FGFRoamingHandoff Pending;
};
