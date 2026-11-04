#pragma once

// Mining clips (mining_anims_v1): DT_AnimMap keys and the small choices AGFCharacter makes before playing one.
// The clips play on the local body through UGFSimpleAnimComponent::ClaimClip (emote slot), under the ferry seat and the fist scope.

#include "CoreMinimal.h"

namespace GFMiningAnim
{
	inline FName Punch() { return FName(TEXT("Player.Mine_Punch")); }
	inline FName Dig() { return FName(TEXT("Player.Mine_Dig")); }
	inline FName Claw() { return FName(TEXT("Player.Mine_Claw")); }
	inline FName Overcharge() { return FName(TEXT("Player.Mine_Overcharge")); }
	inline FName ElementSwap() { return FName(TEXT("Player.Cast_Element_Swap")); }
	inline FName FieldStart() { return FName(TEXT("Player.Mine_Field_Start")); }
	inline FName FieldLoop() { return FName(TEXT("Player.Mine_Field_Loop")); }
	inline FName FieldEnd() { return FName(TEXT("Player.Mine_Field_End")); }

	/** A tap hit this close above the feet (or on a block's top face) is dug, not punched. */
	constexpr float DigAboveFeetCm = 30.f;

	/** Tap to mine: Dig for the ground or a block below, Punch for anything in front. */
	inline FName TapAnim(float ImpactZ, float ImpactNormalZ, float FeetZ)
	{
		return (ImpactNormalZ > 0.7f || ImpactZ < FeetZ + DigAboveFeetCm) ? Dig() : Punch();
	}

	/** Ferry seat > fist scope > mining: a mining clip never replaces either. */
	inline bool MayPlay(bool bFerrySeat, bool bFistScopeHeld)
	{
		return !bFerrySeat && !bFistScopeHeld;
	}
}