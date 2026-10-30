#pragma once

// Open-world faint hook. Whoever owns fainting (drop a little material, respawn at the circle edge) implements
// IGFFaintHandler; the giant event only calls it, so GFGiant never touches the ledger itself.
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GFFaintHandler.generated.h"

class AGFPlayerState;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGFFaintHandler : public UInterface
{
	GENERATED_BODY()
};

class GFGIANT_API IGFFaintHandler
{
	GENERATED_BODY()

public:
	/** Server: PS fainted in the open world (Cause e.g. "GiantSnack"). Apply the open-world faint rule and respawn at RespawnAt. */
	virtual void HandleOpenWorldFaint(AGFPlayerState* PS, const FVector& RespawnAt, FName Cause) = 0;
};

/**
 * Stand-in until the real faint system exists: respawns the pawn at RespawnAt and records the call.
 * TODO(faint system): drop "a little material" there (through the ledger, in that system, not in GFGiant).
 */
UCLASS()
class GFGIANT_API UGFFaintStub : public UObject, public IGFFaintHandler
{
	GENERATED_BODY()

public:
	virtual void HandleOpenWorldFaint(AGFPlayerState* PS, const FVector& RespawnAt, FName Cause) override;

	struct FCall
	{
		FString Key;
		FVector RespawnAt = FVector::ZeroVector;
		FName Cause;
	};
	TArray<FCall> Calls;
};
