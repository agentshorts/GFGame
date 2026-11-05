#pragma once
// Acceleration Hop tutorial (#348): the four lessons, their on-screen prompts, and the pure progress rules.
// The live map (L_AccelHop_Tutorial_v2, the visual pass copy of v1 from Tools/create_accel_hop_tutorial_map.py) tags every
// hop tile with its lesson
// (LessonTag). AGFAccelHopTutorial feeds each judged landing in here and the HUD draws PromptFor.
// Lessons run in order: hop on the beat, steer in the air, hit the lit tile, then one short run that chains all three.
// Reaching a later lesson's tiles skips ahead (nobody gets stuck); walking back never rewinds.
#include "CoreMinimal.h"
#include "GFRhythmJudge.h"

enum class EGFAccelHopLesson : uint8
{
	Beat,
	Steer,
	Lit,
	Chain,
	Done
};

/** One lesson's copy and goal. Copy is short on purpose: it sits on screen while you hop. */
struct GFRHYTHM_API FGFAccelHopLessonSpec
{
	EGFAccelHopLesson Lesson = EGFAccelHopLesson::Beat;
	/** Tag suffix on the map's tiles (GFAccelHopTutorial.Lesson.<Id>). Tools/accel_hop_tutorial_layout.py uses the same ids. */
	FName Id;
	FString Title;
	FString Body;
	/** Beat, Steer, Lit: landings needed. Chain: chain length needed. */
	int32 Goal = 1;
};

/** One judged landing, as the tutorial sees it. */
struct GFRHYTHM_API FGFAccelHopTutorialLanding
{
	/** Lesson of the tile (0..3), or INDEX_NONE for a tile that is not part of the tutorial. */
	int32 LessonIndex = INDEX_NONE;
	EGFJudgement Judgement = EGFJudgement::None;
	/** The landing paid the lit target. */
	bool bLit = false;
	/** Which side of the road the tile sits on: -1 left, 0 centre, +1 right (SideOf). */
	int32 Side = 0;
};

/** One player's progress (local to that player's machine). */
struct GFRHYTHM_API FGFAccelHopTutorialProgress
{
	/** Current lesson index; LessonCount once done. */
	int32 Lesson = 0;
	/** Beat, Steer, Lit: landings that counted. */
	int32 Count = 0;
	/** Side of the last counted landing in this lesson (Steer and Chain). */
	int32 LastSide = 0;
	/** Chain lesson: in-time landings in a row, the best so far, side switches and lit landings. */
	int32 Chain = 0;
	int32 BestChain = 0;
	int32 Zigs = 0;
	int32 Lits = 0;
	/** Bumped on every lesson change, so the HUD can flash. */
	int32 Serial = 0;
};

/** What the HUD draws. */
struct GFRHYTHM_API FGFAccelHopTutorialPrompt
{
	/** "1/4" .. "4/4", or "Done". */
	FString Step;
	FString Title;
	FString Body;
	/** The live counter line. */
	FString Goal;
	bool bDone = false;
};

struct GFRHYTHM_API FGFAccelHopTutorialRules
{
	static constexpr int32 LessonCount = 4;
	/** Tiles closer than this to the road's centre line (cm) are neither left nor right. */
	static constexpr float SideDeadZoneCm = 60.f;
	/** Chain lesson: also needs this many side switches and this many lit landings. */
	static constexpr int32 ChainZigGoal = 1;
	static constexpr int32 ChainLitGoal = 1;

	/** The four lessons, in teaching order. */
	static const TArray<FGFAccelHopLessonSpec>& Lessons();

	/** GFAccelHopTutorial.Lesson.<Id> for lesson Index, or None. */
	static FName LessonTag(int32 Index);

	/** The lesson a tile belongs to from its actor tags, or INDEX_NONE. */
	static int32 LessonFromTags(const TArray<FName>& Tags);

	/** Perfect or Good. */
	static bool IsInTime(EGFJudgement Judgement);

	/** -1, 0 or +1 from a tile's road-local Y (cm). */
	static int32 SideOf(float LocalY);

	/** Feeds one landing in. True when it finished the lesson (and moved on). */
	static bool OnLanding(FGFAccelHopTutorialProgress& Progress, const FGFAccelHopTutorialLanding& Landing);

	/** The player reached lesson Index (walked onto its stretch). Skips forward only. True when the lesson changed. */
	static bool OnReachLesson(FGFAccelHopTutorialProgress& Progress, int32 Index);

	/** Is the current lesson's goal met? */
	static bool IsLessonComplete(const FGFAccelHopTutorialProgress& Progress);

	static bool IsDone(const FGFAccelHopTutorialProgress& Progress) { return Progress.Lesson >= LessonCount; }

	static FGFAccelHopTutorialPrompt PromptFor(const FGFAccelHopTutorialProgress& Progress);

	/** Seconds the panel says "Nice!" after a lesson change. */
	static constexpr float FlashSeconds = 1.5f;

	/** The panel's lines, top to bottom: "<Step>  <Title>" (with "Nice!  " in front while bFlash), the body, the goal. */
	static TArray<FString> PromptLines(const FGFAccelHopTutorialPrompt& Prompt, bool bFlash);

	/**
	 * The lesson a road position is in: the last lesson whose first tile is at or before Along (cm along the road).
	 * StartAlong holds each lesson's first tile, in lesson order (MAX_flt for a lesson with no tiles). INDEX_NONE before the first.
	 */
	static int32 LessonAtAlong(const TArray<float>& StartAlong, float Along);

private:
	static void EnterLesson(FGFAccelHopTutorialProgress& Progress, int32 Index);
};
