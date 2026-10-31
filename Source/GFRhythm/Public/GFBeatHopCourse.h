#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFBeatmap.h"
#include "GFRhythmJudge.h"
#include "GFLiveChartGenerator.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFEconomy.h"
#include "GFBeatHopCourses.h"
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

	/** True when this run crossed the finish. A song that runs out first is not a pass (#114). */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	bool bPassed = false;
};

/**
 * One judged hop, replicated to every machine (#214). OffsetSeconds is landing time minus the note
 * (negative = early). A miss swept after the window uses the same clock, measured past the note's end.
 */
USTRUCT()
struct GFRHYTHM_API FGFBeatHopJudgementRep
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerKey;

	UPROPERTY()
	int32 BrickIndex = INDEX_NONE;

	UPROPERTY()
	EGFJudgement Result = EGFJudgement::None;

	UPROPERTY()
	float OffsetSeconds = 0.f;

	UPROPERTY()
	int32 Combo = 0;

	/** Bumps on every record so a repeated hop on the same brick still replicates. */
	UPROPERTY()
	uint16 Seq = 0;
};

/** One player's live score. The results board is a separate snapshot taken when the run ends. */
USTRUCT()
struct GFRHYTHM_API FGFBeatHopScoreRep
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerKey;

	UPROPERTY()
	int32 Score = 0;

	UPROPERTY()
	int32 Combo = 0;

	UPROPERTY()
	int32 BestCombo = 0;

	UPROPERTY()
	int32 Perfects = 0;

	UPROPERTY()
	int32 Goods = 0;

	UPROPERTY()
	int32 Misses = 0;
};

/**
 * The hop clip a character is playing (#207), so remote pawns don't guess Perfect / Stumble from movement alone.
 * Clip is a DT_AnimMap key (Player.Land_Perfect, Player.Stumble, Player.Bhop_Air, ...).
 */
USTRUCT()
struct GFRHYTHM_API FGFBeatHopAnimRep
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerKey;

	UPROPERTY()
	FName Clip;

	UPROPERTY()
	float PlayRate = 1.f;

	UPROPERTY()
	uint8 bOneShot = 0;

	UPROPERTY()
	uint16 Seq = 0;
};

/**
 * Run movement mirrored onto the owning client (#214). Phase 0 = none, 1 = auto-run on, 2 = handed back.
 * The three live floats are what prediction must use; the saved floats are the movement the run replaced.
 */
USTRUCT()
struct GFRHYTHM_API FGFBeatHopMoveRep
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerKey;

	UPROPERTY()
	float MaxWalkSpeed = 0.f;

	UPROPERTY()
	float GravityScale = 1.f;

	UPROPERTY()
	float AirControl = 0.f;

	UPROPERTY()
	float SavedWalkSpeed = 0.f;

	UPROPERTY()
	float SavedGravityScale = 1.f;

	UPROPERTY()
	float SavedAirControl = 0.f;

	UPROPERTY()
	uint8 Phase = 0;

	/** INDEX_NONE unless this runner is waiting on a put-back brick (#221). */
	UPROPERTY()
	int32 HoldBrick = INDEX_NONE;

	UPROPERTY()
	double HoldUntilRunTime = -1.0;
};

