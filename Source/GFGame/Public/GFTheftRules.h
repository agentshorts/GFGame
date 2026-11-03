#pragma once

// Crystal theft rules. No world. The director passes distances, sight, and who is still chasing.
// One break seen by several villagers hires once. An unseen break hires nobody.

#include "CoreMinimal.h"
#include "GFTerritoryTypes.h"
#include "GFTownsfolkTypes.h"

/** Clear, then spotted while the villager warns and flees, then hunted once the elder hires. */
enum class EGFTheftHeat : uint8
{
	Clear,
	Spotted,
	Hunted
};

/** One player's heat for one town. Advance is the only clock. */
struct GFGAME_API FGFTheftHeat
{
	EGFTheftHeat State = EGFTheftHeat::Clear;
	FName TownId;
	float HuntLeft = 0.f;
	float Away = 0.f;
	/** Set when this record has already reported a clear, so a second tick does not report it again. */
	bool bReportedClear = false;
};

/** A wild dino the hire may take. Distance is from the break. bInRange is the town ring. */
struct GFGAME_API FGFHireCandidate
{
	float Distance = 0.f;
	bool bWild = false;
	bool bInRange = false;
};

/** Existing indices into the candidate list, plus how many new bodies to spawn. */
struct GFGAME_API FGFHirePlan
{
	TArray<int32> Existing;
	int32 SpawnCount = 0;
};

struct GFGAME_API FGFTheftRules
{
	static constexpr float SightCm = 1500.f;
	static constexpr float HuntSeconds = 120.f;
	static constexpr float AwaySeconds = 60.f;
	static constexpr int32 MaxHired = 3;
	/** Spawn only when fewer than this many wild dinos are already in the ring. */
	static constexpr int32 SpawnBelow = 2;
	static constexpr float DefaultHireRangeCm = 1500.f;

	/** A villager within SightCm who can see the break. Elders do not witness. */
	static bool Sees(EGFTownsfolkRole Role, float DistanceCm, bool bLineOfSight, float RangeCm = SightCm);

	/** True only for the first ask of a break that at least one villager saw. */
	static bool ShouldHire(int32 Witnesses, bool bAlreadyHired);

	/**
	 * Nearest in-range wild dinos, up to MaxHired.
	 * When fewer than SpawnBelow are in range, spawn enough to reach SpawnBelow.
	 * SpeciesCount 0 cannot spawn.
	 */
	static FGFHirePlan PlanHire(const TArray<FGFHireCandidate>& Candidates, int32 SpeciesCount);

	/**
	 * GetZoneAt's faction when the point is faction land.
	 * A town with no members has no radius, so a break on that anchor still names the town
	 * when it sits inside the one-member camp.
	 */
	static FName ResolveTown(const TArray<FGFTerritoryFaction>& Factions, const FGFTerritoryCurve& Curve, FName ZoneFaction, const FVector& Location);

	/** Enter Spotted for Town. False when this record is already hunting, or spotted for another town. */
	static bool NoteWitness(FGFTheftHeat& Heat, FName Town);

	/** Spotted becomes Hunted and the 120s clock starts. False unless the record is Spotted. */
	static bool NoteHire(FGFTheftHeat& Heat);

	/**
	 * Hunted only. Clears when the clock runs out, when Away reaches AwaySeconds outside the town,
	 * or when no hired dino is left alive. Stepping back into town resets Away.
	 * True on the tick that clears.
	 */
	static bool AdvanceHeat(FGFTheftHeat& Heat, float Dt, bool bInTown, int32 AliveHired);
};
