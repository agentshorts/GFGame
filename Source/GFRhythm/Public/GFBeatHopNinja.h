#pragma once

// Beat Hop's traversal kit (#277). Every number lives on FGFBeatHopTraversalProfile, which UGFMovementTuning owns.
// The course reads that profile through GFBeatHopNinja::Profile. Obstacle rules are pure so a station can run without a song.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFBeatmap.h"
#include "GFRhythmJudge.h"
#include "GFBeatHopNinja.generated.h"

class USceneComponent;
class UTextRenderComponent;

/** Moves a mode can opt into. Bit order is the mask on the profile. */
UENUM(BlueprintType)
enum class EGFBeatHopKitMove : uint8
{
	MantleLedge,
	DashGate,
	WallRun,
	Grapple,
	Swivel,
	LedgeGrab,
	AirDash,
	TripleJump,
	DashGrip,
	LandingMarker,
	CrumbleBrick,
	BeatWall
};

/** Beat-synced obstacles. Mantle ledges stay pending until mantling (#259) is implemented. */
UENUM(BlueprintType)
enum class EGFBeatHopObstacle : uint8
{
	WallRunPanel,
	MantleLedge,
	GrapplePost,
	DashGate,
	SwivelPole,
	CrumbleBrick,
	BeatWall
};

UENUM(BlueprintType)
enum class EGFBeatHopNinjaContact : uint8
{
	None,
	/** The move connected (landed on the panel, swung the post, dashed the open ring). */
	Clear,
	/** Hit the obstacle without the move (a closed gate, a raised wall, a ledge bonk). */
	Bonk
};

/** Where a ledge sits relative to the mantle bands on the profile. */
UENUM(BlueprintType)
enum class EGFBeatHopMantleBand : uint8
{
	/** At or under the step. No mantle. */
	None,
	Auto,
	Grab,
	/** Above the grab, or a grab the difficulty does not allow. */
	TooHigh
};

