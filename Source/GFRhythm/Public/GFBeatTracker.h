#pragma once

#include "CoreMinimal.h"

/**
 * Live beat/onset tracker (PLAN.md M5). Pure C++, no engine objects, so GFGame.Live.BeatTrack can run offline.
 * Stub: level metering + silence detection only.
 * TODO(M5): multi-band spectral flux onsets (FFT 1024 / hop 256), tempo comb filter 60-200 BPM with
 * octave correction, phase-locked beat prediction with confidence.
 */
class GFRHYTHM_API FGFBeatTracker
{
public:
	explicit FGFBeatTracker(int32 InSampleRate = 48000) : SampleRate(InSampleRate) {}

	void Reset();
	/** Feeds mono samples whose first sample was rendered at FirstSampleSeconds. */
	void ProcessSamples(const float* Samples, int32 Num, double FirstSampleSeconds);

	float GetLevelDbfs() const { return LevelDbfs; }
	/** Below -50 dBFS for more than 1.5 s. */
	bool IsSilent() const { return SilentSeconds > 1.5; }
	float GetBPM() const { return Bpm; }
	float GetConfidence() const { return Confidence; }
	/** Predicted time (s) of the next beat after Now, or a negative value if not locked. */
	double PredictNextBeat(double Now) const;

private:
	int32 SampleRate;
	float LevelDbfs = -120.f;
	double SilentSeconds = 0.0;
	float Bpm = 0.f;
	float Confidence = 0.f;
	double LastBeatSeconds = -1.0;
};
