#include "GFBeatHopMantle.h"

bool GFBeatHopMantle::IsImplemented()
{
	// #259 has not landed. Beat Hop places the ledges and leaves the climb pending.
	return false;
}

GFBeatHopMantle::FResult GFBeatHopMantle::Query(const FQuery& Query, const FGFBeatHopTraversalProfile& Profile)
{
	FResult Result;
	Result.bAvailable = IsImplemented();
	Result.bFreeInRun = Profile.bMantleFreeInRun;
	const EGFBeatHopMantleBand Band = GFBeatHopNinja::MantleBand(Query.HeightCm, Query.Difficulty, Profile);
	const bool bApproach = Query.bForwardHeld && Query.bAirborne;
	Result.bWouldAutoMantle = bApproach && Band == EGFBeatHopMantleBand::Auto;
	Result.bWouldGrab = bApproach && Band == EGFBeatHopMantleBand::Grab;
	Result.bBonk = Query.bForwardHeld && (Band == EGFBeatHopMantleBand::TooHigh || (Band == EGFBeatHopMantleBand::None && Query.bAirborne));
	return Result;
}
