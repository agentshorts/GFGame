#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFBeatClock.h"
#include "GFBeatmap.h"
#include "GFBeatTracker.h"
#include "GFWasapiLoopback.h"
#include "GFBeatClockSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnBeat, int32, BeatIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnOnset, float, Strength);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnLiveStatusChanged, EGFLiveStatus, Status);

/** Makes the live capture source (tests swap in a fake; empty = WASAPI loopback). */
using FGFLiveSourceFactory = TFunction<TUniquePtr<IGFLiveAudioSource>()>;

/**
 * World-level beat clock (PLAN.md M5), one interface for every mode:
 * - Metronome: fixed tempo on FPlatformTime (fallback and tests).
 * - Song: a built-in beatmap's tempo and offset, plus the player's Songs-mode calibration. (Greybox: no audio yet.)
 * - Live: WASAPI loopback of the "Listen to" output device (system default unless chosen) into FGFBeatTracker.
 *   Never a microphone. Beat numbers are on the tracker's grid; the per-device latency offset is applied.
 *
 * Every time here is on FGFAudioClock (FPlatformTime::Seconds): capture stamps are converted to it in the loopback.
 * If the device goes away mid-run, live mode reopens the system default output (a few tries with backoff), then
 * stops the clock and reports Failed.
 */
UCLASS()
class GFRHYTHM_API UGFBeatClockSubsystem : public UTickableWorldSubsystem, public IGFBeatClock
{
	GENERATED_BODY()

public:
	UGFBeatClockSubsystem();
	virtual ~UGFBeatClockSubsystem() override;

	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	void StartMetronome(float InBpm);

	/** Plays a built-in song's beatmap (tempo + offset). False if there is no such chart. */
	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	bool StartSong(FName SongId, EGFBeatDifficulty Difficulty);

	/** Same, for a beatmap the caller already has. */
	bool StartBeatmap(const FGFBeatmap& Map);

	/**
	 * Starts live mode on the device from UGFRhythmSettings (system default when empty). No mic. Waits (up to
	 * LiveStartTimeoutSeconds) until the device is capturing; false if it couldn't be opened.
	 */
	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	bool StartLive();

	/** Live mode as the player sees it (listening, recovering after the device went away, failed). */
	UFUNCTION(BlueprintPure, Category = "GF|Rhythm")
	EGFLiveStatus GetLiveStatus() const { return LiveStatus; }

	/** How many times live mode reopened a device after losing one (this session). */
	int32 GetLiveRecoveries() const { return LiveRecoveries; }

	/** Tests and tools: where live audio comes from (empty = WASAPI loopback). */
	void SetLiveSourceFactory(FGFLiveSourceFactory InFactory) { SourceFactory = MoveTemp(InFactory); }
	IGFLiveAudioSource* GetLiveSource() const { return LiveSource.Get(); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float LiveStartTimeoutSeconds = 2.f;

	/** Reopen attempts after a lost device before live mode gives up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	int32 MaxLiveRecoveryAttempts = 5;

	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	void StopClock();

	/** Tell live tracking the game itself just played a sound (so loopback doesn't count it as music). */
	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	void NoteGameSound();

	/** True if the saved "Listen to" device was gone and live mode fell back to the system default (show a toast). */
	UFUNCTION(BlueprintPure, Category = "GF|Rhythm")
	bool DidLiveFallBackToDefault() const;

	/** Current beat on the active grid (fractional). Negative while live mode is still listening. */
	UFUNCTION(BlueprintPure, Category = "GF|Rhythm")
	double GetCurrentBeat() const;

	/** Clock time "now" on the analysis clock, latency-corrected (live mode bricks are timed on this). */
	double GetCorrectedNow() const;

	UPROPERTY(BlueprintAssignable, Category = "GF|Rhythm")
	FGFOnBeat OnBeat;

	UPROPERTY(BlueprintAssignable, Category = "GF|Rhythm")
	FGFOnLiveStatusChanged OnLiveStatusChanged;

	/** Live mode only: an onset in the music. */
	UPROPERTY(BlueprintAssignable, Category = "GF|Rhythm")
	FGFOnOnset OnOnset;

	// IGFBeatClock
	virtual EGFBeatSource GetBeatSource() const override { return Source; }
	virtual bool IsRunning() const override { return Source != EGFBeatSource::None; }
	virtual double GetSongTimeSec() const override;
	virtual double BeatToTime(double Beat) const override;
	virtual double TimeToBeat(double TimeSec) const override;
	virtual float GetBPM() const override;
	virtual float GetConfidence() const override;

	// Tickable
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	const FGFBeatTracker& GetTracker() const { return Tracker; }
	const FGFBeatmap* GetActiveBeatmap() const { return Source == EGFBeatSource::Song ? &ActiveMap : nullptr; }
	/** Onsets seen this tick (live mode). */
	const TArray<FGFOnset>& GetTickOnsets() const { return TickOnsets; }

private:
	EGFBeatSource Source = EGFBeatSource::None;
	double StartSeconds = 0.0;
	double OffsetSeconds = 0.0;
	/** Beat 0 offset inside the song (chart offset_ms), seconds. */
	double FirstBeatSeconds = 0.0;
	float Bpm = 120.f;
	int32 LastBeatIndex = -1;
	FGFBeatmap ActiveMap;

	TUniquePtr<IGFLiveAudioSource> MakeSource() const;
	void SetLiveStatus(EGFLiveStatus NewStatus);
	void TickLiveSource();

	FGFLiveSourceFactory SourceFactory;
	TUniquePtr<IGFLiveAudioSource> LiveSource;
	EGFLiveStatus LiveStatus = EGFLiveStatus::Off;
	int32 LiveRecoveries = 0;
	int32 RecoveryAttempt = 0;
	double NextRecoveryAt = 0.0;
	FGFBeatTracker Tracker;
	TArray<float> Scratch;
	TArray<FGFOnset> TickOnsets;
};
