#include "GFBeatTracker.h"

void FGFBeatTracker::Reset()
{
	LevelDbfs = -120.f;
	SilentSeconds = 0.0;
	Bpm = 0.f;
	Confidence = 0.f;
	LastBeatSeconds = -1.0;
}

void FGFBeatTracker::ProcessSamples(const float* Samples, int32 Num, double /*FirstSampleSeconds*/)
{
	if (!Samples || Num <= 0)
	{
		return;
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
	// TODO(M5): onset detection + tempo/phase tracking. Until then the tracker never locks.
}

double FGFBeatTracker::PredictNextBeat(double Now) const
{
	if (Bpm <= 0.f || LastBeatSeconds < 0.0)
	{
		return -1.0;
	}
	const double Period = 60.0 / Bpm;
	const double N = FMath::FloorToDouble((Now - LastBeatSeconds) / Period) + 1.0;
	return LastBeatSeconds + N * Period;
}
