#include "GFBeatTracker.h"

namespace GFBeatTrack
{
	static constexpr double OnsetBiasFraction = 0.2; // peak flux frame centre sits ~0.2 x FFT before the hit
	static constexpr double MinOnsetGapSeconds = 0.06;
	static constexpr float OnsetOverMean = 3.5f; // a peak must stand this far above the last 0.5 s (rejects noise-floor flux)
	static constexpr double SupportWindowSeconds = 0.07;
	static constexpr double PllWindowFraction = 0.15;
	static constexpr float BandWeightLow = 1.0f;
	static constexpr float BandWeightMid = 0.6f;
	static constexpr float BandWeightHigh = 0.3f;
}

FGFBeatTracker::FGFBeatTracker(int32 InSampleRate)
	: SampleRate(FMath::Max(8000, InSampleRate))
{
	Window.SetNumUninitialized(FftSize);
	for (int32 i = 0; i < FftSize; ++i)
	{
		Window[i] = 0.5f - 0.5f * FMath::Cos(2.f * PI * i / (FftSize - 1));
	}
	const double BinHz = static_cast<double>(SampleRate) / FftSize;
	LowBin = FMath::Clamp(FMath::CeilToInt32(200.0 / BinHz), 2, FftSize / 2);
	MidBin = FMath::Clamp(FMath::CeilToInt32(2000.0 / BinHz), LowBin + 1, FftSize / 2);
	Reset();
}

void FGFBeatTracker::Reset()
{
	PrevLogMag.Reset();
	Pending.Reset();
	PendingStartSample = 0;
	TotalSamples = 0;
	ClockOffset = 0.0;
	Env.Reset();
	EnvTime.Reset();
	EnvBands.Reset();
	FramesSinceAnalysis = 0;
	LastOnsetTime = -1.0;
	EnvPeak = 0.f;
	NewOnsets.Reset();
	RecentOnsetTimes.Reset();
	GameSoundTimes.Reset();
	LevelDbfs = -120.f;
	SilentSeconds = 0.0;
	Period = 0.0;
	LastBeatSeconds = -1.0;
	BeatCount = 0;
	AcConfidence = 0.f;
	Support = 0.f;
	Confidence = 0.f;
}

void FGFBeatTracker::NoteGameSound(double Time)
{
	GameSoundTimes.Add(Time);
	if (GameSoundTimes.Num() > 64)
	{
		GameSoundTimes.RemoveAt(0, GameSoundTimes.Num() - 64);
	}
}

void FGFBeatTracker::ProcessSamples(const float* Samples, int32 Num, double FirstSampleSeconds)
{
	if (!Samples || Num <= 0)
	{
		return;
	}
	if (FirstSampleSeconds >= 0.0)
	{
		ClockOffset = FirstSampleSeconds - static_cast<double>(TotalSamples) / SampleRate;
	}

	double SumSq = 0.0;
	for (int32 i = 0; i < Num; ++i)
	{
		SumSq += static_cast<double>(Samples[i]) * Samples[i];
	}
	const double Rms = FMath::Sqrt(SumSq / Num);
	LevelDbfs = Rms > 1e-6 ? static_cast<float>(20.0 * FMath::LogX(10.0, Rms)) : -120.f;
	const double Seconds = static_cast<double>(Num) / SampleRate;
	SilentSeconds = LevelDbfs < -50.f ? SilentSeconds + Seconds : 0.0;

	Pending.Append(Samples, Num);
	TotalSamples += Num;

	int32 Read = 0;
	while (Pending.Num() - Read >= FftSize)
	{
		AnalyseFrame(Pending.GetData() + Read, PendingStartSample + Read);
		Read += HopSize;
	}
	if (Read > 0)
	{
		Pending.RemoveAt(0, Read, EAllowShrinking::No);
		PendingStartSample += Read;
	}

	AdvanceGrid(GetAnalysedUntil());
	if (IsSilent())
	{
		Support = FMath::Max(0.f, Support - static_cast<float>(Seconds));
		AcConfidence = FMath::Max(0.f, AcConfidence - static_cast<float>(Seconds));
	}
	Confidence = Period > 0.0 ? FMath::Clamp(0.5f * AcConfidence + 0.5f * Support, 0.f, 1.f) : 0.f;
}

