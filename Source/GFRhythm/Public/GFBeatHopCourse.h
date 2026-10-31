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
class UMeshComponent;
class UPrimitiveComponent;
class UInstancedStaticMeshComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UMaterialInterface;
class UTexture2D;
class UTextRenderComponent;
class ACharacter;
class APawn;

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
	/** Mesh origin to brick top (cm): Size.Z for the bottom-centre art bricks, Size.Z / 2 for the engine-cube fallback. */
	float TopOffsetZ = 0.f;
	/** The miss break-apart already ran (the brick is hidden, its chunks are falling). */
	bool bCrumbled = false;
};

/** A falling piece: a Crumble chunk from a missed brick, or a pixel cube from a Perfect burst. */
USTRUCT()
struct GFRHYTHM_API FGFBeatHopDebris
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	FVector Velocity = FVector::ZeroVector;
	FRotator Spin = FRotator::ZeroRotator;
	float Age = 0.f;
	float Life = 1.5f;
	/** Shrinks to nothing over its life (burst cubes); crumble chunks keep their size. */
	bool bShrink = false;
	FVector StartScale = FVector::OneVector;
};

/** One judgement pop for the HUD: the word (and combo) over the brick, plus the lane-coloured sparkle on a hit. */
struct GFRHYTHM_API FGFBeatHopPop
{
	FString PlayerKey;
	FVector WorldLocation = FVector::ZeroVector;
	EGFJudgement Judgement = EGFJudgement::None;
	int32 Combo = 0;
	FLinearColor LaneColor = FLinearColor::White;
	/** World time (s) the pop appeared. */
	double ShownAt = 0.0;
};

/** One player's line on the end-of-run results board (session mode, replicated to everyone). */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFBeatHopResultRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	FString PlayerKey;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	FString PlayerName;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	int32 Score = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	int32 BestCombo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	int32 Perfects = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	int32 Goods = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	int32 Misses = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	EGFBeatHopGrade Grade = EGFBeatHopGrade::C;
};

/**
 * Beat Hop VFX sprite sheet (art batch beat_hop_v1, #209): T_VFX_Judgement is a 64x64 one-colour sheet whose
 * rects come from T_VFX_Judgement_layout.json; T_VFX_Sparkle is a 4x1 flipbook of 16x16 frames.
 */
struct GFRHYTHM_API FGFBeatHopVfx
{
	static const TCHAR* JudgementTexturePath;
	static const TCHAR* SparkleTexturePath;
	static constexpr int32 SheetSize = 64;
	static constexpr int32 DigitAdvancePx = 6;
	static constexpr int32 SparkleFrames = 4;
	static constexpr float SparkleFps = 12.f;
	static constexpr float PopSeconds = 0.8f;

