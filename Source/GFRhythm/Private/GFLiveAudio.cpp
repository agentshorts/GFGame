#include "GFLiveAudio.h"
#include "HAL/PlatformTime.h"

double FGFAudioClock::Now()
{
	return FPlatformTime::Seconds();
}

uint64 FGFAudioClock::QpcNow100ns()
{
	// Cycles64 is the raw performance counter on Windows (the counter WASAPI stamps packets with).
	const uint64 Cycles = FPlatformTime::Cycles64();
	const double SecondsPerCycle = FPlatformTime::GetSecondsPerCycle64();
	return static_cast<uint64>(static_cast<double>(Cycles) * SecondsPerCycle * 1e7);
}

double FGFAudioClock::MeasureQpcOffset()
{
	// Bracket the counter read with two clock reads so a context switch in between can't skew it.
	const double Before = Now();
	const uint64 Qpc = QpcNow100ns();
	const double After = Now();
	return 0.5 * (Before + After) - static_cast<double>(Qpc) * 1e-7;
}

bool FGFAudioClock::IsStampUsable(uint64 Qpc100ns, bool bStampValid, double QpcOffset, double NowSeconds)
{
	if (!bStampValid || Qpc100ns == 0)
	{
		return false;
	}
	const double T = FromQpc100ns(Qpc100ns, QpcOffset);
	return T <= NowSeconds + 0.05 && T >= NowSeconds - MaxStampSkewSeconds;
}

double FGFAudioClock::PacketStartTime(uint64 Qpc100ns, bool bStampValid, double QpcOffset, double NowSeconds, double PacketSeconds)
{
	if (!IsStampUsable(Qpc100ns, bStampValid, QpcOffset, NowSeconds))
	{
		return NowSeconds - FMath::Max(0.0, PacketSeconds);
	}
	return FromQpc100ns(Qpc100ns, QpcOffset);
}

void FGFAudioRing::Reset(int32 InCapacity, int32 InSampleRate)
{
	Data.SetNumZeroed(FMath::Max(1, InCapacity));
	SampleRate = FMath::Max(1, InSampleRate);
	Head = 0;
	Count = 0;
	StartSeconds = 0.0;
	Dropped = 0;
	Filled = 0;
	BreaksSeen = 0;
	Breaks.Reset();
}

void FGFAudioRing::DropOldest(int32 Num)
{
	Num = FMath::Min(Num, Count);
	if (Num <= 0)
	{
		return;
	}
	Head = (Head + Num) % Data.Num();
	Count -= Num;
	Dropped += Num;
	// The start time moves with the oldest sample, jumping to a later segment's own time if it reaches one.
	double NewStart = StartSeconds + static_cast<double>(Num) / SampleRate;
	int32 Gone = 0;
	for (FBreak& B : Breaks)
	{
		B.Offset -= Num;
		if (B.Offset <= 0)
		{
			NewStart = B.Seconds + static_cast<double>(-B.Offset) / SampleRate;
			++Gone;
		}
	}
	Breaks.RemoveAt(0, Gone);
	StartSeconds = NewStart;
}

void FGFAudioRing::Append(const float* Samples, int32 Num)
{
	const int32 Cap = Data.Num();
	const int32 Overflow = Count + Num - Cap;
	if (Overflow > 0)
	{
		DropOldest(Overflow);
	}
	const int32 Tail = (Head + Count) % Cap;
	const int32 First = FMath::Min(Num, Cap - Tail);
	if (Samples)
	{
		FMemory::Memcpy(Data.GetData() + Tail, Samples, First * sizeof(float));
		if (Num > First)
		{
			FMemory::Memcpy(Data.GetData(), Samples + First, (Num - First) * sizeof(float));
		}
	}
	else
	{
		FMemory::Memzero(Data.GetData() + Tail, First * sizeof(float));
		if (Num > First)
		{
			FMemory::Memzero(Data.GetData(), (Num - First) * sizeof(float));
		}
	}
	Count += Num;
}

