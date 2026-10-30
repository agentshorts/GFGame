#pragma once

// Live mode audio plumbing shared by the WASAPI loopback, the beat clock and tests: one clock, a ring buffer,
// and the capture-source interface (so tests can feed audio without a sound card).
#include "CoreMinimal.h"
#include "GFLiveAudio.generated.h"

/** What a capture source is doing. */
UENUM(BlueprintType)
enum class EGFCaptureStatus : uint8
{
	Idle,
	/** Thread started, device not open yet. */
	Starting,
	Capturing,
	/** The device never opened (no output device, unsupported format, init error). */
	Failed,
	/** The device was open and went away (unplugged, disabled, format change). */
	DeviceLost
};

/** Live mode as the player sees it. */
UENUM(BlueprintType)
enum class EGFLiveStatus : uint8
{
	Off,
	Listening,
	/** The device went away; retrying on the system default output. */
	Recovering,
	/** Couldn't open any output device (live mode stopped). */
	Failed
};

/**
 * The one clock for Beat Hop live mode: FPlatformTime::Seconds(). Capture timestamps, the tracker, NoteGameSound,
 * GetCorrectedNow and the course's run time all use it.
 *
 * WASAPI stamps packets with the QPC position in 100 ns units. FPlatformTime::Seconds() is QPC-based too but adds a
 * fixed offset (16777216 s on Windows), so a raw QPC value is ~194 days away from it. FromQpc100ns adds back the
 * measured offset.
 */
struct GFRHYTHM_API FGFAudioClock
{
	/** Packet stamps further than this from "now" are treated as broken. */
	static constexpr double MaxStampSkewSeconds = 5.0;

	static double Now();
	/** The raw performance counter, in WASAPI's units (100 ns). */
	static uint64 QpcNow100ns();
	/** Now() minus the QPC time in seconds, measured now. */
	static double MeasureQpcOffset();
	/** Converts a QPC position (100 ns) to Now()'s clock. */
	static double FromQpc100ns(uint64 Qpc100ns, double QpcOffset) { return static_cast<double>(Qpc100ns) * 1e-7 + QpcOffset; }
	/**
	 * Time of a packet's first sample on Now()'s clock. Falls back to NowSeconds - PacketSeconds when the stamp is
	 * missing, flagged bad, in the future or more than MaxStampSkewSeconds old.
	 */
	static double PacketStartTime(uint64 Qpc100ns, bool bStampValid, double QpcOffset, double NowSeconds, double PacketSeconds);
	/** Whether PacketStartTime would use the device stamp (true) or fall back to the clock (false). */
	static bool IsStampUsable(uint64 Qpc100ns, bool bStampValid, double QpcOffset, double NowSeconds);
};

/**
 * Fixed-size mono sample ring. The capture thread writes, the game thread reads.
 * When full, the oldest samples are overwritten and the start time moves on with them. Not thread-safe by itself.
 *
 * Every write carries its own timestamp and is checked against where the previous one ended: within
 * ContiguousToleranceSeconds it simply continues; a small forward gap (or a device-flagged discontinuity with one)
 * is filled with silence so sample counts stay on real time; anything else (a big gap, a backwards jump, a flagged
 * discontinuity without a usable gap) starts a new segment. Read returns one contiguous segment at a time.
 */
class GFRHYTHM_API FGFAudioRing
{
public:
	FGFAudioRing() = default;
	FGFAudioRing(int32 InCapacity, int32 InSampleRate) { Reset(InCapacity, InSampleRate); }

	void Reset(int32 InCapacity, int32 InSampleRate);
	static constexpr double ContiguousToleranceSeconds = 0.002;
	static constexpr double MaxSilenceFillSeconds = 0.1;

	/**
	 * Appends Num samples whose first one was heard at FirstSampleSeconds. bDiscontinuity: the device says samples
	 * were lost before this packet (WASAPI AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY), so it never just continues.
	 */
	void Write(const float* Samples, int32 Num, double FirstSampleSeconds, bool bDiscontinuity = false);
	/**
	 * Moves the oldest contiguous segment into Out (replacing its contents). Returns the count; OutFirstSampleSeconds
	 * is Out[0]'s time. Call again for the next segment (0 when empty).
	 */
	int32 Read(TArray<float>& Out, double& OutFirstSampleSeconds);
	/** Segments started because of gaps or jumps (not counting the first). */
	int32 GetBreaks() const { return BreaksSeen; }
	/** Silence samples written to cover small gaps. */
	int64 GetFilled() const { return Filled; }

	int32 Num() const { return Count; }
	int32 Capacity() const { return Data.Num(); }
	/** Samples overwritten before anyone read them. */
	int64 GetDropped() const { return Dropped; }

private:
	TArray<float> Data;
	int32 Head = 0;
	int32 Count = 0;
	int32 SampleRate = 48000;
	double StartSeconds = 0.0;
	int64 Dropped = 0;
	int64 Filled = 0;
	int32 BreaksSeen = 0;

	/** A new segment starting Offset samples after Head, first heard at Seconds. */
	struct FBreak
	{
		int32 Offset;
		double Seconds;
	};
	TArray<FBreak> Breaks;

	/** Appends raw samples (null = silence), overwriting the oldest when full. */
	void Append(const float* Samples, int32 Num);
	void DropOldest(int32 Num);
};

/** Something that captures an output device's audio as mono float samples on FGFAudioClock. */
class GFRHYTHM_API IGFLiveAudioSource
{
public:
	virtual ~IGFLiveAudioSource() = default;
	/** Starts the device without waiting. Empty id = system default output. */
	virtual bool BeginCapture(const FString& DeviceIdOrEmpty) = 0;
	/** Starts and waits until the device is capturing (true) or has failed / timed out (false, and stopped). */
	virtual bool StartCapture(const FString& DeviceIdOrEmpty, float TimeoutSeconds = 2.f);
	virtual void StopCapture() = 0;
	virtual EGFCaptureStatus GetStatus() const = 0;
	/** 0 until the device is capturing. */
	virtual int32 GetSampleRate() const = 0;
	/** Moves captured samples into Out. Returns the count. */
	virtual int32 ReadSamples(TArray<float>& Out, double& OutFirstSampleSeconds) = 0;
	virtual bool DidFallBackToDefault() const { return false; }

protected:
	/** Blocks until the status leaves Starting or the timeout passes. */
	virtual bool WaitUntilReady(float TimeoutSeconds) = 0;
};