	/** Pixel rect (x, y, w, h; top-left origin) of PERFECT / GOOD / MISS. False for None. */
	static bool WordRect(EGFJudgement Judgement, FIntRect& Out);
	static FIntRect ComboRect();
	static FIntRect TimesRect();
	static FIntRect PlusRect();
	/** 0-9. */
	static FIntRect DigitRect(int32 Digit);
	/** PERFECT sunflower, GOOD mint, MISS clay (linear). */
	static FLinearColor Tint(EGFJudgement Judgement);
	/** COMBO lavender (linear). */
	static FLinearColor ComboTint();
	/** Flipbook frame at this age, or INDEX_NONE once the 4 frames have played. */
	static int32 SparkleFrame(double AgeSeconds);
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
 * Songs mode runs on the replicated server world clock, so co-op players see the same lit bricks.
 *
 * Art (batch beat_hop_v1, #201): bricks are the SM_Block_Beat_* meshes (L1-L5 by lane, Hold, Pad, Switch), each with a
 * MID made from the mesh's own MI on M_GF_BeatBrick so UpdateLights drives `Color`; the engine cube is only a fallback
 * when the art isn't there. A brick everyone missed breaks into Crumble_A/B/C. The track kit (Start/Finish gates,
 * checkpoint strips with trigger boxes, catch-floor tiles, rails, pillars and scenery) is built around the bricks and
 * pulses EmissiveStrength 1..4 on the beat. Characters on the course play the hop clips (#207). The HUD draws the
 * judgement pops from GetPops(). No audio yet.
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

	/** The art mesh for a brick: SM_Block_Beat_L<lane+1> (Tap), _Hold, _Pad, _Switch. Object path. */
	static FString BrickMeshPath(EGFBeatNoteType Type, int32 Lane);
	/** Linear EmissiveStrength for the track kit at this beat phase (0..1 within the beat): 4 on the beat, easing to 1. */
	static float BeatPulseStrength(double BeatPhase);

	/** Recent judgement pops (newest last), for the HUD. */
	const TArray<FGFBeatHopPop>& GetPops() const { return Pops; }
	/** Falling crumble chunks and burst cubes right now. */
	int32 NumDebris() const { return Debris.Num(); }
	UStaticMeshComponent* GetStartGate() const { return StartGate; }
	UStaticMeshComponent* GetFinishGate() const { return FinishGate; }
	int32 NumCheckpoints() const { return CheckpointTriggers.Num(); }
	UBoxComponent* GetCheckpointTrigger(int32 Index) const { return CheckpointTriggers.IsValidIndex(Index) ? CheckpointTriggers[Index].Get() : nullptr; }
	UInstancedStaticMeshComponent* GetCatchTiles() const { return CatchTiles; }
	/** Where a caught player goes back to: their last checkpoint, else just behind the start gate. World space. */
	FVector GetRespawnPoint(const ACharacter* Character) const;
	/** Records a checkpoint for a character (the trigger boxes call this). */
	void ReachCheckpoint(ACharacter* Character, int32 CheckpointIndex);
	/** (Re)builds the gates, checkpoints, catch floor, rails and scenery around the current bricks. */
	void BuildTrackKit();

	// -----------------------------------------------------------------------------------------------------------------
	// Session (#212): the course on L_Beat runs itself. It lifts into the sky, builds a lobby behind the start pad with
	// song and difficulty tiles, keeps players on it, starts a run when someone walks through the start gate, ends it at
	// the finish gate (or when the song runs out), shows the results, then sends everyone back to the hub.

	/** Server: turn the session on (the hub gate director calls this on L_Beat). Safe to call again. */
	void BeginSession();
	bool IsSession() const { return bSession; }

	/** Server: pick the song / difficulty for the next run (the lobby tiles call these). False if unknown or mid-run. */
	bool SelectSong(FName SongId);
	bool SelectDifficulty(EGFBeatDifficulty Difficulty);
	FName GetSelectedSong() const { return SelectedSong; }
	FName GetRunSongId() const { return RunSongId; }
	EGFBeatDifficulty GetRunDifficulty() const { return RunDifficulty; }
	EGFBeatDifficulty GetSelectedDifficulty() const { return SelectedDifficulty; }
	/** Song id of each lobby song tile, in tile order. */
	const TArray<FName>& GetSongTileIds() const { return SongTileIds; }
	static FString SongTitle(FName SongId);
	static FString DifficultyName(EGFBeatDifficulty Difficulty);

	/** Server: a pawn crossed the start gate. Starts the selected song; the run clock lines up with where it crossed. */
	bool TryStartFromGate(APawn* Pawn, FString& OutReason);
	/** Server: a pawn crossed the finish gate. Ends the run (results, then back to the hub). */
	bool ReachFinish(APawn* Pawn);
	/** Server: results countdown, return to the hub, deferred gate starts. Tick calls this every frame. */
	void UpdateSession();

	bool IsShowingResults() const { return ResultsEndServerTime > 0.0; }
	double GetResultsSecondsLeft() const;
	const TArray<FGFBeatHopResultRow>& GetResults() const { return Results; }
	/** Run timeline time (s) at which the song is over (last note + padding). */
	double GetRunEndTime() const { return EndTime; }
	/** Where the run clock starts when the run begins at a pawn crossing this local X. */
	double RunTimeAtLocalX(double LocalX) const;

	/** World location players are put on in the lobby. */
	FVector GetLobbySpawn() const;
	/** True when a pawn is nowhere near the course (just spawned on the ground, fell off the world, wandered off). */
	bool IsLostFromCourse(const APawn* Pawn) const;
	/** Server: put a pawn in the lobby, facing the start gate. */
	void PlaceInLobby(APawn* Pawn);
	/** True while the run's movement (auto-run, beat-length hop) is applied to this character. */
	bool HasRunMovement(const ACharacter* Character) const;

	/**
	 * StartGate countdown screen cell (beat_hop_v2, T_BeatHop_Countdown layout): 0 Off, 1 Ready_A, 2 "3", 3 "2", 4 "1",
	 * 5 GO, 6 GO flash, 7 Ready_B. Idle: Ready_A/B at 2 Hz. Run: Ready on the beat, then 3/2/1 at GO-3/-2/-1 beats,
	 * GO flash/GO every 0.125 s for 0.5 s, GO held 0.5 s, then Off. GO is the first brick's time.
	 */
	static int32 CountdownFrame(double RunTime, double GoTime, double Period, bool bRunningNow, double WorldNow);
	/** The StartGate Screen slot's MID (null if the gate art or its countdown material isn't there). */
	UMaterialInstanceDynamic* GetScreenMaterial() const { return ScreenMaterial; }
	int32 GetShownScreenFrame() const { return ShownScreenFrame; }
	/** Pushes the countdown cell to the screen (Tick does this every frame). */
	void UpdateScreen(double WorldNow);

	UBoxComponent* GetStartTrigger() const { return StartTrigger; }
	UBoxComponent* GetFinishTrigger() const { return FinishTrigger; }
	int32 NumSongTiles() const { return SongTiles.Num(); }
	int32 NumDifficultyTiles() const { return DifficultyTiles.Num(); }

	/** Tests: replace the level change at the end of the results (gets the map package name). */
	void SetTravelOverride(TFunction<void(const FString&)> InOverride) { TravelOverride = MoveTemp(InOverride); }
	/** The map the session last sent players to (empty until it has). */
	const FString& GetLastTravelMap() const { return LastTravelMap; }
	/** Tests: freeze the server world clock at a time (negative = use the real clock). */
	void SetDebugServerTime(double Seconds) { DebugServerTime = Seconds; }

	/** How high the session lifts the course (cm), clear of the voxel terrain and the map's own spawn. */
	static constexpr double SessionHeightCm = 10000.0;

	/** Results stay up this long (s) before everyone goes back to the hub. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float ResultsSeconds = 7.f;

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

	/** Checkpoints every this many beats along a song (none in live runs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	int32 CheckpointEveryBeats = 32;

	/** Catch-floor slab origin (cm below the brick tops). Its collision top sits 12.5 cm above this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float CatchFloorDepthCm = 500.f;

	// AActor
	virtual void BeginPlay() override;
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

	UPROPERTY(ReplicatedUsing = OnRep_Session)
	bool bSession = false;

	UPROPERTY(ReplicatedUsing = OnRep_Session)
	FName SelectedSong;

	UPROPERTY(ReplicatedUsing = OnRep_Session)
	EGFBeatDifficulty SelectedDifficulty = EGFBeatDifficulty::Easy;

	UPROPERTY(Replicated)
	TArray<FGFBeatHopResultRow> Results;

	UPROPERTY(Replicated)
	double ResultsEndServerTime = 0.0;

	UFUNCTION()
	void OnRep_Session();

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
	void Crumble(FGFBeatHopBrick& Brick);
	void Burst(const FVector& WorldTop, const FLinearColor& Color);
	void TickDebris(float DeltaSeconds);
	void UpdatePulse(double Now);
	void DriveAnims(double Now);
	void AddPop(const FString& PlayerKey, const FVector& WorldTop, EGFJudgement Judgement, int32 Combo, const FLinearColor& Color);
	void ClearTrackKit();
	UStaticMeshComponent* AddKitMesh(const TCHAR* Path, const FTransform& Relative, bool bCollide);
	UInstancedStaticMeshComponent* AddKitInstances(const TCHAR* Path, const TArray<FTransform>& Relative, bool bCollide);
	void TrackPulse(UMeshComponent* Mesh);

	void BuildLobby();
	void UpdateLobbyLook();
	void RebuildPreview();
	void ApplyRunMovement(ACharacter* Character);
	void RestoreRunMovement();
	void RespawnCharacter(ACharacter* Character);
	void OnSessionRunFinished();
	void TravelToHub();
	void ForEachPlayerPawn(TFunctionRef<void(APawn*)> Fn) const;
	UTextRenderComponent* AddLobbyText(const FString& Text, const FVector& Relative, float Size, const FColor& Color);

	UFUNCTION()
	void OnStartTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void OnFinishTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void OnLobbyTileOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCheckpointOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Per character on the course: which hop clip is on, and when a one-shot ends (#207). */
	struct FHopAnim
	{
		FName Key;
		double OneShotUntil = 0.0;
		bool bWasFalling = false;
		bool bVictory = false;
		bool bCaught = false;
		double RespawnAt = 0.0;
	};

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

	UPROPERTY()
	TArray<FGFBeatHopDebris> Debris;

	/** Gates, strips, rails, scenery: everything BuildTrackKit made (destroyed on rebuild). */
	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> KitComponents;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> StartGate;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> FinishGate;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> CatchTiles;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> CheckpointTriggers;

	/** Track-kit MIDs whose EmissiveStrength follows the beat. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PulseMaterials;

	/** One per checkpoint strip; lit mint once someone reaches it. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CheckpointMaterials;

	TArray<FGFBeatHopPop> Pops;
	TMap<TWeakObjectPtr<ACharacter>, FHopAnim> HopAnims;
	/** Judgement of each character's latest brick landing (server), read by DriveAnims on the same tick. */
	TMap<TWeakObjectPtr<ACharacter>, EGFJudgement> LastLanding;
	TMap<TWeakObjectPtr<ACharacter>, int32> LastCheckpoint;
	TArray<FVector> CheckpointSpots;
	double KitStartX = 0.0;
	double KitEndX = 0.0;
	int32 LastAddedLane = INDEX_NONE;
	bool bWasRunning = false;
	float ShownPulse = -1.f;

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

	// Session (#212)
	struct FSavedRunMove
	{
		float MaxWalkSpeed = 0.f;
		float GravityScale = 1.f;
		float AirControl = 0.f;
	};

	UPROPERTY()
	TObjectPtr<UBoxComponent> StartTrigger;

	UPROPERTY()
	TObjectPtr<UBoxComponent> FinishTrigger;

	/** Lobby floor, tiles, labels and signs (built once; the track kit rebuilds around them). */
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> LobbyComponents;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> SongTiles;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> DifficultyTiles;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SongTileMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DifficultyTileMaterials;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> StatusText;

	/** StartGate slot 1 "Screen": a MID of MI_Track_StartGate_Screen whose scalar Frame picks the countdown cell. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ScreenMaterial;
	int32 ShownScreenFrame = -1;

	TArray<FName> SongTileIds;
	TMap<TWeakObjectPtr<ACharacter>, FSavedRunMove> SavedMoves;
	/** Caught players put back mid-run stand still on their brick until the song reaches it (run time). */
	TMap<TWeakObjectPtr<ACharacter>, double> HoldUntil;
	TWeakObjectPtr<APawn> PendingStart;
	TWeakObjectPtr<APawn> PendingFinish;
	TFunction<void(const FString&)> TravelOverride;
	FString LastTravelMap;
	double DebugServerTime = -1.0;
	bool bTravelled = false;
};
