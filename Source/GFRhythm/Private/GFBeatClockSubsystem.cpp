#include "GFBeatClockSubsystem.h"
#include "GFBuiltInSongs.h"
#include "GFRhythmSettings.h"
#include "GFWasapiLoopback.h"
#include "GFLiveAudio.h"
#include "GFLog.h"

UGFBeatClockSubsystem::UGFBeatClockSubsystem() = default;
UGFBeatClockSubsystem::~UGFBeatClockSubsystem() = default;

void UGFBeatClockSubsystem::StartMetronome(float InBpm)
{
	StopClock();
	Bpm = FMath::Clamp(InBpm, 30.f, 300.f);
	StartSeconds = FGFAudioClock::Now();
	OffsetSeconds = UGFRhythmSettings::GetMutable()->SongsOffsetMs / 1000.0;
	FirstBeatSeconds = 0.0;
	LastBeatIndex = -1;
	Source = EGFBeatSource::Metronome;
}

bool UGFBeatClockSubsystem::StartSong(FName SongId, EGFBeatDifficulty Difficulty)
{
	const FGFBeatmap* Map = FGFBuiltInSongs::Find(SongId, Difficulty);
	return Map ? StartBeatmap(*Map) : false;
}

bool UGFBeatClockSubsystem::StartBeatmap(const FGFBeatmap& Map)
{
	if (Map.Bpm <= 0.f)
	{
		return false;
	}
	StopClock();
	ActiveMap = Map;
	Bpm = Map.Bpm;
	StartSeconds = FGFAudioClock::Now();
	OffsetSeconds = UGFRhythmSettings::GetMutable()->SongsOffsetMs / 1000.0;
	FirstBeatSeconds = Map.OffsetMs / 1000.0;
	LastBeatIndex = -1;
	Source = EGFBeatSource::Song;
	return true;
}

TUniquePtr<IGFLiveAudioSource> UGFBeatClockSubsystem::MakeSource() const
{
	if (SourceFactory)
	{
		return SourceFactory();
	}
	return MakeUnique<FGFWasapiLoopback>();
}

void UGFBeatClockSubsystem::SetLiveStatus(EGFLiveStatus NewStatus)
{
	if (LiveStatus != NewStatus)
	{
		LiveStatus = NewStatus;
		OnLiveStatusChanged.Broadcast(NewStatus);
	}
}

bool UGFBeatClockSubsystem::StartLive()
{
	StopClock();
	const UGFRhythmSettings* Settings = UGFRhythmSettings::GetMutable();
	LiveSource = MakeSource();
	// Blocks until the device is open, so the sample rate below is the device's real one.
	if (!LiveSource || !LiveSource->StartCapture(Settings->ListenDeviceId, LiveStartTimeoutSeconds) || LiveSource->GetSampleRate() <= 0)
	{
		UE_LOG(LogGF, Warning, TEXT("Beat Hop live: couldn't open an output device"));
		LiveSource.Reset();
		SetLiveStatus(EGFLiveStatus::Failed);
		return false;
	}
	Tracker = FGFBeatTracker(LiveSource->GetSampleRate());
	RecoveryAttempt = 0;
	StartSeconds = FGFAudioClock::Now();
	OffsetSeconds = Settings->GetLiveLatencyMs() / 1000.0;
	FirstBeatSeconds = 0.0;
	LastBeatIndex = -1;
	Source = EGFBeatSource::Live;
	SetLiveStatus(EGFLiveStatus::Listening);
	return true;
}

void UGFBeatClockSubsystem::StopClock()
{
	if (LiveSource)
	{
		LiveSource->StopCapture();
		LiveSource.Reset();
	}
	TickOnsets.Reset();
	Source = EGFBeatSource::None;
	SetLiveStatus(EGFLiveStatus::Off);
}

void UGFBeatClockSubsystem::NoteGameSound()
{
	if (Source == EGFBeatSource::Live)
	{
		Tracker.NoteGameSound(FGFAudioClock::Now());
	}
}

bool UGFBeatClockSubsystem::DidLiveFallBackToDefault() const
{
	return LiveSource && LiveSource->DidFallBackToDefault();
}

void UGFBeatClockSubsystem::Deinitialize()
{
	StopClock();
	Super::Deinitialize();
}

double UGFBeatClockSubsystem::GetCorrectedNow() const
{
	return FGFAudioClock::Now() - OffsetSeconds;
}

double UGFBeatClockSubsystem::GetSongTimeSec() const
{
	return IsRunning() ? GetCorrectedNow() - StartSeconds : 0.0;
}

double UGFBeatClockSubsystem::GetCurrentBeat() const
{
	return IsRunning() ? TimeToBeat(GetSongTimeSec()) : -1.0;
}

float UGFBeatClockSubsystem::GetBPM() const
{
	return Source == EGFBeatSource::Live ? Tracker.GetBPM() : Bpm;
}

float UGFBeatClockSubsystem::GetConfidence() const
{
	return Source == EGFBeatSource::Live ? Tracker.GetConfidence() : (IsRunning() ? 1.f : 0.f);
}

