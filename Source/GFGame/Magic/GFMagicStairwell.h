#pragma once

// Stairwell Call reuses Big Grumble's dig plan at player size, then keeps at most one claw's worth of cells.

#include "CoreMinimal.h"
#include "GFGrumbleDig.h"

class AGFVoxelWorld;
class APawn;

struct GFGAME_API FGFMagicStairwell
{
	static FGFGrumbleDigContext Context();
	static FGFGrumbleDigTuning Tuning();
	static FGFGrumbleDigMove MoveFor(const APawn* Pawn, const FVector& AimPoint);
	static TArray<FIntVector> Cells(const APawn* Pawn, const FVector& AimPoint, const AGFVoxelWorld* Voxel);
};
