#pragma once

// Where townsfolk stand, who is in reach, and the idle / wander / talk state machine.
// No world. Callers pass the faction layout and the config offsets.

#include "CoreMinimal.h"
#include "GFTerritoryTypes.h"
#include "GFTownsfolkTypes.h"

struct GFGAME_API FGFTownsfolkSpot
{
	FName TownId;
	EGFTownsfolkRole Role = EGFTownsfolkRole::Villager;
	FVector Location = FVector::ZeroVector;
};

/** Numbers copied onto a townsfolk when the director spawns it. */
struct GFGAME_API FGFTownsfolkSpawnParams
{
	FName TownId;
	EGFTownsfolkRole Role = EGFTownsfolkRole::Villager;
	FVector TownCenter = FVector::ZeroVector;
	float TownRadius = 0.f;
	float WanderRadius = 400.f;
	float WalkSpeed = 140.f;
	float TalkSeconds = 4.f;
	float Reach = 300.f;
	float IdleMin = 4.f;
	float IdleMax = 9.f;
	float StopDistance = 100.f;
};

struct GFGAME_API FGFTownsfolkRules
{
	static constexpr float InteractReachCm = 300.f;
	static constexpr int32 VillagersPerTown = 4;
	static constexpr int32 EldersPerTown = 1;

	static FName VillagerSpotTag();
	static FName ElderSpotTag();

	/** "Town:<Id>" on the same actor as a spot tag. */
	static bool SpotFromTags(const TArray<FName>& Tags, const FVector& Location, FGFTownsfolkSpot& Out);

	/**
	 * One row per spawn. A town and role with any tagged spots uses only those spots.
	 * Otherwise the config offsets (or a small ring, if the offset list is short) sit on the faction anchor.
	 * Towns that are not in Factions are dropped. A nameless faction is dropped.
	 */
	static void Resolve(
		const TArray<FGFTerritoryFaction>& Factions,
		const TArray<FGFTownsfolkSpot>& Tagged,
		const TArray<FVector>& VillagerOffsets,
		const FVector& ElderOffset,
		int32 InVillagersPerTown,
		int32 InEldersPerTown,
		TArray<FGFTownsfolkSpot>& Out);

	/** 3D distance. Reach 300 cm is the default interact range. */
	static bool InReach(const FVector& From, const FVector& To, float Reach);

	/** Index of the closest point within Reach, or INDEX_NONE. Ties keep the earlier index. */
	static int32 NearestInReach(const FVector& From, const TArray<FVector>& Points, float Reach);

	/** Horizontal disc. Z stays on Point. A radius of 0 is the center. */
	static FVector ClampToDisc(const FVector& Center, const FVector& Point, float Radius);

	/**
	 * Land radius for this camp. A member count below 1 uses the 1-member radius, so an empty town
	 * still has a disc and a joined town is never smaller than that.
	 */
	static float CampRadius(const FGFTerritoryFaction& Faction, const FGFTerritoryCurve& Curve, int32 Members);

	/** Role title only. "Rime Elder", "Verdant Villager". No personal name. */
	static FString DisplayName(FName TownId, EGFTownsfolkRole Role);

	/** The shout a villager uses for the whole Warn. */
	static const TCHAR* WarnShout();

	/** L_World_Factions_A, including a PIE prefix on the map name. */
	static bool IsFactionsWorld(const FString& MapOrPackage);
};

/**
 * Idle, wander, talk, warn, and flee. Advance moves Location and keeps it inside the town disc.
 * Talk returns to Idle. Talk does not go straight to Wander.
 * Warn lasts WarnSeconds, then Flee. Flee asks for a hire on arrival or after FleeSeconds.
 */
struct GFGAME_API FGFTownsfolkMind
{
	static constexpr float WarnSeconds = 3.f;
	static constexpr float FleeSeconds = 10.f;

	EGFTownsfolkBehavior State = EGFTownsfolkBehavior::Idle;
	float TimeLeft = 0.f;
	FVector Home = FVector::ZeroVector;
	FVector TownCenter = FVector::ZeroVector;
	float TownRadius = 0.f;
	float WanderRadius = 400.f;
	float WalkSpeed = 140.f;
	float StopDistance = 100.f;
	float IdleMin = 4.f;
	float IdleMax = 9.f;
	FVector WanderTarget = FVector::ZeroVector;
	/** Who to face while warning. */
	FVector FacePoint = FVector::ZeroVector;
	/** Where Flee walks. */
	FVector ElderPoint = FVector::ZeroVector;

	static bool CanTransition(EGFTownsfolkBehavior From, EGFTownsfolkBehavior To);
	/** To when the move is allowed, otherwise From. */
	static EGFTownsfolkBehavior Transition(EGFTownsfolkBehavior From, EGFTownsfolkBehavior To);

	/** Enters Talk, or refreshes the timer when already talking. False when Talk is refused. */
	bool BeginTalk(float Seconds);

	/** Enters Warn. False when Warn is refused (already warning, fleeing, or an unknown state). */
	bool BeginWarn(const FVector& Thief, const FVector& Elder);

	EGFTownsfolkPulse Advance(float DeltaSeconds, FRandomStream& Rng, FVector& Location);

	float PickIdleWait(FRandomStream& Rng) const;
};
