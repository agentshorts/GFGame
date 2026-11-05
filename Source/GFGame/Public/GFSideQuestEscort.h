#pragma once

// Side quest hire reward (QL-c). When a side line reaches a HireSpawned step, a short escort hire walks with the
// player: one dino spawned near them through the theft director's hire spawn, kept calm so it never turns on them,
// trailing the player like a follower, then let go the way a theft case lets go of a hire spawn.
// Raises HireSpawned when the body actually spawns, so the step completes. Server only.
// This is not a theft case: it never opens a case, never hires against a thief, and never reads or changes heat (#299).
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFSideQuestEscort.generated.h"

class AGFDino;
class APawn;

/** Where one escort stands. */
enum class EGFSideQuestEscortPhase : uint8
{
	/** Asked for; spawns once the player has a body. */
	Pending,
	/** The hire is out and walking with the player. */
	Escorting,
	/** Let go (or given up). The list drops it. */
	Done
};

/** One escort, pure. The subsystem holds the dino beside it. */
struct GFGAME_API FGFSideQuestEscort
{
	FString PlayerKey;
	FName LineId;
	FName TownId;
	EGFSideQuestEscortPhase Phase = EGFSideQuestEscortPhase::Pending;
	/** Seconds of escort left (Escorting) or of waiting for a body (Pending). */
	float Left = 0.f;
	bool bFollowing = false;
};

/** What one escort should do this tick. */
enum class EGFSideQuestEscortAction : uint8
{
	None,
	/** Pending with a body to spawn beside. */
	Spawn,
	/** Escorting and time is up, the hire is gone, or the player is gone. */
	StandDown,
	/** Pending too long with no body. Dropped without a spawn; the line still waits and asks again on rejoin. */
	GiveUp
};

/** Escort rules. No world: tests drive these directly. */
struct GFGAME_API FGFSideQuestEscortRules
{
	/** Plan: a short hire, about 60 s, then stand down. */
	static constexpr float EscortSeconds = 60.f;
	/** A Pending escort with no player body for this long is dropped. */
	static constexpr float PendingSeconds = 30.f;
	/** Spawn ring around the player (cm). Close enough to read as theirs, outside their feet. */
	static constexpr float SpawnMinCm = 200.f;
	static constexpr float SpawnMaxCm = 450.f;
	/** Calm is renewed in steps of this many seconds whenever less than CalmRenewBelowSeconds is left. */
	static constexpr float CalmSeconds = 10.f;
	static constexpr float CalmRenewBelowSeconds = 3.f;
	/** Follow band (cm): starts walking past FollowStartCm, stops inside FollowStopCm. Same band as a tamed follower. */
	static constexpr float FollowStartCm = 800.f;
	static constexpr float FollowStopCm = 300.f;

	/** The escort species. Plan: a short Tusksaw hire. */
	static FName Species();

	/**
	 * Adds a Pending escort for Key. False (and no change) when Key is empty or already has a Pending or Escorting
	 * escort, so a repeat ask never spawns a second hire.
	 */
	static bool Ask(TArray<FGFSideQuestEscort>& Escorts, const FString& PlayerKey, FName LineId, FName TownId);

	/** Advances one escort's clock by Dt and says what to do. bHasBody: the player has a pawn. bHireAlive: the hire is up. */
	static EGFSideQuestEscortAction Advance(FGFSideQuestEscort& Escort, float Dt, bool bHasBody, bool bHireAlive);

	/** Marks a Pending escort as spawned: Escorting for EscortSeconds. */
	static void Spawned(FGFSideQuestEscort& Escort);

	/** Follow decision for this tick from the distance to the player (AGFDino::NextState's follower band). */
	static bool ShouldFollow(bool bFollowing, float DistanceCm);

	/** Renew calm when it ends within CalmRenewBelowSeconds. */
	static bool ShouldRenewCalm(float Now, float CalmedUntil);
};

UCLASS()
class GFGAME_API UGFSideQuestEscorts : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UGFSideQuestEscorts* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Server. Queues an escort for Key (the quest subsystem calls this through OnSideHireAsked). */
	bool Ask(const FString& PlayerKey, FName LineId, FName TownId);
	/** Server. One step of every escort. Tick calls this; tests call it with their own Dt. */
	void TickEscorts(float DeltaSeconds);

	const TArray<FGFSideQuestEscort>& GetEscorts() const { return Escorts; }
	/** The hire walking with Key (null when none is out). */
	AGFDino* HireFor(const FString& PlayerKey) const;

private:
	bool IsServer() const;
	void HandleHireAsked(const FString& PlayerKey, FName LineId, FName TownId);
	bool SpawnFor(int32 Index, APawn* Body);
	void StandDown(int32 Index, bool bWorldEnding);
	void EscortStep(int32 Index, APawn* Body, float DeltaSeconds);

	TArray<FGFSideQuestEscort> Escorts;
	/** Parallel to Escorts. */
	TArray<TWeakObjectPtr<AGFDino>> Hires;
	FDelegateHandle AskHandle;
};