double UGFBeatClockSubsystem::BeatToTime(double Beat) const
{
	if (Source == EGFBeatSource::Live)
	{
		const double T = Tracker.TimeOfBeatNumber(Beat);
		return T >= 0.0 ? T - StartSeconds : -1.0;
	}
	const float B = GetBPM();
	return B > 0.f ? FirstBeatSeconds + Beat * 60.0 / B : 0.0;
}

double UGFBeatClockSubsystem::TimeToBeat(double TimeSec) const
{
	if (Source == EGFBeatSource::Live)
	{
		return Tracker.BeatNumberAt(StartSeconds + TimeSec);
	}
	return (TimeSec - FirstBeatSeconds) * GetBPM() / 60.0;
}

void UGFBeatClockSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickOnsets.Reset();
	if (Source == EGFBeatSource::Live)
	{
		TickLiveSource();
	}
	if (Source == EGFBeatSource::Live && LiveSource && LiveStatus == EGFLiveStatus::Listening)
	{
		// One contiguous segment per read, each with its own timestamp (more than one only after a capture gap).
		double First = 0.0;
		for (int32 Segment = 0; Segment < 8 && LiveSource->ReadSamples(Scratch, First) > 0; ++Segment)
		{
			Tracker.ProcessSamples(Scratch.GetData(), Scratch.Num(), First);
		}
		TickOnsets = Tracker.ConsumeOnsets();
		for (const FGFOnset& O : TickOnsets)
		{
			OnOnset.Broadcast(O.Strength);
		}
		// Silence or lost lock: beats stop (confidence gate below); the track shows the fallback.
	}
	if (IsRunning() && GetBPM() > 0.f && GetConfidence() >= FGFBeatTracker::LockConfidence)
	{
		const double BeatNow = GetCurrentBeat();
		const int32 Beat = BeatNow >= 0.0 ? FMath::FloorToInt32(BeatNow) : -1;
		if (Beat != LastBeatIndex && Beat >= 0)
		{
			LastBeatIndex = Beat;
			OnBeat.Broadcast(Beat);
		}
	}
}

void UGFBeatClockSubsystem::TickLiveSource()
{
	const double Now = FGFAudioClock::Now();
	if (LiveStatus == EGFLiveStatus::Listening)
	{
		const EGFCaptureStatus S = LiveSource ? LiveSource->GetStatus() : EGFCaptureStatus::DeviceLost;
		if (S == EGFCaptureStatus::Capturing)
		{
			return;
		}
		// The device went away (unplugged, disabled, format change): reopen the system default after a short wait.
		UE_LOG(LogGF, Warning, TEXT("Beat Hop live: output device lost; trying the system default"));
		if (LiveSource)
		{
			LiveSource->StopCapture();
			LiveSource.Reset();
		}
		RecoveryAttempt = 0;
		NextRecoveryAt = Now + 0.5;
		SetLiveStatus(EGFLiveStatus::Recovering);
		return;
	}
	if (LiveStatus != EGFLiveStatus::Recovering)
	{
		return;
	}
	if (LiveSource)
	{
		// A reopen is in flight (BeginCapture doesn't block the game thread).
		const EGFCaptureStatus S = LiveSource->GetStatus();
		if (S == EGFCaptureStatus::Starting)
		{
			return;
		}
		if (S == EGFCaptureStatus::Capturing && LiveSource->GetSampleRate() > 0)
		{
			// Fresh tracker: the old lock belongs to a different device / gap in time.
			Tracker = FGFBeatTracker(LiveSource->GetSampleRate());
			LastBeatIndex = -1;
			++LiveRecoveries;
			UE_LOG(LogGF, Log, TEXT("Beat Hop live: listening again (%d Hz)"), LiveSource->GetSampleRate());
			SetLiveStatus(EGFLiveStatus::Listening);
			return;
		}
		LiveSource->StopCapture();
		LiveSource.Reset();
		NextRecoveryAt = Now + FMath::Min(4.0, 0.5 * FMath::Pow(2.0, static_cast<double>(RecoveryAttempt)));
	}
	if (RecoveryAttempt >= MaxLiveRecoveryAttempts)
	{
		UE_LOG(LogGF, Warning, TEXT("Beat Hop live: no output device after %d tries; live mode stopped"), RecoveryAttempt);
		StopClock();
		SetLiveStatus(EGFLiveStatus::Failed);
		return;
	}
	if (Now >= NextRecoveryAt)
	{
		++RecoveryAttempt;
		LiveSource = MakeSource();
		// The saved device is what went away, so recovery always uses the system default.
		if (!LiveSource || !LiveSource->BeginCapture(FString()))
		{
			LiveSource.Reset();
			NextRecoveryAt = Now + FMath::Min(4.0, 0.5 * FMath::Pow(2.0, static_cast<double>(RecoveryAttempt)));
		}
	}
}

TStatId UGFBeatClockSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGFBeatClockSubsystem, STATGROUP_Tickables);
}
