#pragma once
// Acceleration Hop (issue 334): hop tiles on the Verdant to Cinderwright road. Landing on a tile on the music beat
// gives a short speed boost. Judging reuses the Beat Hop clock and windows; this file is the pure part (no world),
// so every rule is unit tested. AGFAccelHopTile and AGFAccelHopRoad are the placed actors; the movement component owns
// the boost state.
#include "CoreMinimal.h"
#include "GFBeatmap.h"
#include "GFRhythmJudge.h"
#include "GFAccelHop.generated.h"

/** One of the four hop tile meshes. Placement picks the kind (Tools/accel_hop_layout.py). */
UENUM(BlueprintType)
enum class EGFHopTileKind : uint8
{
	/** A plain hop. */
	Standard,
	/** The bar line: an extra kick when the landing is on the first beat of a bar. */
	Downbeat,
	/** Chevrons: a bigger kick. */
	Boost,
	/** Run start and end: refreshes the boost without stacking it, and closes the chain. */
	Landing
};

/** Every Acceleration Hop number. Lives on UGFMovementTuning (AccelHop) so it is tuned in the data asset. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFAccelHopTuning
{
	GENERATED_BODY()

	/** Speed multiplier added by a Good landing (0.10 = +10% run speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0"))
	float GoodBoost = 0.10f;

	/** Speed multiplier added by a Perfect landing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0"))
	float PerfectBoost = 0.18f;

	/** Extra added on a Downbeat tile when the landing is on the bar line (beat 1 of BeatsPerBar). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0"))
	float DownbeatBonus = 0.08f;

	/** Extra added on a Boost tile (any beat, as long as the landing is in time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0"))
	float BoostTileBonus = 0.15f;

	/** The boost never goes past this multiplier, however long the chain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "1"))
	float MaxMultiplier = 1.8f;

	/** A boost lasts this many beats of the music, so a faster song gives shorter, snappier boosts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0.5"))
	float BoostBeats = 4.f;

	/** The last beats of a boost ease back to normal speed instead of snapping. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0"))
	float FadeBeats = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "1"))
	int32 BeatsPerBar = 4;

	/** A Miss on a tile resets the chain count (the running boost still plays out). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	bool bMissBreaksChain = true;

	/** Which Beat Hop windows judge a landing (Songs mode: Normal 50/110 ms, Easy 70/150 ms). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	EGFBeatDifficulty Difficulty = EGFBeatDifficulty::Normal;

	/** Server side: hops closer together than this are refused (one landing cannot be counted twice). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0"))
	float MinHopGapSeconds = 0.2f;

	/** Server side: the reported tile must be this close to the pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0"))
	float ServerCheckRadiusCm = 400.f;
};

/** One landing judged against the music. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFAccelHopHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	EGFJudgement Judgement = EGFJudgement::None;

	/** Nearest beat index (0-based) on the clock's grid; -1 when there was no clock. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	int32 BeatIndex = -1;

	/** Landing time minus the beat, in ms (negative = early). */
	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	float ErrorMs = 0.f;

	/** The nearest beat is beat 1 of a bar. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	bool bOnBarLine = false;
};

/** The running boost on one character. Times are world seconds. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFAccelHopBoost
{
	GENERATED_BODY()

	/** Peak multiplier of the current boost (1 = none). */
	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	float Multiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	double ExpiresAt = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	float FadeSeconds = 0.f;

	/** In-time hops in a row (shown by the HUD later; Landing and Miss reset it). */
	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	int32 Chain = 0;

	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	int32 LastBeatIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	double LastHopTime = -1.0e9;
};

/** Pure Acceleration Hop rules. Same input, same result. */
struct GFRHYTHM_API FGFAccelHopRules
{
	/** CustomDepth stencil the tiles write and PP_GF_HopOutline reads. */
	static constexpr int32 OutlineStencil = 3;
	static constexpr const TCHAR* TileMeshDir = TEXT("/Game/Art/AccelerationHop/Tiles");
	static constexpr const TCHAR* OutlineMaterialPath = TEXT("/Game/Art/AccelerationHop/Materials/PP_GF_HopOutline.PP_GF_HopOutline");
	static constexpr const TCHAR* BeatCollectionPath = TEXT("/Game/Art/AccelerationHop/Materials/MPC_GF_HopBeat.MPC_GF_HopBeat");

	/** "/Game/Art/AccelerationHop/Tiles/SM_Hop_Tile_<Kind>.SM_Hop_Tile_<Kind>" (matches Tools/accel_hop_layout.py). */
	static FString MeshPathFor(EGFHopTileKind Kind);

	/** True when the asset's package is on disk (the hop art is imported on the editor machine, not in this repo). */
	static bool AssetExists(const FString& ObjectPath);

	/** Beat Hop's Songs windows at the tuning's difficulty (the road always runs a built-in song or live music). */
	static FGFJudgeWindows WindowsFor(EGFBeatHopMode Mode, const FGFAccelHopTuning& Tuning);

	/** Judges a landing at NowBeat (the clock's current beat) against the nearest beat. No clock (NowBeat < 0, Bpm <= 0) is None. */
	static FGFAccelHopHit JudgeLanding(double NowBeat, float Bpm, const FGFJudgeWindows& Windows, int32 BeatsPerBar);

	/** Multiplier a hit adds on a tile kind (0 for Miss and None). */
	static float GainFor(const FGFAccelHopHit& Hit, EGFHopTileKind Kind, const FGFAccelHopTuning& Tuning);

	/**
	 * Applies a judged landing. Good/Perfect stack onto the current boost (capped) and restart its timer for BoostBeats
	 * beats; Landing tiles hold the speed (max of current and a fresh boost) and close the chain. A second hop on the
	 * same beat within one beat is ignored. Returns true when the boost changed.
	 */
	static bool ApplyHit(FGFAccelHopBoost& State, const FGFAccelHopHit& Hit, EGFHopTileKind Kind, double Now, float BeatSeconds, const FGFAccelHopTuning& Tuning);

	/** Speed multiplier at Now: the peak until the fade, then easing to 1 at ExpiresAt. Never below 1. */
	static float MultiplierAt(const FGFAccelHopBoost& State, double Now);

	/** 0..1 glow for the tiles' BeatPulse: 1 on the beat, falling off before the next. 0 with no clock. */
	static float BeatPulse(double NowBeat);

	/** Seconds per beat from a client report, clamped to the clock's 30..300 BPM range. */
	static float ClampBeatSeconds(float BeatSeconds);

	/** Server rate limit: false when the last accepted hop was less than MinHopGapSeconds ago. */
	static bool ServerAccepts(const FGFAccelHopBoost& State, double Now, const FGFAccelHopTuning& Tuning);
};
