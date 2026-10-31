#pragma once

// Copies Villain Mode between AGFPlayerState and the pure rules, and applies a landed hit.

#include "CoreMinimal.h"
#include "GFVillainRules.h"

class AGFPlayerState;
class UDataTable;

struct GFSOCIAL_API FGFVillainSession
{
	static FGFVillainStatus Read(const AGFPlayerState& PS);
	static void Write(AGFPlayerState& PS, const FGFVillainStatus& Status);

	static FName Arm(AGFPlayerState& PS, double Now, bool bJailed, FRandomStream& Rng);
	static FName BeginDisarm(AGFPlayerState& PS, double Now);
	static bool Tick(AGFPlayerState& PS, double Now);
	static FName RerollTitle(AGFPlayerState& PS, FRandomStream& Rng);

	/** Resolves the hit, updates both players, and on a faint moves the material drop onto the victim. */
	static FGFVillainHit Resolve(AGFPlayerState& Attacker, AGFPlayerState& Victim, EGFVillainPlace AttackerPlace, EGFVillainPlace VictimPlace, double Now, float RawDamage, const UDataTable* Items);
};
