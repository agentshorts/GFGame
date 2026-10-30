#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GFBeatClock.generated.h"

UENUM(BlueprintType)
enum class EGFBeatSource : uint8
{
	None,
	/** Fixed-tempo metronome (fallback when live music stops; also used by tests). */
	Metronome,
	/** Built-in song (Songs mode). TODO(M5): Quartz start + audio render clock. */
	Song,
	/** Live loopback of the PC's audio output (any app) through FGFBeatTracker. */
	Live
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGFBeatClock : public UInterface
{
	GENERATED_BODY()
};

/**
 * One beat-time interface for both Songs and Live mode (PLAN.md M5), so the judge and the track
 * never care which mode is running. Times are seconds on the clock's own timeline, already
 * corrected by the player's calibration offset.
 */
class GFRHYTHM_API IGFBeatClock
{
	GENERATED_BODY()

public:
	virtual EGFBeatSource GetBeatSource() const = 0;
	virtual bool IsRunning() const = 0;
	/** Current time on the beat timeline (s). */
	virtual double GetSongTimeSec() const = 0;
	virtual double BeatToTime(double Beat) const = 0;
	virtual double TimeToBeat(double TimeSec) const = 0;
	virtual float GetBPM() const = 0;
	/** 0..1. Songs/metronome are always 1; live mode reports the tracker's lock confidence. */
	virtual float GetConfidence() const = 0;
};
