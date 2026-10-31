#pragma once

// Server side of Hallow Night (the Depths, PLAN.md M7). Players talk to it through AGFPlayerState::RequestMode
// (Mode "Depths"). The server runs the 2.5D mover at a fixed 60 Hz from each player's held input and replicates
// bodies, runs, enemies and the shared Z/X/C switches (clients place platforms from switch state + server time).
//
// Verbs (FGFModeRequest.Verb):
//   Enter     Ints = [depth]  (depth N needs N-1 finished)        Leave (mid-run: half the unbanked loot is lost, the rest is banked)
//   Input     Floats = [moveX -1..1], Ints = [jumpPressed, jumpHeld, dashPressed]
//   Switch    Names = [Z | X | C] (anyone in the run; 0.4 s cooldown per key)
//   Heal      (3 per section, +2 HP)      Attack (1 damage to shades in front)     Revive (a downed friend in reach)
//   Helper    Names = [dino id, tool id, optional arg]
//             catch_player (where), float_water_area (area), fly_player_over (level). A faint keeps loot.
//   LiftHome  (at the entrance, a reached checkpoint or the exit: banks everything) -> Stacks = banked
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "GFDepthsTypes.h"
#include "GFModeTypes.h"
#include "GFDepthsDirector.generated.h"

class AGFGameState;

USTRUCT(BlueprintType)
struct GFDEPTHS_API FGFDepthsPlayerView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depths") FString PlayerKey;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") FGFDepthsBody Body;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") FGFDepthsRunState Run;
	/** Everything banked this run (receipt). */
	UPROPERTY(BlueprintReadOnly, Category = "Depths") TArray<FGFItemStack> Banked;
};

USTRUCT(BlueprintType)
struct GFDEPTHS_API FGFDepthsEnemyView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depths") FVector2D Pos = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 HP = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 MaxHP = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 Dir = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") bool bDefeated = false;
};

UCLASS()
class GFDEPTHS_API AGFDepthsDirector : public AActor, public IGFModeHandler
{
	GENERATED_BODY()

public:
	AGFDepthsDirector();

	static const FName ModeName;
	static AGFDepthsDirector* Get(UWorld* World, bool bSpawn = true);

	virtual FName GetModeName() const override { return ModeName; }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual void OnModePlayerLeft(AGFPlayerState* PS) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 0 = no run. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depths") int32 Depth = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depths") TArray<FGFDepthsPlayerView> Players;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depths") TArray<FGFDepthsSwitch> Switches;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depths") TArray<FGFDepthsEnemyView> Enemies;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depths") TArray<bool> PickupTaken;
	/** Water areas a rented swimmer has floated this run. Shared, like the Z/X/C switches. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Depths") TArray<FName> FloatedAreas;

	/** Server or client: where a platform is right now (switch state + replicated server time). */
	FVector2D PlatformPosition(int32 Index) const;
	const FGFDepthsPlayerView* FindPlayer(const FString& Key) const;
	/** Server time the platforms run on (AGameStateBase::GetServerWorldTimeSeconds on every machine). */
	double ServerTime() const;

	/** Tests: fixed loot seed (0 = random). */
	int32 ForcedSeed = 0;
	/** Tests only: put a player somewhere (standing still). */
	void TeleportForTests(const FString& Key, const FVector2D& Pos);
	/** Tests only: the server's view of a player in the run (null if not in it). */
	FGFDepthsPlayerView* EditPlayerForTests(const FString& Key) { return Edit(Key); }
	/** Tests only: hurt a player as a shade would (knockback and invulnerability apply). */
	void HitForTests(const FString& Key, int32 Amount);

	/** Spawns the playable lane and the start / finish / results / hub spots on this pad. Safe to call again. */
	void EnsurePlayableSite();
	/**
	 * Puts a runner on the exit lift and steps the sim so the exit drop can roll.
	 * The greybox finish arch uses this. The 2.5D course itself is unchanged.
	 * True when that runner has reached the exit (including when they were already there).
	 */
	bool ArriveAtExit(const FString& Key);

	static constexpr float LiftReach = 150.f;
	static constexpr float AttackReach = 140.f;
	static constexpr float AttackCooldown = 0.35f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FPlayerInput
	{
		FGFDepthsInput Held;
		bool bJumpLatch = false;
		bool bDashLatch = false;
		double LastAttack = -1000.0;
	};
	TMap<FString, FPlayerInput> Inputs;
	FRandomStream Rng;
	float Accum = 0.f;

	AGFGameState* GS() const;
	FGFDepthsPlayerView* Edit(const FString& Key);
	const FGFDepthsLevel& Level() const { return FGFDepthsLevel::Get(Depth); }
	void StartRun(int32 InDepth);
	void SimStep(float Dt, double Now);
	void Hurt(FGFDepthsPlayerView& P, int32 Amount);
	void Faint(FGFDepthsPlayerView& P);
	void Respawn(FGFDepthsPlayerView& P);
	void Bank(FGFDepthsPlayerView& P, const TArray<FGFItemStack>& Stacks, FName Reason);
	void RemovePlayer(const FString& Key);
	void RescaleEnemies();
	bool AtLift(const FGFDepthsPlayerView& P) const;
};
