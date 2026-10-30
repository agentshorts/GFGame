#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
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
 * Audio stays in memory (ring buffer) and is never recorded, saved or sent.
 *
 * Stub status: enumeration, default resolution and a polling capture thread (downmix to mono float) are in.
 * TODO(M5): resample to 48 kHz, MMCSS "Pro Audio" priority (avrt), IMMNotificationClient default-device follow,
 * lock-free ring, ignore onsets within 30 ms of game-triggered sounds.
 */
class GFRHYTHM_API FGFWasapiLoopback : public FRunnable
{
public:
	FGFWasapiLoopback();
	virtual ~FGFWasapiLoopback() override;

	/** Active render endpoints, preceded by the "System default output" entry (Id empty). */
	static TArray<FGFAudioOutputDevice> EnumerateOutputDevices();
	/** Endpoint ID of the current default render device (eConsole), or empty. */
	static FString GetDefaultOutputDeviceId();

	/** Starts capturing. Empty id = system default output. If the id no longer exists, falls back to default. */
	bool StartCapture(const FString& DeviceIdOrEmpty);
	void StopCapture();
	bool IsCapturing() const { return bCapturing; }
	/** True if a requested device was missing and the default was used instead (UI shows a toast). */
	bool DidFallBackToDefault() const { return bFellBack; }

	/** Moves captured mono samples into Out. Returns sample count. OutFirstSampleSeconds is on the FPlatformTime clock. */
	int32 ReadSamples(TArray<float>& Out, double& OutFirstSampleSeconds);
	int32 GetSampleRate() const { return SampleRate; }

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override { bStopRequested = true; }

private:
	FString RequestedDeviceId;
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bStopRequested = false;
	FThreadSafeBool bCapturing = false;
	FThreadSafeBool bFellBack = false;
	int32 SampleRate = 48000;

	FCriticalSection BufferLock;
	TArray<float> Buffer;
	double BufferStartSeconds = 0.0;
	static constexpr int32 MaxBufferedSeconds = 10;
};