void FGFBeatTracker::Fft(TArray<float>& Re, TArray<float>& Im) const
{
	const int32 N = Re.Num();
	for (int32 i = 1, j = 0; i < N; ++i)
	{
		int32 Bit = N >> 1;
		for (; j & Bit; Bit >>= 1)
		{
			j ^= Bit;
		}
		j ^= Bit;
		if (i < j)
		{
			Swap(Re[i], Re[j]);
			Swap(Im[i], Im[j]);
		}
	}
	for (int32 Len = 2; Len <= N; Len <<= 1)
	{
		const double Ang = -2.0 * PI / Len;
		const float WRe = static_cast<float>(FMath::Cos(Ang));
		const float WIm = static_cast<float>(FMath::Sin(Ang));
		for (int32 i = 0; i < N; i += Len)
		{
			float CurRe = 1.f, CurIm = 0.f;
			for (int32 k = 0; k < Len / 2; ++k)
			{
				const int32 A = i + k, B = i + k + Len / 2;
				const float TRe = Re[B] * CurRe - Im[B] * CurIm;
				const float TIm = Re[B] * CurIm + Im[B] * CurRe;
				Re[B] = Re[A] - TRe;
				Im[B] = Im[A] - TIm;
				Re[A] += TRe;
				Im[A] += TIm;
				const float NRe = CurRe * WRe - CurIm * WIm;
				CurIm = CurRe * WIm + CurIm * WRe;
				CurRe = NRe;
			}
		}
	}
}

void FGFBeatTracker::AnalyseFrame(const float* Frame, int64 FrameStartSample)
{
	TArray<float> Re, Im;
	Re.SetNumUninitialized(FftSize);
	Im.SetNumZeroed(FftSize);
	double SumSq = 0.0;
	for (int32 i = 0; i < FftSize; ++i)
	{
		Re[i] = Frame[i] * Window[i];
		SumSq += static_cast<double>(Frame[i]) * Frame[i];
	}
	const bool bQuiet = SumSq / FftSize < 1e-7; // about -70 dBFS
	Fft(Re, Im);

	const int32 Bins = FftSize / 2 + 1;
	const bool bHasPrev = PrevLogMag.Num() == Bins;
	PrevLogMag.SetNumZeroed(Bins);
	double Flux[3] = { 0.0, 0.0, 0.0 };
	for (int32 b = 1; b < Bins; ++b)
	{
		const float Mag = FMath::Sqrt(Re[b] * Re[b] + Im[b] * Im[b]);
		const float LogMag = FMath::Loge(1.f + 100.f * Mag);
		const float Diff = bHasPrev ? FMath::Max(0.f, LogMag - PrevLogMag[b]) : 0.f;
		PrevLogMag[b] = LogMag;
		Flux[b < LowBin ? 0 : (b < MidBin ? 1 : 2)] += Diff;
	}
	const float Low = static_cast<float>(Flux[0] / FMath::Max(1, LowBin - 1));
	const float Mid = static_cast<float>(Flux[1] / FMath::Max(1, MidBin - LowBin));
	const float High = static_cast<float>(Flux[2] / FMath::Max(1, Bins - MidBin));
	float Value = GFBeatTrack::BandWeightLow * Low + GFBeatTrack::BandWeightMid * Mid + GFBeatTrack::BandWeightHigh * High;
	if (bQuiet)
	{
		Value = 0.f;
	}

	const double CentreTime = ClockOffset + (static_cast<double>(FrameStartSample) + FftSize * 0.5) / SampleRate;
	Env.Add(Value);
	EnvTime.Add(CentreTime + GFBeatTrack::OnsetBiasFraction * FftSize / SampleRate);
	EnvBands.Add(FVector3f(Low * GFBeatTrack::BandWeightLow, Mid * GFBeatTrack::BandWeightMid, High * GFBeatTrack::BandWeightHigh));

	const int32 MaxFrames = FMath::CeilToInt32(WindowSeconds * SampleRate / HopSize);
	if (Env.Num() > MaxFrames + MaxFrames / 4)
	{
		const int32 Drop = Env.Num() - MaxFrames;
		Env.RemoveAt(0, Drop, EAllowShrinking::No);
		EnvTime.RemoveAt(0, Drop, EAllowShrinking::No);
		EnvBands.RemoveAt(0, Drop, EAllowShrinking::No);
	}

	EnvPeak = FMath::Max(EnvPeak * 0.999f, Value);
	PickOnset();

	if (++FramesSinceAnalysis >= FMath::Max(1, FMath::RoundToInt32(AnalysisIntervalSeconds * SampleRate / HopSize)))
	{
		FramesSinceAnalysis = 0;
		EstimateTempo();
	}
}