/**
 * Beat Hop override of the hub traversal kit.
 * UGFMovementTuning::BeatHop is the asset. Course and station code read fields from here and do not keep their own numbers.
 */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFBeatHopTraversalProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control")
	bool bBrakeTapEnabled = true;

	/** How long a back tap kills auto-run, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0"))
	float BrakeTapSeconds = 0.3f;

	/** Dot against course forward, scaled by input size. More negative than minus this counts as a back tap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0", ClampMax = "1"))
	float BrakeEnterDot = 0.5f;

	/** Speed scale while the brake is held. Zero stops the auto-run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0", ClampMax = "1"))
	float BrakeSpeedScale = 0.f;

	/** Stick scale of the auto-run when the brake is up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0"))
	float AutoRunInputScale = 1.f;

	/** Presses this many seconds before the beat still count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0"))
	float InputBufferSeconds = 0.12f;

	/** Air time after leaving a brick edge that still counts as grounded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0"))
	float CoyoteSeconds = 0.08f;

	/** Added past the input buffer when a station script wants a press that must miss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0"))
	float MissMarginSeconds = 0.08f;

	/** Just-before epsilon for crumble and window edges, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0"))
	float TimingSlackSeconds = 0.001f;

	/** Judge windows are stored in milliseconds. Multiply by this to get seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta = (ClampMin = "0"))
	float MillisecondsToSeconds = 0.001f;

	/** One beat of phase, used when a swivel wraps past the downbeat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float BeatPhaseSpan = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beats", meta = (ClampMin = "0"))
	float HopBeats = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beats", meta = (ClampMin = "0"))
	float GroundDashBeats = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beats", meta = (ClampMin = "0"))
	float AirDashBeats = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beats", meta = (ClampMin = "0"))
	float WallKickBeats = 0.5f;

	/** Jumps allowed in one airtime. Jump 1 is the hop. Hard is the triple jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beats", meta = (ClampMin = "0"))
	int32 EasyJumps = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beats", meta = (ClampMin = "0"))
	int32 NormalJumps = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beats", meta = (ClampMin = "0"))
	int32 HardJumps = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0"))
	float WallRunCameraPullbackCm = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0"))
	float GrappleCameraPullbackCm = 220.f;

	/**
	 * Multiplier on hub stamina costs during a run.
	 * Zero skips every cost (the chart gates the moves). Any positive value uses the hub costs as tuned.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0"))
	float StaminaCostScale = 0.f;

	/** Heights at or under this are a step, not a mantle. Matches the 105 cm step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mantle", meta = (ClampMin = "0"))
	float AutoMantleMinCm = 105.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mantle", meta = (ClampMin = "0"))
	float AutoMantleMaxCm = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mantle", meta = (ClampMin = "0"))
	float LedgeGrabMinCm = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mantle", meta = (ClampMin = "0"))
	float LedgeGrabMaxCm = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mantle")
	bool bMantleFreeInRun = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mantle")
	bool bLedgeGrabHardOnly = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float GrappleLitBeats = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float DashGateOpenBeats = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float CrumbleDelayBeats = 2.f;

	/** Beat walls hold the lowered pose for this many beats, then the raised pose, and repeat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float BeatWallHoldBeats = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float BeatWallCycleBeats = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float GlowLeadBeats = 1.f;

	/** Swivel counts on this fractional beat. Zero is the downbeat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0", ClampMax = "1"))
	float SwivelBeatPhase = 0.f;

	/** How far from SwivelBeatPhase (in beats, wrapping) still counts as the downbeat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0", ClampMax = "0.5"))
	float SwivelPhaseWindow = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float WallRunPanelLengthCm = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float WallRunPanelThicknessCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float WallRunPanelHeightCm = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float MantleLedgeDepthCm = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float MantleLedgeWidthCm = 90.f;

	/** Placed test ledges. Bands come from the mantle fields, not from these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float MantleAutoSampleCm = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float MantleAutoTopCm = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float MantleGrabSampleCm = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float GrapplePostHeightCm = 280.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float GrapplePostRadiusCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float DashGateRadiusCm = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float DashGateDepthCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float SwivelPoleHeightCm = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float SwivelPoleRadiusCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float CrumbleBrickSizeCm = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float BeatWallWidthCm = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float BeatWallThicknessCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float BeatWallHeightCm = 200.f;

	/** Wall-run panels sit this far beside the lane, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float ObstacleSideOffsetCm = 120.f;

	/** How far above a safe brick a respawn stands, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0"))
	float RespawnAboveBrickCm = 100.f;

	/** Unscaled engine cube edge, cm. Mesh scale is extent divided by this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "1"))
	float MeshUnitCm = 100.f;

	/** Engine cube pivot sits at its centre, so a mesh rests on the ground at extent times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Obstacle", meta = (ClampMin = "0", ClampMax = "1"))
	float MeshCenterScale = 0.5f;

	/** Stations without a song use this beat length, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "0.01"))
	float StationPeriodSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "0"))
	float StationSpacingCm = 800.f;

	/** Whole beats from the station origin to the obstacle, so the downbeat is phase 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "0"))
	float StationBeatIndex = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "0"))
	float StationLabelSize = 48.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Station", meta = (ClampMin = "0"))
	float StationLabelHeightCm = 180.f;

	/**
	 * Bit masks of EGFBeatHopKitMove. Defaults: Easy is mantle ledges, dash gates, the landing marker,
	 * crumble bricks and beat walls. Normal adds wall runs and grapples. Hard adds the rest, including ledge grab.
	 * 3587 / 3599 / 4095 are those masks. GFBeatHopNinja.cpp static_asserts them against the enum order.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	int32 EasyMoves = 3587;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	int32 NormalMoves = 3599;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	int32 HardMoves = 4095;
};

/** One placed obstacle. SafeBrickIndex is the brick a fall returns to, never the obstacle itself. */
struct GFRHYTHM_API FGFBeatHopObstacleInstance
{
	EGFBeatHopObstacle Kind = EGFBeatHopObstacle::WallRunPanel;
	double BeatTime = 0.0;
	int32 Lane = 0;
	float HeightCm = 0.f;
	int32 SafeBrickIndex = INDEX_NONE;
	/** Run time of the first successful touch. Negative until then. Crumble reads this. */
	double TouchedAt = -1.0;
	TArray<FString> JudgedBy;
};

