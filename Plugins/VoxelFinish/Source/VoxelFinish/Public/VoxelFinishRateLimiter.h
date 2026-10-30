#pragma once

#include "CoreMinimal.h"

/**
 * Token bucket for server-side request limits. Holds up to RatePerSecond tokens (at least 1), starts full and refills
 * RatePerSecond tokens per second. Time is passed in, so the server picks the clock and tests can drive it.
 */
struct VOXELFINISH_API FVoxelFinishRateLimiter
{
	/** True (and one token used) when a request may run at NowSeconds. RatePerSecond <= 0 means no limit. */
	bool TryConsume(double NowSeconds, float RatePerSecond);
	void Reset() { bStarted = false; Tokens = 0.0; LastSeconds = 0.0; }
	double GetTokens() const { return Tokens; }

private:
	double Tokens = 0.0;
	double LastSeconds = 0.0;
	bool bStarted = false;
};
