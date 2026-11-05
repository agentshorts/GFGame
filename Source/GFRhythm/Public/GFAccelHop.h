#pragma once
// Acceleration Hop (issue 334, v2 in issue 342): hop tiles on the Verdant to Cinderwright road. Landing on a tile on the music beat
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

	/** v2 (#342): an in-time landing on the road's lit target tile scales the whole gain by this (1.5 = half again). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "1"))
	float LitBoostScale = 1.5f;

	/**
	 * v3 (#347): in the air, the flat momentum turns toward where the camera aims (action game style). Only the
	 * direction turns, never the speed, and never faster than AirSteerTurnRateDeg, so it still flies like a hop.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|AirSteer")
	bool bAirSteerFollowsAim = true;

	/** Most the flight turns toward the aim per second, degrees. 90 is a quarter turn over a one second hop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|AirSteer", meta = (ClampMin = "0", ClampMax = "720"))
	float AirSteerTurnRateDeg = 90.f;

	/** Below this flat speed nothing turns, cm/s (a hop straight up stays straight up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|AirSteer", meta = (ClampMin = "0"))
	float AirSteerMinSpeedCm = 150.f;

	/** Aim further than this from the flight does not steer, degrees (a look back over the shoulder is free). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|AirSteer", meta = (ClampMin = "0", ClampMax = "180"))
	float AirSteerMaxAngleDeg = 150.f;

	/** On: aim steering only while the pawn is inside a hop road's box. Off: on every jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|AirSteer")
	bool bAirSteerOnlyOnRoad = true;
};

/**
 * v2 (#342): how the road picks and shows its one lit target tile. Lives on AGFAccelHopRoad (the road owns the pick);
 * the speed side (LitBoostScale) lives with the rest of the boost numbers on FGFAccelHopTuning.
 */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFAccelHopLitTuning
{
	GENERATED_BODY()

	/** Rough length of one hop along the road at base run speed, cm. "A few hops ahead" is measured in these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "50"))
	float HopLengthCm = 450.f;

	/** The pick window starts this many hops ahead of the player (closer is too late to aim for). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float MinAheadHops = 2.f;

	/** ...and ends this many hops ahead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0.5"))
	float MaxAheadHops = 4.f;

	/** Among tiles of equal pull, the one nearest this many hops ahead wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float PreferAheadHops = 3.f;

	/** Pick weights by kind. 0 never lights (Landing tiles open and close the run, they are not targets). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float StandardWeight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float DownbeatWeight = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float BoostWeight = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float LandingWeight = 0.f;

	/** A lit tile stays lit until the player is this far past it (then the next beat picks a new one). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float KeepBehindCm = 150.f;

	/** Server: a landing on the tile that was lit up to this long ago still counts (covers the RPC trip). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float ServerGraceSeconds = 0.6f;

	/** Lit look on M_GF_HopCel: EmissiveStrength (unlit tiles use the material's 2.0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0"))
	float LitEmissiveStrength = 10.f;

	/** Lit look: RimLightWidth (README: 0 is off, 0.2 to 0.4 is a lit edge; full strength is the top of that). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0", ClampMax = "1"))
	float LitRimLightWidth = 0.4f;

	/** The lit tile's own beat pulse (the material's per-tile BeatPulse) lasts this fraction of a beat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|Lit", meta = (ClampMin = "0.05", ClampMax = "1"))
	float LitPulseBeats = 0.3f;

	// ---- v3 (#347): the lit marker. A diamond lying flat on the lit tile's top face (HUD, projected from the world),
	// plus a beacon chevron over it. Magenta, black outline and a white core: never gold or green, so it is never
	// read as the landing circle. ----

	/** Diamond half diagonal as a share of the tile's smaller half size (1 touches the tile edges). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float MarkerTileFill = 0.85f;

	/** The diamond is never smaller than this half diagonal on the ground, cm (and before the tile art has a mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "10"))
	float MarkerMinHalfCm = 110.f;

	/** Lifted this far off the tile's top face so it never sinks into it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "0"))
	float MarkerLiftCm = 3.f;

	/** On the beat the diamond swells by this share (0.2 = 20% bigger at the top of the lit pulse). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "0", ClampMax = "1"))
	float MarkerPulseGrow = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker")
	FLinearColor MarkerColor = FLinearColor(1.f, 0.16f, 0.86f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker")
	FLinearColor MarkerOutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.85f);

	/** Colored stroke, px. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "1"))
	float MarkerLineWidthPx = 5.f;

	/** Dark outline under the stroke, px (wider than the stroke so it reads on any ground). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "1"))
	float MarkerOutlineWidthPx = 10.f;

	/** However far away, the diamond is drawn at least this big on screen (half diagonal, px). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "4"))
	float MarkerMinScreenPx = 30.f;

	/** The beacon chevron floats this high over the tile, cm, so the tile is found from hop height and over a crest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "0"))
	float MarkerBeaconHeightCm = 180.f;

	/** Beacon chevron half width, px. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop|LitMarker", meta = (ClampMin = "2"))
	float MarkerBeaconSizePx = 18.f;
};

/** One hop tile as the lit pick sees it: distance along the road (box local X, cm) and kind. */
struct GFRHYTHM_API FGFHopTileSlot
{
	float Along = 0.f;
	EGFHopTileKind Kind = EGFHopTileKind::Standard;
};

/** The road's lit target, replicated from the server (#342). Index into the road's sorted tile list; -1 is none. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFAccelHopLitState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	int32 Index = -1;

	/** Bumped on every change, so a re-pick of the same index still reaches clients. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|AccelHop")
	int32 Serial = 0;
};

/** Pure lit-tile rules (#342). Same input, same result. */
struct GFRHYTHM_API FGFAccelHopLitRules
{
	/** Pick weight of a kind. */
	static float WeightFor(EGFHopTileKind Kind, const FGFAccelHopLitTuning& Tuning);

	/** 0..1, stable for (Index, Seed): the variety in the pick, so a Downbeat sometimes beats a Boost. */
	static float Jitter(int32 Index, int32 Seed);

	/**
	 * The tile to light: within MinAheadHops..MaxAheadHops hops ahead of AnchorAlong in Direction (sign only), weight
	 * scaled by 0.5..1 Jitter, then a small pull toward PreferAheadHops. Skips AvoidIndex and weight 0 kinds. -1 when
	 * nothing is in the window (the end of the road).
	 */
	static int32 PickLitIndex(const TArray<FGFHopTileSlot>& Slots, float AnchorAlong, float Direction, int32 AvoidIndex, int32 Seed, const FGFAccelHopLitTuning& Tuning);

	/** Keep the current lit tile? Yes while it is not more than KeepBehindCm behind the anchor and not far past the window. */
	static bool KeepLit(const TArray<FGFHopTileSlot>& Slots, int32 LitIndex, float AnchorAlong, float Direction, const FGFAccelHopLitTuning& Tuning);

	/** The lit tile's sharp pulse: 1 on the beat, gone after PulseBeats of a beat. 0 with no clock. */
	static float LitPulse(double NowBeat, float PulseBeats);

	/**
	 * v3 (#347): ground half diagonal of the lit diamond for a tile of these half sizes (cm, the tile's own axes):
	 * MarkerTileFill of the smaller one, never under MarkerMinHalfCm, swollen by MarkerPulseGrow x Pulse.
	 */
	static float LitMarkerHalfCm(float TileHalfX, float TileHalfY, float Pulse, const FGFAccelHopLitTuning& Tuning);

	/**
	 * v3 (#347): the four ground corners of the lit diamond, flat at Center.Z: ahead, right, behind, left along the
	 * tile's flat Forward (world X if Forward is vertical or zero).
	 */
	static void LitMarkerCorners(const FVector& Center, const FVector& Forward, float HalfCm, FVector OutCorners[4]);
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

	/** Multiplier a hit adds on a tile kind (0 for Miss and None). A lit target tile (#342) scales it by LitBoostScale. */
	static float GainFor(const FGFAccelHopHit& Hit, EGFHopTileKind Kind, const FGFAccelHopTuning& Tuning, bool bLit = false);

	/**
	 * Applies a judged landing. Good/Perfect stack onto the current boost (capped) and restart its timer for BoostBeats
	 * beats; Landing tiles hold the speed (max of current and a fresh boost) and close the chain. A second hop on the
	 * same beat within one beat is ignored. bLit is a landing on the road's lit target tile (#342). Returns true when the
	 * boost changed.
	 */
	static bool ApplyHit(FGFAccelHopBoost& State, const FGFAccelHopHit& Hit, EGFHopTileKind Kind, double Now, float BeatSeconds, const FGFAccelHopTuning& Tuning, bool bLit = false);

	/** Speed multiplier at Now: the peak until the fade, then easing to 1 at ExpiresAt. Never below 1. */
	static float MultiplierAt(const FGFAccelHopBoost& State, double Now);

	/** 0..1 glow for the tiles' BeatPulse: 1 on the beat, falling off before the next. 0 with no clock. */
	static float BeatPulse(double NowBeat);

	/** Seconds per beat from a client report, clamped to the clock's 30..300 BPM range. */
	static float ClampBeatSeconds(float BeatSeconds);

	/** Server rate limit: false when the last accepted hop was less than MinHopGapSeconds ago. */
	static bool ServerAccepts(const FGFAccelHopBoost& State, double Now, const FGFAccelHopTuning& Tuning);
};
