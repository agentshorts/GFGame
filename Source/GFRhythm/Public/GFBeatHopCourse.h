#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFBeatmap.h"
#include "GFRhythmJudge.h"
#include "GFLiveChartGenerator.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFEconomy.h"
#include "GFBeatHopCourse.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UMaterialInterface;
class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FGFOnBrickLanding, const FString&, PlayerKey, int32, BrickIndex, EGFJudgement, Judgement, int32, MaterialsEarned);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGFOnBeatHopRunFinished, const FString&, PlayerKey, const FGFMiniGameResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnBeatHopRunEnded, const FString&, Reason);

/** Server only: a reward the ledger couldn't take yet (the player's account isn't readable). Retried with the same TxId. */
struct FGFBeatHopPendingGrant
{
	FGuid TxId;
	FString PlayerKey;
	TArray<FGFItemStack> Stacks;
	FName Reason;
};

/** One brick on the course. */
USTRUCT()
struct GFRHYTHM_API FGFBeatHopBrick
{
	GENERATED_BODY()

	/** Judged at this time on the run's timeline (s). */
	double Time = 0.0;
	/** Hold length (s); 0 for single bricks. */
	double LenSeconds = 0.0;
	int32 Lane = 0;
	EGFBeatNoteType Type = EGFBeatNoteType::Tap;
	/** Live bricks: generator id. Songs: note index in the beatmap. */
	int32 SourceId = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

	bool bHitByAnyone = false;
	bool bMissedByEveryone = false;
	float ShownGlow = -1.f;
};

/**
 * Beat Hop greybox course (PLAN.md M5). Lays coloured bricks out from a beatmap (beat time x reference speed = distance,
 * lane = sideways offset), lights them to the beat (dim ~2 beats early, lit on the beat), and judges landings on the
 * server: landing on a lit brick pays materials straight into the player's ledger account. A song's end pays a
 * grade/combo bonus. Live mode ("Play to my music", solo only) streams bricks just ahead from the live beat tracker;
 * a live run ends if a second player joins or live audio stops.
 *
 * Rewards: every mint goes through the GF game state's ledger (AGFGameState::ServerMint) with a fixed TxId built from
 * the run id, the player key and what was earned (brick index or song end), so a retry can never pay twice. If the
 * player's account isn't readable yet, the grant waits here (server side, keyed by player) and is retried with the
 * same TxId. Nothing is handed to the mini-game gate's unkeyed PendingRewards.
 *
 * Songs mode runs on the replicated server world clock, so co-op players see the same lit bricks. Greybox only:
 * engine cube meshes and the basic shape material; no audio, VFX or HISM yet.
 */
UCLASS()
class GFRHYTHM_API AGFBeatHopCourse : public AActor
{
	GENERATED_BODY()

public:
	AGFBeatHopCourse();

	/** Server: builds the course for a run and starts it. False (with a reason) if the rules don't allow it. */
	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	bool StartRun(FName SongId, EGFBeatDifficulty Difficulty, EGFBeatHopMode Mode, bool bCozy, FString& OutReason);

	/** Same, with an explicit player count (tests, or a lobby that knows who's in). */
	bool StartRunForPlayers(FName SongId, EGFBeatDifficulty Difficulty, EGFBeatHopMode Mode, bool bCozy, int32 NumPlayers, FString& OutReason);

	/** Server: ends the run now, paying song-end rewards. */
	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	void FinishRun();

	/** Server: ends the run early with a reason the HUD can show (rewards earned so far still pay). */
	void EndRun(const FString& Reason);

	/** Why the last run ended early (empty if it ran to the end). */
	const FString& GetEndReason() const { return EndReason; }

	/** Server: judge a landing on a brick at time Now (run timeline, s). Normally called from Tick when a character lands. */
	EGFJudgement HandleLanding(const FString& PlayerKey, int32 BrickIndex, double Now);

	/** Run timeline "now" (s). Songs: server world time since start minus calibration. Live: latency-corrected clock. */
	double GetRunTime() const;

	/** Tests and tools: freeze the run clock at a time (negative = use the real clock). */
	void SetDebugRunTime(double Seconds) { DebugRunTime = Seconds; }

	/** Tests and tools: pretend this many players are in the session (negative = count the game state's players). */
	void SetDebugPlayerCount(int32 Count) { DebugPlayerCount = Count; }

	/** Tests and tools: mint into this ledger instead of the GF game state's (both must outlive the course). */
	void SetLedgerOverride(FGFTransferLedger* InLedger, IGFLedgerAccounts* InAccounts) { LedgerOverride = InLedger; AccountsOverride = InAccounts; }

	/** The fixed TxId of one grant: run id + player key + what it was for ("Landing.12", "SongEnd"). */
	static FGuid MakeGrantTxId(const FGuid& RunId, const FString& PlayerKey, const FString& What);
	const FGuid& GetRunId() const { return RunId; }

	/** Server: retries grants the ledger couldn't take yet (Tick does this every second). Returns how many went in. */
	int32 RetryPendingGrants();
	const TArray<FGFBeatHopPendingGrant>& GetPendingGrants() const { return PendingGrants; }

	/** Re-evaluates brick lights, misses and the song end at the current run time (Tick does this every frame). */
	void UpdateRun();