void FGFBeatTracker::PickOnset()
{
	const int32 K = Env.Num() - 1;
	const int32 C = K - 1;
	if (C < 2)
	{
		return;
	}
	const float V = Env[C];
	if (!(V > Env[C - 1] && V >= Env[K]))
	{
		return;
	}
	const int32 MeanFrames = FMath::Max(4, FMath::RoundToInt32(0.5 * SampleRate / HopSize));
	double Sum = 0.0;
	int32 Count = 0;
	for (int32 i = FMath::Max(0, C - MeanFrames); i < C; ++i)
	{
		Sum += Env[i];
		++Count;
	}
	const float LocalMean = Count > 0 ? static_cast<float>(Sum / Count) : 0.f;
	if (V <= GFBeatTrack::OnsetOverMean * LocalMean + 1e-4f || V < 0.1f * EnvPeak)
	{
		return;
	}
	const double T = EnvTime[C];
	if (LastOnsetTime >= 0.0 && T - LastOnsetTime < GFBeatTrack::MinOnsetGapSeconds)
	{
		return;
	}
	for (double G : GameSoundTimes)
	{
		if (FMath::Abs(T - G) <= IgnoreGameSoundSeconds)
		{
			return;
		}
	}
	LastOnsetTime = T;
	FGFOnset Onset;
	Onset.Time = T;
	Onset.Strength = V;
	const FVector3f B = EnvBands[C];
	const float Total = FMath::Max(1e-6f, B.X + B.Y + B.Z);
	Onset.Low = B.X / Total;
	Onset.Mid = B.Y / Total;
	Onset.High = B.Z / Total;
	OnOnset(Onset);
}

void FGFBeatTracker::OnOnset(const FGFOnset& Onset)
{
	NewOnsets.Add(Onset);
	if (NewOnsets.Num() > 512)
	{
		NewOnsets.RemoveAt(0, NewOnsets.Num() - 512);
	}
	RecentOnsetTimes.Add(Onset.Time);
	while (RecentOnsetTimes.Num() > 0 && RecentOnsetTimes[0] < Onset.Time - 4.0)
	{
		RecentOnsetTimes.RemoveAt(0);
	}

	// Phase-locked grid: an onset near a grid beat nudges the phase and, slowly, the period.
	if (Period > 0.0 && LastBeatSeconds >= 0.0)
	{
		const double N = FMath::RoundToDouble((Onset.Time - LastBeatSeconds) / Period);
		const double Grid = LastBeatSeconds + N * Period;
		const double Err = Onset.Time - Grid;
		if (FMath::Abs(Err) < GFBeatTrack::PllWindowFraction * Period)
		{
			LastBeatSeconds += 0.25 * Err;
			if (N >= 1.0)
			{
				Period = FMath::Clamp(Period + 0.05 * Err / N, 60.0 / MaxBpm, 60.0 / MinBpm);
			}
		}
	}
}

void FGFBeatTracker::AdvanceGrid(double Now)
{
	if (Period <= 0.0 || LastBeatSeconds < 0.0)
	{
		return;
	}
	// Move the anchor to the newest beat whose support window has fully passed, scoring each beat as we go.
	while (LastBeatSeconds + Period + GFBeatTrack::SupportWindowSeconds + 0.03 <= Now)
	{
		LastBeatSeconds += Period;
		++BeatCount;
		bool bSupported = false;
		for (double T : RecentOnsetTimes)
		{
			if (FMath::Abs(T - LastBeatSeconds) <= GFBeatTrack::SupportWindowSeconds)
			{
				bSupported = true;
				break;
			}
		}
		Support = FMath::Lerp(Support, bSupported ? 1.f : 0.f, 0.25f);
	}
}