void FGFAudioRing::Write(const float* Samples, int32 Num, double FirstSampleSeconds, bool bDiscontinuity)
{
	if (!Samples || Num <= 0 || Data.Num() == 0)
	{
		return;
	}
	const int32 Cap = Data.Num();
	if (Num >= Cap)
	{
		// Bigger than the whole ring: keep only the newest Cap samples.
		const int32 Skip = Num - Cap;
		Dropped += Count + Skip;
		Samples += Skip;
		FirstSampleSeconds += static_cast<double>(Skip) / SampleRate;
		Num = Cap;
		Head = 0;
		Count = 0;
		Breaks.Reset();
	}
	if (Count == 0)
	{
		StartSeconds = FirstSampleSeconds;
		Head = 0;
		Breaks.Reset();
		Append(Samples, Num);
		return;
	}
	// Where the last write ended, on its own segment's clock.
	const FBreak* Last = Breaks.Num() > 0 ? &Breaks.Last() : nullptr;
	const double SegStart = Last ? Last->Seconds : StartSeconds;
	const int32 SegOffset = Last ? Last->Offset : 0;
	const double Expected = SegStart + static_cast<double>(Count - SegOffset) / SampleRate;
	const double Gap = FirstSampleSeconds - Expected;
	if (!bDiscontinuity && FMath::Abs(Gap) <= ContiguousToleranceSeconds)
	{
		Append(Samples, Num);
		return;
	}
	const int32 FillSamples = FMath::RoundToInt32(Gap * SampleRate);
	if (Gap > 0.0 && Gap <= MaxSilenceFillSeconds && FillSamples > 0 && FillSamples + Num < Cap)
	{
		// Lost a little audio: pad with silence so sample counts stay on real time.
		Append(nullptr, FillSamples);
		Filled += FillSamples;
		Append(Samples, Num);
		return;
	}
	// A real jump: new segment with its own start time.
	++BreaksSeen;
	const int32 Overflow = Count + Num - Cap;
	if (Overflow > 0)
	{
		DropOldest(Overflow);
	}
	if (Count == 0)
	{
		StartSeconds = FirstSampleSeconds;
		Breaks.Reset();
	}
	else
	{
		Breaks.Add({ Count, FirstSampleSeconds });
	}
	Append(Samples, Num);
}

int32 FGFAudioRing::Read(TArray<float>& Out, double& OutFirstSampleSeconds)
{
	const int32 N = Breaks.Num() > 0 ? Breaks[0].Offset : Count;
	Out.SetNumUninitialized(N, EAllowShrinking::No);
	OutFirstSampleSeconds = StartSeconds;
	if (N == 0)
	{
		return 0;
	}
	const int32 Cap = Data.Num();
	const int32 First = FMath::Min(N, Cap - Head);
	FMemory::Memcpy(Out.GetData(), Data.GetData() + Head, First * sizeof(float));
	if (N > First)
	{
		FMemory::Memcpy(Out.GetData() + First, Data.GetData(), (N - First) * sizeof(float));
	}
	Head = (Head + N) % Cap;
	Count -= N;
	if (Breaks.Num() > 0)
	{
		StartSeconds = Breaks[0].Seconds;
		Breaks.RemoveAt(0);
		for (FBreak& B : Breaks)
		{
			B.Offset -= N;
		}
	}
	else
	{
		StartSeconds += static_cast<double>(N) / SampleRate;
		Head = 0;
	}
	return N;
}

bool IGFLiveAudioSource::StartCapture(const FString& DeviceIdOrEmpty, float TimeoutSeconds)
{
	if (!BeginCapture(DeviceIdOrEmpty))
	{
		return false;
	}
	if (WaitUntilReady(TimeoutSeconds) && GetStatus() == EGFCaptureStatus::Capturing && GetSampleRate() > 0)
	{
		return true;
	}
	StopCapture();
	return false;
}