/** One press the buffer or a station plays back. LeftGroundAt is negative while the pawn has not left the ground. */
struct GFRHYTHM_API FGFBeatHopNinjaPress
{
	double Time = 0.0;
	EGFBeatHopKitMove Move = EGFBeatHopKitMove::MantleLedge;
	bool bGrounded = true;
	double LeftGroundAt = -1.0;
};

struct GFRHYTHM_API FGFBeatHopNinjaVerdict
{
	EGFJudgement Judgement = EGFJudgement::None;
	bool bPendingMantle = false;
	bool bTouchCrumble = false;
	bool bGlowing = false;
	bool bGateOpen = false;
	bool bWallRaised = false;
	bool bMoveAllowed = false;
	float CameraPullbackCm = 0.f;
};

/** Replicated obstacle result so a client sees the glow, the grapple and the mantle state (#214, #277). */
USTRUCT()
struct GFRHYTHM_API FGFBeatHopNinjaRep
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ObstacleIndex = INDEX_NONE;

	UPROPERTY()
	FString PlayerKey;

	UPROPERTY()
	EGFJudgement Result = EGFJudgement::None;

	UPROPERTY()
	uint8 Move = 0;

	UPROPERTY()
	uint8 bGlowing = 0;

	UPROPERTY()
	uint8 bWallRun = 0;

	UPROPERTY()
	uint8 bGrapple = 0;

	UPROPERTY()
	uint8 bMantle = 0;

	UPROPERTY()
	uint8 bMantlePending = 0;

	UPROPERTY()
	float OffsetSeconds = 0.f;

	UPROPERTY()
	uint16 Seq = 0;
};

/** Presses remembered across the input buffer. */
struct GFRHYTHM_API FGFBeatHopInputBuffer
{
	struct FEntry
	{
		double Time = 0.0;
		EGFBeatHopKitMove Move = EGFBeatHopKitMove::MantleLedge;
	};

	void Note(double Time, EGFBeatHopKitMove Move);
	/** True when a stored press of Move falls inside the buffer before NoteTime. */
	bool Find(double NoteTime, EGFBeatHopKitMove Move, const FGFBeatHopTraversalProfile& Profile, double& OutTime) const;

	TArray<FEntry> Presses;
};

/** One station on the ninja range. The map itself is #260; this is the obstacle the range places. */
struct GFRHYTHM_API FGFBeatHopNinjaStation
{
	FName Id;
	EGFBeatHopObstacle Kind;
	FString Expected;
	FGFBeatHopObstacleInstance Obstacle;
	float HeightCm = 0.f;
};

namespace GFBeatHopNinja
{
	GFRHYTHM_API int32 MoveBit(EGFBeatHopKitMove Move);

	/** Stateless hooks. Function pointers stay out of UHT's way; the lambdas that fill them do not capture. */
	using FProfileProvider = FGFBeatHopTraversalProfile (*)();
	using FMantleProbe = bool (*)();

	/** Asset profile when GFGame has registered one, otherwise the struct defaults. */
	GFRHYTHM_API FGFBeatHopTraversalProfile Profile();
	GFRHYTHM_API void SetProfileProvider(FProfileProvider Provider);

	/** False until mantling (#259) registers a probe that returns true. */
	GFRHYTHM_API bool IsMantleImplemented();
	GFRHYTHM_API void SetMantleProbe(FMantleProbe Probe);

