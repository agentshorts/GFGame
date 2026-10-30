#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "GFLiveAudio.h"
#include <atomic>
#include "GFWasapiLoopback.generated.h"

/** One active audio OUTPUT (render) endpoint, for the "Listen to" dropdown. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFAudioOutputDevice
{
	GENERATED_BODY()

	/** WASAPI endpoint ID. Empty = the "System default output" pseudo-entry. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	FString FriendlyName;

	UPROPERTY(BlueprintReadOnly, Category = "GF|Rhythm")
	bool bIsSystemDefault = false;
};

/**
 * WASAPI loopback capture of an output device (Beat Hop live mode). Windows only; elsewhere every call fails cleanly.
 * Only render endpoints are enumerated or opened: no microphones, ever.
 * Audio stays in memory (a 10 s ring buffer) and is never recorded, saved or sent.
 *
 * StartCapture waits until the device is open (or has failed), so the sample rate is known when it returns true.
 * If the device goes away mid-run, the status becomes DeviceLost and the thread ends; UGFBeatClockSubsystem then
 * reopens the system default. Packet times are converted from QPC to FGFAudioClock (FPlatformTime::Seconds).
 *
 * TODO(M5): resample to 48 kHz, MMCSS "Pro Audio" priority (avrt), IMMNotificationClient default-device follow,
 * non-float32 mix formats.
 */
class GFRHYTHM_API FGFWasapiLoopback : public IGFLiveAudioSource, public FRunnable
{
public:
	FGFWasapiLoopback();
	virtual ~FGFWasapiLoopback() override;

	/** Active render endpoints, preceded by the "System default output" entry (Id empty). */
	static TArray<FGFAudioOutputDevice> EnumerateOutputDevices();
	/** Endpoint ID of the current default render device (eConsole), or empty. */
	static FString GetDefaultOutputDeviceId();
	/**
	 * Which endpoint a saved "Listen to" choice opens: the saved id while it is still an active output, else ""
	 * (the system default) with bOutFellBack set so the UI can show a toast. Empty saved id = system default, no fallback.
	 */
	static FString ResolveListenDevice(const FString& SavedId, const TArray<FGFAudioOutputDevice>& ActiveOutputs, bool& bOutFellBack);

	// IGFLiveAudioSource
	/** Starts the capture thread. Empty id = system default output. If the id no longer exists, falls back to default. */
	virtual bool BeginCapture(const FString& DeviceIdOrEmpty) override;
	virtual void StopCapture() override;
	virtual EGFCaptureStatus GetStatus() const override { return static_cast<EGFCaptureStatus>(Status.load()); }
	virtual int32 GetSampleRate() const override { return SampleRate.load(); }
	/** Moves captured mono samples into Out. Returns sample count. OutFirstSampleSeconds is on FGFAudioClock. */
	virtual int32 ReadSamples(TArray<float>& Out, double& OutFirstSampleSeconds) override;
	/** True if a requested device was missing and the default was used instead (UI shows a toast). */
	virtual bool DidFallBackToDefault() const override { return bFellBack; }

	bool IsCapturing() const { return GetStatus() == EGFCaptureStatus::Capturing; }
	/** Samples overwritten before the game read them (hitches). */
	int64 GetDroppedSamples() const;
	/** Packets the device flagged as a data discontinuity (glitches), since BeginCapture. */
	int32 GetDiscontinuities() const { return Discontinuities.load(); }
	/** Packets whose device timestamp was flagged bad or out of range, so their time came from the clock instead. */
	int32 GetBadTimestamps() const { return BadTimestamps.load(); }

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override { bStopRequested = true; }

protected:
	virtual bool WaitUntilReady(float TimeoutSeconds) override;

private:
	void SetStatus(EGFCaptureStatus NewStatus);

	FString RequestedDeviceId;
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bStopRequested = false;
	FThreadSafeBool bFellBack = false;
	std::atomic<int32> SampleRate{0};
	std::atomic<int32> Discontinuities{0};
	std::atomic<int32> BadTimestamps{0};
	std::atomic<uint8> Status{static_cast<uint8>(EGFCaptureStatus::Idle)};
	/** Triggered when the status leaves Starting (capturing or failed). */
	FEvent* ReadyEvent = nullptr;

	mutable FCriticalSection BufferLock;
	FGFAudioRing Ring;
	static constexpr int32 MaxBufferedSeconds = 10;
};