	EGFBrickLight GetBrickLight(int32 BrickIndex) const;
	int32 FindBrickByComponent(const UPrimitiveComponent* Component) const;
	/**
	 * Brick indices are stable for the whole run. Live mode prunes bricks well behind the player, so only
	 * [GetFirstBrickIndex(), NumBricks()) are still in memory; NumBricks() counts every brick made this run.
	 */
	int32 NumBricks() const { return BrickBase + Bricks.Num(); }
	int32 GetFirstBrickIndex() const { return BrickBase; }
	int32 NumKeptBricks() const { return Bricks.Num(); }
	bool IsValidBrick(int32 Index) const { return Bricks.IsValidIndex(Index - BrickBase); }
	const FGFBeatHopBrick& GetBrick(int32 Index) const { return Bricks[Index - BrickBase]; }
	bool IsRunning() const { return bRunning; }
	const FGFBeatmap& GetBeatmap() const { return Beatmap; }
	const FGFRhythmScore* GetScore(const FString& PlayerKey) const;
	/** Everything this player earned this run (paid into the ledger, or pending in GetPendingGrants). */
	const FGFInventory* GetEarnedRewards(const FString& PlayerKey) const;
	/** World location of a brick's top centre. */
	FVector GetBrickTop(int32 BrickIndex) const;

	UPROPERTY(BlueprintAssignable, Category = "GF|Rhythm")
	FGFOnBrickLanding OnBrickLanding;

	/** Per player when a run ends. Result.Rewards is empty: rewards are already in the ledger (see GetEarnedRewards). */
	UPROPERTY(BlueprintAssignable, Category = "GF|Rhythm")
	FGFOnBeatHopRunFinished OnRunFinished;

	/** Server: a run ended early (live mode: a second player joined, or the audio device is gone). */
	UPROPERTY(BlueprintAssignable, Category = "GF|Rhythm")
	FGFOnBeatHopRunEnded OnRunEnded;

	/** Sideways distance between lane centres (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float LaneWidthCm = 100.f;

	/** Live mode: a brick is removed this long after its judging window has passed (and everyone's misses are counted). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float LiveBrickKeepSeconds = 2.f;

	/** Brick footprint (cm) and thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	FVector BrickSizeCm = FVector(90.f, 90.f, 30.f);

	/** Lane colours, left to right (each lane also gets its own colour in colour-blind mode later via glyphs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	TArray<FLinearColor> LaneColors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	FGFBeatHopRewardRules Rewards;

	/** Pad bricks launch you this hard (cm/s up, forward kept). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float PadLaunchZ = 900.f;

	// AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** What every machine needs to rebuild the same course. */
	UPROPERTY(ReplicatedUsing = OnRep_RunSpec)
	FName RunSongId;

	UPROPERTY(ReplicatedUsing = OnRep_RunSpec)
	EGFBeatDifficulty RunDifficulty = EGFBeatDifficulty::Normal;

	UPROPERTY(Replicated)
	EGFBeatHopMode RunMode = EGFBeatHopMode::Songs;

	UPROPERTY(Replicated)
	bool bRunCozy = false;

	UPROPERTY(Replicated)
	double RunStartServerTime = 0.0;

	UPROPERTY(Replicated)
	bool bRunning = false;

	UFUNCTION()
	void OnRep_RunSpec();

private:
	struct FPlayerRun
	{
		FGFRhythmJudge Judge;
		TSet<int32> Judged;
		int32 MissSweep = 0;
		FGFInventory Earned;
	};

	void ClearBricks();
	void BuildFromBeatmap(const FGFBeatmap& Map);
	int32 AddBrick(double Time, double LenSeconds, int32 Lane, int32 NumLanes, EGFBeatNoteType Type, int32 SourceId);
	void UpdateLights(double Now);
	void SweepMisses(double Now);
	void UpdateLive(double Now);
	void PruneLiveBricks(double Now);
	void PollLandings(double Now);
	void Grant(const FString& PlayerKey, FPlayerRun& Run, const TArray<FGFItemStack>& Stacks, FName Reason, const FString& What);
	/** True once the ledger has it (applied now or before); false to retry later. */
	bool TryMint(const FGFBeatHopPendingGrant& Grant);
	int32 CurrentPlayerCount() const;
	double GetServerWorldTime() const;
	double WindowSeconds() const;
	double PeriodSeconds() const;
	FPlayerRun& RunFor(const FString& PlayerKey);
	FLinearColor LaneColor(int32 Lane) const;

	UPROPERTY(VisibleAnywhere, Category = "GF|Rhythm")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "GF|Rhythm")
	TObjectPtr<UStaticMeshComponent> CatchFloor;

	UPROPERTY()
	TArray<FGFBeatHopBrick> Bricks;
	/** Index of Bricks[0] (bricks pruned so far this run). */
	int32 BrickBase = 0;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	FGFBeatmap Beatmap;
	FGFJudgeWindows Windows;
	TMap<FString, FPlayerRun> Runs;
	TMap<TWeakObjectPtr<ACharacter>, bool> WasFalling;
	FGFLiveChartGenerator Live;
	double LiveStartClock = 0.0;
	double DebugRunTime = -1.0;
	int32 DebugPlayerCount = -1;
	FGuid RunId;
	FString EndReason;
	TArray<FGFBeatHopPendingGrant> PendingGrants;
	float RetryTimer = 0.f;
	FGFTransferLedger* LedgerOverride = nullptr;
	IGFLedgerAccounts* AccountsOverride = nullptr;
	double EndTime = 0.0;
	int32 NumLanes = 4;
};