/** A server teleport (mid-run put-back, lobby, checkpoint) the owning client snaps to instead of smoothing. */
USTRUCT()
struct GFRHYTHM_API FGFBeatHopPlacementRep
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerKey;

	UPROPERTY()
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.f;

	UPROPERTY()
	FVector_NetQuantize10 Velocity = FVector::ZeroVector;

	UPROPERTY()
	uint8 bFalling = 0;

	UPROPERTY()
	uint16 Seq = 0;
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
 * server: landing on a lit brick pays materials straight into the player's ledger account. Passing the course
 * (crossing the finish) pays the grade/combo bonus. A song that runs out first is not a win (#114).
 * Live mode ("Play to my music", solo only) streams bricks just ahead from the live beat tracker;
 * a live run ends if a second player joins or live audio stops.
 *
 * Rewards: every mint goes through the GF game state's ledger (AGFGameState::ServerMint) with a fixed TxId built from
 * the run id, the player key and what was earned (brick index or song end), so a retry can never pay twice. If the
 * player's account isn't readable yet, the grant waits here (server side, keyed by player) and is retried with the
 * same TxId. Nothing is handed to the mini-game gate's unkeyed PendingRewards.
 *
 * Songs mode runs on the replicated server world clock (run start + the host's calibration, snapshotted for the run),
 * so co-op players see the same lit bricks and the same StartGate countdown. Judgements, live scores, hit bricks,
 * hop clips, run-movement tuning and mid-run teleports replicate so every screen shows every player's hops (#214).
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
	/** Every player in the run: the server's judges, plus any replicated rows a client has that the server doesn't. */
	void GetScoreboard(TArray<FGFBeatHopScoreRep>& Out) const;
	/** True once anyone has landed a Perfect or Good on this brick (replicated, so clients don't crumble it). */
	bool IsBrickHitByAnyone(int32 Index) const;
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
	/** Server: a pawn crossed the finish gate. That is the win (#114). Ends the run (results, then back to the hub). */
	bool ReachFinish(APawn* Pawn);

	/** True after the last run ended on the finish. A timeout or an early end leaves this false until the next pass. */
	bool DidPass() const { return bLastPassed; }

	/** Server: pick the course for the next run (Fern, Rime, Cinder). False if unknown or mid-run. */
	bool SelectCourse(FName CourseId);
	FName GetSelectedCourseId() const { return SelectedCourseId; }
	FName GetRunCourseId() const { return RunCourseId; }
	int32 NumCourseTiles() const { return CourseTiles.Num(); }

	/**
	 * Server: burn GlowDust and hire a Ripwing for this player. The same player can't hire twice.
	 * The dino catches NotifyFall until its catches run out.
	 */
	bool TryHireRescue(const FString& PlayerKey, FString& OutReason);
	/** Catches left for this player, or 0 when they have no Ripwing. */
	int32 GetRescueCatches(const FString& PlayerKey) const;
	/**
	 * Server, mid-run: a hired Ripwing catches this fall and puts the runner back on the next brick.
	 * No hire, or no catches left, leaves the runner where they are (the catch floor still does its own job).
	 */
	bool NotifyFall(ACharacter* Character, const FString& PlayerKey);

	/** Server: step ice skid and the revenge dino. Does not write walk speed, gravity or air control. */
	void TickHazards(float DeltaSeconds);
	/** Course-local spot of the revenge dino. Zero when this course has none. */
	FVector GetRevengeLocal() const { return Revenge.Local; }
	int32 GetRevengeKnocks() const { return Revenge.Knocks; }
	bool HasRevengeDino() const { return Revenge.bSpawned; }
	UStaticMeshComponent* GetRescueDino() const { return RescueDino; }
	UStaticMeshComponent* GetRevengeDino() const { return RevengeDino; }
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
	 * Server: put a fallen runner back mid-run on the next brick the song hasn't reached yet (#221). That brick is
	 * excused for PlayerKey (no score, no Miss); the runner waits on it, then leaves on the beat with a full hop forward.
	 */
	void PutBackMidRun(ACharacter* Character, const FString& PlayerKey);
	/** The brick a put-back runner is waiting on, or INDEX_NONE once released (or never put back). */
	int32 GetHoldBrick(const ACharacter* Character) const;
	/** The velocity a put-back runner leaves their brick with: run speed along the course plus a jump's lift (#221). */
	FVector GetRespawnHopVelocity(const ACharacter* Character) const;

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

	/**
	 * Host calibration (seconds) snapshotted when the run starts and replicated. GetRunTime subtracts this on every
	 * machine, so a client's own Songs offset can't drift the countdown or the brick lights off the host.
	 */
	double GetRunCalibrationSeconds() const { return RunCalibrationSeconds; }
	double GetRunStartServerTime() const { return RunStartServerTime; }
	/** Tests: take the replicated clock (start + calibration) the way a client does when those properties arrive. */
	void AdoptSharedClock(double StartServerTime, double CalibrationSeconds);

	/** Clip a landing plays (#207). None stays None. */
	static FName LandingClipFor(EGFJudgement Judgement);
	/**
	 * Which clip a viewer plays. A simulated proxy uses the replicated clip. The owning client keeps its local
	 * takeoff / air, and takes the replicated clip only when both sides are brick landings (Land / Perfect / Stumble),
	 * so a late judgement can still correct the feet.
	 */
	static FName ClipForViewer(bool bSimulatedProxy, FName LocalClip, FName ReplicatedClip);

	const TArray<FGFBeatHopJudgementRep>& GetJudgements() const { return Judgements; }
	const TArray<FGFBeatHopScoreRep>& GetScoreReps() const { return ScoreReps; }
	const TArray<int32>& GetHitBricks() const { return HitBricks; }
	const TArray<FGFBeatHopAnimRep>& GetAnimReps() const { return AnimReps; }
	const TArray<FGFBeatHopMoveRep>& GetMoveReps() const { return MoveReps; }
	const TArray<FGFBeatHopPlacementRep>& GetPlacements() const { return Placements; }

	/** Client apply path for the replicated run view (OnRep calls these; tests call them on a second course). */
	void ReceiveRemoteJudgements(const TArray<FGFBeatHopJudgementRep>& In);
	void ReceiveRemoteScores(const TArray<FGFBeatHopScoreRep>& In);
	void ReceiveRemoteHitBricks(const TArray<int32>& In);
	/** Owning client: copy replicated auto-run speed, beat-length gravity and air control onto the local character. */
	void ApplyMoveRep(ACharacter* Character, const FGFBeatHopMoveRep& Row);
	/** Owning client: snap onto a replicated teleport. Sets bJustTeleported so the mesh doesn't lerp across the gap. */
	void ApplyPlacement(ACharacter* Character, const FGFBeatHopPlacementRep& Row);

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

	/** Course layout for this run (Fern, Rime, Cinder). Clients rebuild the same gaps and weave. */
	UPROPERTY(ReplicatedUsing = OnRep_RunSpec)
	FName RunCourseId;

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

	/** Host's Songs offset (seconds) at the moment the run started. Clients use this, not their own setting. */
	UPROPERTY(Replicated)
	double RunCalibrationSeconds = 0.0;

	UFUNCTION()
	void OnRep_RunSpec();

	/** Recent judgements (a short ring). Clients turn these into pops and landing clips. */
	UPROPERTY(ReplicatedUsing = OnRep_Judgements)
	TArray<FGFBeatHopJudgementRep> Judgements;

	UPROPERTY(ReplicatedUsing = OnRep_Scores)
	TArray<FGFBeatHopScoreRep> ScoreReps;

	/** Brick indices anyone has hit. Clients mark those bricks so a late joiner doesn't crumble them. */
	UPROPERTY(ReplicatedUsing = OnRep_HitBricks)
	TArray<int32> HitBricks;

	UPROPERTY(Replicated)
	TArray<FGFBeatHopAnimRep> AnimReps;

	UPROPERTY(ReplicatedUsing = OnRep_Moves)
	TArray<FGFBeatHopMoveRep> MoveReps;

	UPROPERTY(ReplicatedUsing = OnRep_Placements)
	TArray<FGFBeatHopPlacementRep> Placements;

	UFUNCTION()
	void OnRep_Judgements();

	UFUNCTION()
	void OnRep_Scores();

	UFUNCTION()
	void OnRep_HitBricks();

	UFUNCTION()
	void OnRep_Moves();

	UFUNCTION()
	void OnRep_Placements();

	UPROPERTY(ReplicatedUsing = OnRep_Session)
	bool bSession = false;

	UPROPERTY(ReplicatedUsing = OnRep_Session)
	FName SelectedSong;

	UPROPERTY(ReplicatedUsing = OnRep_Session)
	EGFBeatDifficulty SelectedDifficulty = EGFBeatDifficulty::Easy;

	UPROPERTY(ReplicatedUsing = OnRep_Session)
	FName SelectedCourseId;

	UPROPERTY(Replicated)
	TArray<FGFBeatHopResultRow> Results;

	UPROPERTY(Replicated)
	double ResultsEndServerTime = 0.0;

	/** Revenge dino spot (course-local). Clients drop the greybox sphere here. */
	UPROPERTY(ReplicatedUsing = OnRep_Hazards)
	FVector_NetQuantize RevengeSpot;

	UPROPERTY(ReplicatedUsing = OnRep_Hazards)
	uint8 bRevengeShown = 0;

	/** Where the hired Ripwing was last seen (world). */
	UPROPERTY(ReplicatedUsing = OnRep_Hazards)
	FVector_NetQuantize RescueSpot;

	UPROPERTY(ReplicatedUsing = OnRep_Hazards)
	uint8 bRescueShown = 0;

	UFUNCTION()
	void OnRep_Session();

	UFUNCTION()
	void OnRep_Hazards();

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
	void RecordJudgement(const FString& PlayerKey, int32 BrickIndex, EGFJudgement Result, double OffsetSeconds, int32 Combo);
	void PublishScore(const FString& PlayerKey, const FGFRhythmScore& Score);
	void PublishHit(int32 BrickIndex);
	void PublishAnim(const FString& PlayerKey, FName Clip, float PlayRate, bool bOneShot);
	void PublishMove(ACharacter* Character, uint8 Phase);
	void PublishHold(ACharacter* Character, const FString& PlayerKey, int32 BrickIndex, double UntilRunTime);
	void PublishPlacement(ACharacter* Character, const FString& PlayerKey, const FVector& Velocity, bool bFalling);
	void ApplyIncomingJudgements();
	void ApplyIncomingScores();
	void ApplyIncomingHitBricks();
	void MirrorOwnerMovement();
	void ClearTeleportForgiveness();
	FString RunnerId(const ACharacter* Character) const;
	FGFBeatHopMoveRep* FindMoveRep(const FString& PlayerKey);
	const FGFBeatHopAnimRep* FindAnimRep(const FString& PlayerKey) const;
	ACharacter* FindRunner(const FString& PlayerKey) const;
	void TeleportRunner(ACharacter* Character, const FString& PlayerKey, const FVector& Dest, const FRotator& Rot, const FVector& Velocity, bool bFalling);
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
	TArray<TObjectPtr<UBoxComponent>> CourseTiles;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> RescueTiles;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SongTileMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DifficultyTileMaterials;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CourseTileMaterials;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RescueTileMaterial;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> StatusText;

	/** StartGate slot 1 "Screen": a MID of MI_Track_StartGate_Screen whose scalar Frame picks the countdown cell. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ScreenMaterial;
	int32 ShownScreenFrame = -1;

	TArray<FName> SongTileIds;
	TArray<FName> CourseTileIds;

	/** Greybox spheres. The course builds them; the art dinos are not required for the rules. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> RescueDino;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> RevengeDino;

	FGFBeatHopRescueBook Rescues;
	FGFBeatHopRevengeState Revenge;
	FGuid RescueSalt;
	bool bCoursePassed = false;
	bool bLastPassed = false;

	void RefreshHazards(const APawn* Runner);
	void EnsureDinoMeshes();
	void ShowRescueAt(const FVector& WorldLocation);
	void ApplyHazardPush(ACharacter* Character, const FVector& CourseLocalPush);
	EGFTxResult BurnCost(const FGuid& TxId, const FString& PlayerKey, const TArray<FGFItemStack>& Stacks, FName Reason);
	TMap<TWeakObjectPtr<ACharacter>, FSavedRunMove> SavedMoves;
	/** Caught players put back mid-run stand still on their brick until the song reaches it (run time). */
	TMap<TWeakObjectPtr<ACharacter>, double> HoldUntil;
	/** Which brick each held runner is waiting on (#221). */
	TMap<TWeakObjectPtr<ACharacter>, int32> HoldBrick;
	/** Lets a held runner go: run speed forward, plus a hop unless they already jumped (#221). */
	void ReleaseHold(ACharacter* Character, bool bAlreadyHopping);
	TWeakObjectPtr<APawn> PendingStart;
	TWeakObjectPtr<APawn> PendingFinish;
	TFunction<void(const FString&)> TravelOverride;
	FString LastTravelMap;
	double DebugServerTime = -1.0;
	bool bTravelled = false;

	/** Client-side scores built from ScoreReps (the server reads FPlayerRun instead). */
	TMap<FString, FGFRhythmScore> RemoteScores;
	TSet<uint16> AppliedJudgementSeqs;
	TMap<FString, uint16> AppliedPlacementSeqs;
	/** Server: ignore in-flight client moves until this world time, so a teleport isn't yanked back. */
	TMap<TWeakObjectPtr<ACharacter>, double> TeleportForgiveUntil;
	uint16 NextJudgementSeq = 0;
	uint16 NextPlacementSeq = 0;
};
