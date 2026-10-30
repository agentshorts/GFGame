#pragma once

// Playtest entry points for Big Grumble. Console commands in GFGiantCheats.cpp call these (server or standalone;
// a client is ignored). The playtest clock is pure so a test can step it with no world.
#include "CoreMinimal.h"

class UWorld;

/**
 * Session clock for PlaytestFirstEventSeconds. 0 (or less) is off. Otherwise the first event of the session is due
 * once a player has been in for that many seconds. It does not look at the 2 day / 3 h trigger.
 * SecondsSinceFirstPlayer stays negative until the first player is in, then counts wall time (it does not pause if
 * they leave). The event itself still needs someone in.
 */
struct GFGIANT_API FGFGiantPlaytestClock
{
	float PlaytestFirstEventSeconds = 0.f;
	double SecondsSinceFirstPlayer = -1.0;
	bool bPlayerIn = false;
	/** An event already began this session (console or auto). The normal clock applies after that. */
	bool bSessionEventStarted = false;
};

namespace FGFGiantPlaytest
{
	/** Next clock after Dt seconds. Does not move while the override is off or this session already started an event. */
	GFGIANT_API FGFGiantPlaytestClock Advance(const FGFGiantPlaytestClock& In, double Dt);
	/** True when the playtest override should start the first event now. */
	GFGIANT_API bool IsFirstEventDue(const FGFGiantPlaytestClock& In);
	/** True while this override still owes the session its first event, so the 2 day / 3 h clock stays quiet. */
	GFGIANT_API bool HoldsOffNormalClock(const FGFGiantPlaytestClock& In);
}

namespace GFGiantCheats
{
	/**
	 * gf.Giant.Start. Empty on success. "client" when ignored (not logged). Otherwise a short reason, also logged.
	 */
	GFGIANT_API FString Start(UWorld* World);
	/** gf.Giant.End. Empty on success. "client" when ignored. Otherwise a short reason, also logged. */
	GFGIANT_API FString End(UWorld* World);
	/** gf.Giant.Status. The logged line, "client" when ignored, or a short reason. */
	GFGIANT_API FString Status(UWorld* World);
}