	GFRHYTHM_API bool Allows(EGFBeatDifficulty Difficulty, EGFBeatHopKitMove Move, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API int32 JumpsFor(EGFBeatDifficulty Difficulty, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API EGFBeatHopKitMove MoveFor(EGFBeatHopObstacle Kind, float HeightCm, EGFBeatDifficulty Difficulty, const FGFBeatHopTraversalProfile& Profile);

	GFRHYTHM_API float ChartDistanceCm(float Beats, float RefSpeedCms);
	GFRHYTHM_API float DurationForBeats(float Beats, float RefSpeedCms, float SpeedCms);
	GFRHYTHM_API float SpeedForBeats(float Beats, float RefSpeedCms, float DurationSeconds);

	GFRHYTHM_API bool IsWithinBuffer(double PressTime, double NoteTime, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API bool IsWithinCoyote(double LeftGroundAt, double PressTime, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API bool GroundedEnough(const FGFBeatHopNinjaPress& Press, const FGFBeatHopTraversalProfile& Profile);

	GFRHYTHM_API EGFBeatHopMantleBand MantleBand(float HeightCm, EGFBeatDifficulty Difficulty, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API FVector ObstacleExtent(EGFBeatHopObstacle Kind, float HeightCm, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API float CameraPullbackCm(bool bWallRunning, bool bGrappling, const FGFBeatHopTraversalProfile& Profile);

	GFRHYTHM_API bool IsGlowing(const FGFBeatHopObstacleInstance& Obstacle, double Now, double PeriodSeconds, double WindowSeconds, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API bool IsDashGateOpen(const FGFBeatHopObstacleInstance& Obstacle, double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API bool IsGrappleLit(const FGFBeatHopObstacleInstance& Obstacle, double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API bool IsBeatWallRaised(double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API bool HasCrumbleFallen(const FGFBeatHopObstacleInstance& Obstacle, double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile);
	GFRHYTHM_API bool IsDownbeat(double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile);

	GFRHYTHM_API EGFJudgement JudgeTiming(double PressTime, double NoteTime, const FGFJudgeWindows& Windows, const FGFBeatHopTraversalProfile& Profile);

	GFRHYTHM_API FGFBeatHopNinjaVerdict Judge(
		const FGFBeatHopObstacleInstance& Obstacle,
		const FGFBeatHopNinjaPress& Press,
		EGFBeatHopNinjaContact Contact,
		EGFBeatDifficulty Difficulty,
		const FGFJudgeWindows& Windows,
		double PeriodSeconds,
		const FGFBeatHopTraversalProfile& Profile,
		bool bMantleImplemented);

	GFRHYTHM_API const TArray<FGFBeatHopNinjaStation>& Stations();
	/** A fresh chart: one of every obstacle, spaced for a range that has no song. */
	GFRHYTHM_API TArray<FGFBeatHopObstacleInstance> BuildStationChart(const FGFBeatHopTraversalProfile& Profile);
}

/**
 * Greybox stations for the traversal obstacles (#277).
 * L_NinjaRange itself is #260 and is not built here. Spawn this actor on that map when it exists.
 * Each station judges from the profile clock, so timing can be checked without a song.
 */
UCLASS()
class GFRHYTHM_API AGFBeatHopNinjaRange : public AActor
{
	GENERATED_BODY()

public:
	AGFBeatHopNinjaRange();

	virtual void BeginPlay() override;

	int32 NumStations() const { return Built.Num(); }
	const FGFBeatHopNinjaStation& GetStation(int32 Index) const { return Built[Index]; }
	FGFBeatHopNinjaVerdict JudgeStation(int32 Index, bool bOnBeat, EGFBeatDifficulty Difficulty) const;

private:
	void Build();

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> Labels;

	TArray<FGFBeatHopNinjaStation> Built;
	FGFBeatHopTraversalProfile UsedProfile;
	bool bBuilt = false;
};
