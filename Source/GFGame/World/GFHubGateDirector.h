#pragma once

// Spawns the mini-game gates on the hub, the name marker (and stage or course) on a mode map,
// and parks each mapless sim director on its pad. Registered with StartupDirectorClasses.
// Does not build terrain or lights.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFHubGateDirector.generated.h"

UCLASS()
class GFGAME_API AGFHubGateDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFHubGateDirector();

	virtual void BeginPlay() override;

	/**
	 * Places whatever this world package needs. Safe to call more than once.
	 * WorldPackage is the map name, including a PIE prefix.
	 */
	void Place(const FString& WorldPackage, const FVector& InRimeCenter);

	/** Rime anchor from the territory layout, or the origin when that layout has no Rime row. */
	FVector RimeAnchor(const UWorld* World) const;
};
