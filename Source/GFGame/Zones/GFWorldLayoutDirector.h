#pragma once

// Authority-only dressing for L_Hub: spires, rim cliffs, seeded points of interest, stones, porch arches.
// The Kindle spire shares Stone.Kindle (off the Rime spawn). Match maps and the Kindle voxel grid are not this actor's job.

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

	/**
	 * One greybox trench on the Grumble Dig porch. Safe to call again.
	 * L_GrumbleDig stays a missing package; this does not open a level.
	 */
	void EnsureGrumbleDigSite();

private:
	bool bLaidOut = false;
};
