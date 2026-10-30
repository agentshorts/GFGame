#pragma once

// Server-side opt-in for a mode that does not have its own game mode class.
// Characters read it while simulating. The resulting flag replicates on the movement component.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFMovementWorldSubsystem.generated.h"

UCLASS()
class GFGAME_API UGFMovementWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UGFMovementWorldSubsystem* Get(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "GF|Movement")
	void SetStaminaOptIn(bool bEnable);

	bool HasStaminaOptIn() const { return bHasOptIn; }
	bool WantsStamina() const { return bOptIn; }

private:
	bool bHasOptIn = false;
	bool bOptIn = false;
};
