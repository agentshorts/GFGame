#pragma once

// Two-bone foot plant. UGFFootIKControlRig runs this inside Control Rig's forwards solve.
// Lengths of the two bones stay put. The ankle moves to the target when the leg can reach it.

#include "CoreMinimal.h"

namespace GFFootIK
{
	/**
	 * Hip stays. OutAnkle is the target, pulled in when the leg cannot reach.
	 * OutKnee keeps the bend on the same side as KneeIn.
	 * Returns true when the target was inside the reach, before the pull-in.
	 */
	bool SolveTwoBone(const FVector& Hip, const FVector& KneeIn, const FVector& AnkleIn, const FVector& AnkleTarget, FVector& OutKnee, FVector& OutAnkle);

	/** Rotates Current so CurrentPrimaryDir swings onto DesiredPrimaryDir. */
	FQuat AlignPrimary(const FQuat& Current, const FVector& CurrentPrimaryDir, const FVector& DesiredPrimaryDir);
}
