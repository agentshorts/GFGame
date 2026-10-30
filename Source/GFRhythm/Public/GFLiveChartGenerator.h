#pragma once

#include "CoreMinimal.h"
#include "GFBeatmap.h"
#include "GFBeatTracker.h"

/** A brick generated live from predicted beats. */
struct GFRHYTHM_API FGFLiveBrick
{
	int32 Id = 0;
	/** Beat number on the tracker's grid. */
	double BeatNumber = 0.0;
	/** Clock time the brick is judged at. Fixed once bFrozen. */
	double Time = 0.0;
	int32 Lane = 0;
	EGFBeatNoteType Type = EGFBeatNoteType::Tap;
	/** Inside the freeze window: later tempo corrections never move it. */
	bool bFrozen = false;
	/** Clock time it was generated (for the look-ahead guarantee). */
	double CreatedAt = 0.0;
};

/**
 * Live mode course builder (PLAN.md M5): turns predicted beats into bricks about 1.5-2 s ahead, so they appear and
 * dim-glow before their beat. Bricks inside the last FreezeSeconds never move; earlier ones follow tempo corrections.
 * Lanes come from what the bands are doing (kick-heavy beats to the centre, snare/hats to the outside).
 * Difficulty sets density: Easy on-beat only, Normal adds off-beats when the music has them, Hard adds syncopation.
 */
class GFRHYTHM_API FGFLiveChartGenerator
{
public:
	double MinLeadSeconds = 1.5;
	double LookaheadSeconds = 2.0;
	double FreezeSeconds = 0.5;
	int32 Lanes = 4;
	EGFBeatDifficulty Difficulty = EGFBeatDifficulty::Normal;

	void Reset();
	/** Call every tick. Feeds the onsets the tracker found since the last call. Returns the bricks created this call. */
	TArray<FGFLiveBrick> Update(double Now, const FGFBeatTracker& Tracker, const TArray<FGFOnset>& NewOnsets);
	/** Drops bricks judged before Time (keeps the list short). */
	void PruneBefore(double Time);

	const TArray<FGFLiveBrick>& GetBricks() const { return Bricks; }
	bool IsGenerating() const { return NextBeatNumber >= 0.0; }

private:
	int32 PickLane(double BeatTime, double Period);
	bool HasOffbeatActivity(double Period) const;

	TArray<FGFLiveBrick> Bricks;
	TArray<FGFOnset> Onsets;
	double NextBeatNumber = -1.0;
	int32 NextId = 1;
	int32 LastLane = 1;
};
