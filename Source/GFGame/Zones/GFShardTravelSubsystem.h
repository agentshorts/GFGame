#pragma once

// Remembers which Shardstone a match should return to. The game instance survives OpenLevel.
// Each player consumes the pending stone once, so a death respawn does not snap back.
// The stone stays pending for late joiners until the next porch sets a new one.

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFShardTravelSubsystem.generated.h"

UCLASS()
class GFGAME_API UGFShardTravelSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Stable key for spawn and respawn. Unique net id when the player has one. */
	static FString KeyFor(const AController* Controller);

	void SetPending(FName StoneId);
	void ClearPending();
	bool Consume(const FString& PlayerKey, FName& OutStoneId);
	/** True when this key already consumed the current pending stone. */
	bool ConsumedStone(const FString& PlayerKey, FName& OutStoneId) const;

	FName GetPendingStoneId() const { return PendingStoneId; }

private:
	FName PendingStoneId;
	TSet<FString> ArrivedKeys;
};
