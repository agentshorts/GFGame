#pragma once

// Authority-only dressing for L_Hub: spires, rim cliffs, seeded points of interest, stones, porch arches.
// Match maps and the Kindle voxel grid are not this actor's job.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFWorldLayoutDirector.generated.h"

UCLASS()
class GFGAME_API AGFWorldLayoutDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFWorldLayoutDirector();

	virtual void BeginPlay() override;

private:
	bool bLaidOut = false;
};
