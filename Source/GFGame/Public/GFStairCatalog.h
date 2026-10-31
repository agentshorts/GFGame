#pragma once

// The hub flights. AGFWorldLayoutDirector spawns these on L_Hub.
// They stand west of the Rime spawn, outside the gate ring and the forest box.

#include "CoreMinimal.h"
#include "GFStairFlight.h"

class UWorld;

namespace GFStairCatalog
{
	GFGAME_API TArray<FGFStairFlightSpec> HubFlights();

	/** Authority only. Clients build the same flight from the replicated spec. */
	GFGAME_API AGFStairFlight* SpawnFlight(UWorld* World, const FGFStairFlightSpec& Spec);

	/** Spawns HubFlights once. A second call does nothing if any flight is already there. */
	GFGAME_API void SpawnHubFlights(UWorld* World);
}
