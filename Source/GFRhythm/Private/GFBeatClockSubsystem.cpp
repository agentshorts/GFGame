#include "GFBeatClockSubsystem.h"
#include "GFRhythmSettings.h"
#include "GFWasapiLoopback.h"
#include "HAL/PlatformTime.h"

UGFBeatClockSubsystem::UGFBeatClockSubsystem() = default;
UGFBeatClockSubsystem::~UGFBeatClockSubsystem() = default;

void UGFBeatClockSubsystem::StartMetronome(float InBpm)
{
	StopClock();
	Bpm = FMath::Clamp(InBpm, 30.f, 300.f);
	StartSeconds = FPlatformTime::Seconds();
	OffsetSeconds = UGFRhythmSettings::GetMutable()->SongsOffsetMs / 1000.0;
	LastBeatIndex = -1;
	Source = EGFBeatSource::Metronome;
}

bool UGFBeatClockSubsystem::StartLive()
{
	StopClock();
	const UGFRhythmSettings* Settings = UGFRhythmSettings::GetMutable();
	Loopback = MakeUnique<FGFWasapiLoopback>();
	if (!Loopback->StartCapture(Settings->ListenDeviceId))
	{
		Loopback.Reset();
		return false;
	}
	Tracker = FGFBeatTracker(Loopback->GetSampleRate());
	StartSeconds = FPlatformTime::Seconds();
	OffsetSeconds = Settings->GetLiveLatencyMs() / 1000.0;
	LastBeatIndex = -1;
	Source = EGFBeatSource::Live;
	return true;
}

void UGFBeatClockSubsystem::StopClock()
{
	if (Loopback)
	{
		Loopback->StopCapture();
		Loopback.Reset();
	}
	Source = EGFBeatSource::None;
}

void UGFBeatClockSubsystem::Deinitialize()
{
	StopClock();
	Super::Deinitialize();
}

double UGFBeatClockSubsystem::GetSongTimeSec() const
{
	return IsRunning() ? FPlatformTime::Seconds() - StartSeconds - OffsetSeconds : 0.0;
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
	const float B = GetBPM();
	return B > 0.f ? Beat * 60.0 / B : 0.0;
}

double UGFBeatClockSubsystem::TimeToBeat(double TimeSec) const
{
	return TimeSec * GetBPM() / 60.0;
}

void UGFBeatClockSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Source == EGFBeatSource::Live && Loopback)
	{
		double First = 0.0;
		if (Loopback->ReadSamples(Scratch, First) > 0)
		{
			Tracker.ProcessSamples(Scratch.GetData(), Scratch.Num(), First);
		}
		// TODO(M5): coast on the last tempo, then pause judging when Tracker.IsSilent() or confidence stays low.
	}
	if (IsRunning() && GetBPM() > 0.f && GetConfidence() > 0.5f)
	{
		const int32 Beat = FMath::FloorToInt32(TimeToBeat(GetSongTimeSec()));
		if (Beat != LastBeatIndex && Beat >= 0)
		{
			LastBeatIndex = Beat;
			OnBeat.Broadcast(Beat);
		}
	}
}

TStatId UGFBeatClockSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGFBeatClockSubsystem, STATGROUP_Tickables);
}