void FGFBeatTracker::EstimateTempo()
{
	const double FrameRate = static_cast<double>(SampleRate) / HopSize;
	const int32 MaxFrames = FMath::CeilToInt32(WindowSeconds * FrameRate);
	const int32 N = FMath::Min(Env.Num(), MaxFrames);
	if (N < MinAnalysisSeconds * FrameRate || IsSilent())
	{
		return;
	}
	const int32 Start = Env.Num() - N;
	TArray<double> W;
	W.SetNumUninitialized(N);
	double Mean = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		Mean += Env[Start + i];
	}
	Mean /= N;
	double Ac0 = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		W[i] = Env[Start + i] - Mean;
		Ac0 += W[i] * W[i];
	}
	Ac0 /= N;
	if (Ac0 <= 1e-9)
	{
		return;
	}

	const int32 MinLag = FMath::FloorToInt32(FrameRate * 60.0 / MaxBpm);
	const int32 MaxLag = FMath::Min(FMath::CeilToInt32(FrameRate * 60.0 / MinBpm), N / 2);
	if (MaxLag <= MinLag + 2)
	{
		return;
	}
	TArray<double> Ac;
	Ac.SetNumZeroed(MaxLag + 2);
	double Best = 0.0;
	for (int32 L = MinLag - 1; L <= MaxLag + 1; ++L)
	{
		if (L <= 0 || L >= N)
		{
			continue;
		}
		double S = 0.0;
		for (int32 i = 0; i + L < N; ++i)
		{
			S += W[i] * W[i + L];
		}
		Ac[L] = S / (N - L);
		if (L >= MinLag && L <= MaxLag)
		{
			Best = FMath::Max(Best, Ac[L]);
		}
	}
	if (Best <= 0.0)
	{
		return;
	}
	// Fastest strong local peak (avoids half-tempo picks).
	int32 Pick = -1;
	for (int32 L = MinLag; L <= MaxLag; ++L)
	{
		if (Ac[L] >= Ac[L - 1] && Ac[L] >= Ac[L + 1] && Ac[L] >= 0.8 * Best)
		{
			Pick = L;
			break;
		}
	}
	if (Pick < 0)
	{
		return;
	}
	const double A = Ac[Pick - 1], B = Ac[Pick], C = Ac[Pick + 1];
	const double Den = A - 2.0 * B + C;
	const double Delta = FMath::Abs(Den) > 1e-12 ? FMath::Clamp(0.5 * (A - C) / Den, -0.5, 0.5) : 0.0;
	double LagFrames = Pick + Delta;
	// Octave correction: very fast picks become half tempo if the double lag is nearly as periodic.
	if (60.0 * FrameRate / LagFrames > 180.0)
	{
		const int32 Double = FMath::RoundToInt32(LagFrames * 2.0);
		if (Double + 1 < Ac.Num() && Ac[Double] >= 0.7 * B)
		{
			LagFrames *= 2.0;
		}
	}
	const double NewPeriod = LagFrames / FrameRate;
	AcConfidence = static_cast<float>(FMath::Clamp((B / Ac0 - 0.2) / 0.4, 0.0, 1.0));

	const bool bRelock = Period <= 0.0 || FMath::Abs(NewPeriod / Period - 1.0) > 0.08;
	if (!bRelock)
	{
		Period = FMath::Lerp(Period, NewPeriod, 0.1);
		return;
	}

	// (Re)seed the grid phase: comb over the envelope, ending at the newest frame.
	const double OldPeriod = Period;
	const int32 LagInt = FMath::Max(1, FMath::RoundToInt32(LagFrames));
	int32 BestPhase = 0;
	double BestScore = -1.0;
	for (int32 P = 0; P < LagInt; ++P)
	{
		double S = 0.0;
		for (double Pos = (N - 1) - P; Pos >= 0.0; Pos -= LagFrames)
		{
			const int32 I = Start + FMath::RoundToInt32(Pos);
			S += Env[I];
		}
		if (S > BestScore)
		{
			BestScore = S;
			BestPhase = P;
		}
	}
	const double NewLastBeat = EnvTime[Start + (N - 1) - BestPhase];
	// Keep beat numbers continuous across a re-lock (numbered on the old grid at the new anchor).
	const double OldBeatNumber = OldPeriod > 0.0 ? BeatNumberAt(NewLastBeat) : -1.0;
	Period = NewPeriod;
	LastBeatSeconds = NewLastBeat;
	BeatCount = OldBeatNumber >= 0.0 ? FMath::RoundToInt64(OldBeatNumber) : 0;
	Support = 0.5f;
}

double FGFBeatTracker::PredictNextBeat(double Now) const
{
	if (Period <= 0.0 || LastBeatSeconds < 0.0)
	{
		return -1.0;
	}
	const double K = FMath::FloorToDouble((Now - LastBeatSeconds) / Period) + 1.0;
	return LastBeatSeconds + K * Period;
}

double FGFBeatTracker::BeatNumberAt(double Time) const
{
	if (Period <= 0.0 || LastBeatSeconds < 0.0)
	{
		return -1.0;
	}
	return static_cast<double>(BeatCount) + (Time - LastBeatSeconds) / Period;
}

double FGFBeatTracker::TimeOfBeatNumber(double BeatNumber) const
{
	if (Period <= 0.0 || LastBeatSeconds < 0.0)
	{
		return -1.0;
	}
	return LastBeatSeconds + (BeatNumber - static_cast<double>(BeatCount)) * Period;
}

TArray<FGFOnset> FGFBeatTracker::ConsumeOnsets()
{
	TArray<FGFOnset> Out = MoveTemp(NewOnsets);
	NewOnsets.Reset();
	return Out;
}
