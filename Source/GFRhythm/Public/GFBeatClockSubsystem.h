#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFBeatClock.h"
#include "GFBeatTracker.h"
#include "GFWasapiLoopback.h"
#include "GFBeatClockSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnBeat, int32, BeatIndex);

/**
 * World-level beat clock (PLAN.md M5). Stub: metronome source works on FPlatformTime;
 * live source starts loopback capture of the "Listen to" output and feeds FGFBeatTracker.
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

	/** Starts live mode on the device from UGFRhythmSettings (system default when empty). No mic. */
	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	bool StartLive();

	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	void StopClock();

	UPROPERTY(BlueprintAssignable, Category = "GF|Rhythm")
	FGFOnBeat OnBeat;

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

private:
	EGFBeatSource Source = EGFBeatSource::None;
	double StartSeconds = 0.0;
	double OffsetSeconds = 0.0;
	float Bpm = 120.f;
	int32 LastBeatIndex = -1;

	TUniquePtr<FGFWasapiLoopback> Loopback;
	FGFBeatTracker Tracker;
	TArray<float> Scratch;
};
