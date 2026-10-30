#pragma once

// Beat Hop judging, brick lighting, rewards and session rules (PLAN.md M5). Pure C++ (no world) and fully unit-tested.
#include "CoreMinimal.h"
#include "GFBeatmap.h"
#include "GFItemTypes.h"
#include "GFRhythmJudge.generated.h"

class UDataTable;

UENUM(BlueprintType)
enum class EGFJudgement : uint8
{
	None,
	Perfect,
	Good,
	Miss
};

UENUM(BlueprintType)
enum class EGFBeatHopMode : uint8
{
	/** Built-in songs with hand-made beatmaps. The only mode allowed in co-op. */
	Songs,
	/** "Play to my music": bricks follow whatever the PC is playing (WASAPI loopback of an output device). Solo only. */
	Live
};

UENUM(BlueprintType)
enum class EGFBeatHopGrade : uint8
{
	C,
	B,
	A,
	S
};

/** How a brick looks right now. */
UENUM(BlueprintType)
enum class EGFBrickLight : uint8
{
	/** More than DimLeadBeats away. */
	Off,
	/** Coming up: glowing faintly in its lane colour. */
	Dim,
	/** On the beat: land now. */
	Lit,
	/** Landed on in time. */
	Hit,
	/** Its window passed without a good landing. */
	Missed
};

/** Timing windows (+/- ms around the brick's beat). */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFJudgeWindows
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float PerfectMs = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float GoodMs = 110.f;

	/** Songs: 50/110 (Easy 70/150). Live tracking jitters, so it's looser: 80/160 (Easy 100/200). */
	static FGFJudgeWindows For(EGFBeatHopMode Mode, EGFBeatDifficulty Difficulty);
};

/** Running score for one player in one run. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFRhythmScore
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm") int32 Score = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm") int32 Combo = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm") int32 BestCombo = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm") int32 Perfects = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm") int32 Goods = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm") int32 Misses = 0;

	int32 Judged() const { return Perfects + Goods + Misses; }
	/** 0..1: Perfect counts 1, Good 0.5, Miss 0. */
	float Accuracy() const;
};

/** Perfect / Good / Miss from timing error, plus combo and score. Same input always gives the same score. */
class GFRHYTHM_API FGFRhythmJudge
{
public:
	static constexpr int32 PerfectPoints = 300;
	static constexpr int32 GoodPoints = 100;
	static constexpr int32 ComboPerMultiplierStep = 10;
	static constexpr int32 MaxMultiplier = 4;

	explicit FGFRhythmJudge(const FGFJudgeWindows& InWindows = FGFJudgeWindows()) : Windows(InWindows) {}

	/** Window edges are inclusive: |error| <= PerfectMs is Perfect. */
	static EGFJudgement Classify(double ErrorSeconds, const FGFJudgeWindows& Windows);
	/** x1 at combo 0-9, x2 at 10-19, x3 at 20-29, x4 from 30. */
	static int32 MultiplierForCombo(int32 Combo);
	/** S >= 95% accuracy, A >= 85%, B >= 70%, else C. Accuracy counts Perfect 1, Good 0.75. */
	static EGFBeatHopGrade GradeFor(const FGFRhythmScore& Score);

	/** Judges a landing. Wrong lane is a Miss. Updates combo and score. */
	EGFJudgement JudgeLanding(double LandingTime, int32 LandingLane, double NoteTime, int32 NoteLane);
	/** A note went by without a landing (or the player fell). */
	void AddMiss();

	const FGFRhythmScore& GetScore() const { return Score; }
	const FGFJudgeWindows& GetWindows() const { return Windows; }
	int32 GetMultiplier() const { return MultiplierForCombo(Score.Combo); }
	void Reset() { Score = FGFRhythmScore(); }

private:
	void Apply(EGFJudgement J);

	FGFJudgeWindows Windows;
	FGFRhythmScore Score;
};

/** Brick lighting: dim a couple of beats early, lit inside the Good window, then Hit or Missed. */
struct GFRHYTHM_API FGFBrickLighting
{
	static constexpr double DimLeadBeats = 2.0;

	/** NowBeat and NoteBeat on the same beat grid; WindowBeats = Good window in beats. */
	static EGFBrickLight LightAt(double NowBeat, double NoteBeat, double LenBeats, double WindowBeats, bool bHit, bool bMissed);
	/** 0..1 glow for the material: ramps 0.15 -> 0.45 while Dim, pulses to 1 on the beat, holds for Hold bricks. */
	static float GlowAt(double NowBeat, double NoteBeat, double LenBeats, double WindowBeats, bool bHit, bool bMissed);
	/** True if a landing at NowBeat counts as landing on a lit brick. */
	static bool IsLandable(double NowBeat, double NoteBeat, double LenBeats, double WindowBeats);
};

/** What Beat Hop pays out. Materials only (PLAN.md section 2: GlowDust, PrismShard, GoldNote). Tuned here until DT_RhythmRewards exists. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFBeatHopRewardRules
{
	GENERATED_BODY()

	/** Paid on every Perfect/Good landing on a lit brick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm") FName LandingItem = TEXT("GlowDust");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm") int32 PerfectLanding = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm") int32 GoodLanding = 1;

	/** End of song, by grade C/B/A/S. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm") FName GradeItem = TEXT("PrismShard");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm") TArray<int32> GradeYield = { 1, 2, 4, 6 };
	/** Extra LandingItem per this much best combo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm") int32 ComboPerBonus = 10;

	/** Rare: a full combo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm") FName FullComboItem = TEXT("GoldNote");

	/** "Cozy" no-fail runs pay this share (rounded down, but never below 1 for a non-zero yield). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm") float CozyScale = 0.5f;
};

struct GFRHYTHM_API FGFBeatHopRewards
{
	static TArray<FGFItemStack> ForLanding(EGFJudgement Judgement, const FGFBeatHopRewardRules& Rules, bool bCozy);
	/** Grade + combo bonus + full-combo rare. TotalJudgedNotes is how many notes the chart judged. */
	static TArray<FGFItemStack> ForSongEnd(const FGFRhythmScore& Score, int32 TotalJudgedNotes, const FGFBeatHopRewardRules& Rules, bool bCozy);
	/** True if every stack is a tradeable economy material in DT_Items (tier 1-4 with a material category). */
	static bool AreMaterialsOnly(const TArray<FGFItemStack>& Stacks, const UDataTable* Items, FString* OutBadId = nullptr);
	static int32 Scale(int32 Count, bool bCozy, float CozyScale);
};

/** Who can play what. */
struct GFRHYTHM_API FGFBeatHopSessionRules
{
	/** Co-op (more than one player) runs use built-in songs only; live mode is solo because each PC hears its own audio. */
	static bool IsModeAllowed(EGFBeatHopMode Mode, int32 NumPlayers, FString* OutReason = nullptr);
	/** Songs mode needs a built-in chart; live needs none. */
	static bool CanStart(EGFBeatHopMode Mode, int32 NumPlayers, FName SongId, EGFBeatDifficulty Difficulty, FString* OutReason = nullptr);
};
