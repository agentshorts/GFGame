#pragma once
// Holds this machine's Acceleration Hop music state (issue 343) for the whole session, so the shuffle bag and the
// folder scan survive moving from one hop road to the next. Local only: nothing here replicates.
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFHopMusic.h"
#include "GFHopMusicSubsystem.generated.h"

UCLASS()
class GFRHYTHM_API UGFHopMusicSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	FGFHopMusicDirector& GetDirector() { return Director; }

	/** The director for this world's game instance, or null (a bare test world has no game instance). */
	static FGFHopMusicDirector* FindDirector(const UObject* WorldContext);

private:
	FGFHopMusicDirector Director;
};
