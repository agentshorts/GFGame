#include "VoxelFinishRateLimiter.h"

bool FVoxelFinishRateLimiter::TryConsume(double NowSeconds, float RatePerSecond)
{
	if (!(RatePerSecond > 0.f))
	{
		return true;
	}
	const double Capacity = FMath::Max(1.0, static_cast<double>(RatePerSecond));
	if (!bStarted)
	{
		bStarted = true;
		Tokens = Capacity;
		LastSeconds = NowSeconds;
	}
	else if (NowSeconds > LastSeconds)
	{
		Tokens = FMath::Min(Capacity, Tokens + (NowSeconds - LastSeconds) * RatePerSecond);
		LastSeconds = NowSeconds;
	}
	// Refills are summed from fractional time steps; allow for rounding so a request that is exactly due is not refused.
	constexpr double Tolerance = 1e-6;
	if (Tokens < 1.0 - Tolerance)
	{
		return false;
	}
	Tokens = FMath::Max(0.0, Tokens - 1.0);
	return true;
}
