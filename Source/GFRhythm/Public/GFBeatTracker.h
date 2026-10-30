#pragma once

#include "CoreMinimal.h"

/** One detected onset (a drum hit, a note start). Times are on the clock the samples were stamped with. */
struct GFRHYTHM_API FGFOnset
{
	double Time = 0.0;
	/** Weighted spectral flux at the onset. */
	float Strength = 0.f;
	/** Share of the flux in each band (sums to ~1): low = kick, mid = snare/vocals, high = hats. */
	float Low = 0.f;
	float Mid = 0.f;
	float High = 0.f;
};

/**
 * Live beat and onset tracker for Beat Hop "Play to my music" (PLAN.md M5). Pure C++ (no engine objects), so
 * GFGame.Live.* tests feed it synthetic clips offline.
 *
 * - Onsets: multi-band spectral flux (FFT 1024, hop 256, Hann, log magnitude), adaptive threshold peak picking.
 * - Tempo: autocorrelation of the onset envelope over the last 8 s (60-200 BPM), parabolic peak refinement, and
 *   octave correction (prefer the fastest strong period, halve anything above 180 BPM if the half is as strong).
 * - Beats: a phase-locked grid seeded by a comb fit over the envelope and nudged by each on-beat onset.
 * - Confidence: periodicity strength x how many recent grid beats had an onset. Locked at >= 0.5.
 * Audio is only analysed in memory; nothing is stored beyond the last few seconds of envelope.
 */
class GFRHYTHM_API FGFBeatTracker
{
public:
	static constexpr int32 FftSize = 1024;
	static constexpr int32 HopSize = 256;
	static constexpr float MinBpm = 60.f;
	static constexpr float MaxBpm = 200.f;
	static constexpr double WindowSeconds = 8.0;
	static constexpr double MinAnalysisSeconds = 3.0;
	static constexpr double AnalysisIntervalSeconds = 0.25;
	static constexpr float LockConfidence = 0.5f;
	/** Onsets within this of a sound the game itself played are ignored (loopback hears the game too). */
	static constexpr double IgnoreGameSoundSeconds = 0.030;

	explicit FGFBeatTracker(int32 InSampleRate = 48000);

	void Reset();
	/** Feeds mono samples whose first sample was rendered at FirstSampleSeconds (negative = continue the stream clock). */
	void ProcessSamples(const float* Samples, int32 Num, double FirstSampleSeconds);
	/** Tell the tracker the game played a sound at Time, so its onset isn't mistaken for the music. */
	void NoteGameSound(double Time);

	float GetLevelDbfs() const { return LevelDbfs; }
	/** Below -50 dBFS for more than 1.5 s. */
	bool IsSilent() const { return SilentSeconds > 1.5; }
	float GetBPM() const { return Period > 0.0 ? static_cast<float>(60.0 / Period) : 0.f; }
	float GetConfidence() const { return Confidence; }
	bool IsLocked() const { return Confidence >= LockConfidence && Period > 0.0; }
	double GetBeatPeriod() const { return Period; }
	double GetLastBeatTime() const { return LastBeatSeconds; }
	int32 GetSampleRate() const { return SampleRate; }
	/** Clock time of the newest analysed audio. */
	double GetAnalysedUntil() const { return ClockOffset + static_cast<double>(TotalSamples) / SampleRate; }

	/** Predicted time (s) of the next beat after Now, or a negative value if there is no tempo yet. */
	double PredictNextBeat(double Now) const;
	/** Continuous beat count on the tracker's grid at Time (beat 0 = first lock), or a negative value if there is no tempo. */
	double BeatNumberAt(double Time) const;
	/** Clock time of a beat number on the current grid. */
	double TimeOfBeatNumber(double BeatNumber) const;

	/** Onsets found since the last call (oldest first). */
	TArray<FGFOnset> ConsumeOnsets();

private:
	void AnalyseFrame(const float* Frame, int64 FrameStartSample);
	void PickOnset();
	void EstimateTempo();
	void AdvanceGrid(double Now);
	void OnOnset(const FGFOnset& Onset);
	void Fft(TArray<float>& Re, TArray<float>& Im) const;

	int32 SampleRate;
	int32 LowBin = 0;
	int32 MidBin = 0;
	TArray<float> Window;
	TArray<float> PrevLogMag;

	// Input staging
	TArray<float> Pending;
	int64 PendingStartSample = 0;
	int64 TotalSamples = 0;
	double ClockOffset = 0.0;

	// Onset envelope (one value per hop)
	TArray<float> Env;
	TArray<double> EnvTime;
	TArray<FVector3f> EnvBands;
	int32 FramesSinceAnalysis = 0;
	double LastOnsetTime = -1.0;
	float EnvPeak = 0.f;
	TArray<FGFOnset> NewOnsets;
	TArray<double> RecentOnsetTimes;
	TArray<double> GameSoundTimes;

	// Level
	float LevelDbfs = -120.f;
	double SilentSeconds = 0.0;

	// Tempo and grid
	double Period = 0.0;
	double LastBeatSeconds = -1.0;
	int64 BeatCount = 0;
	float AcConfidence = 0.f;
	float Support = 0.f;
	float Confidence = 0.f;
};
