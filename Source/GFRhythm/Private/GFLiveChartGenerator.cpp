#include "GFLiveChartGenerator.h"

void FGFLiveChartGenerator::Reset()
{
	Bricks.Reset();
	Onsets.Reset();
	NextBeatNumber = -1.0;
	NextId = 1;
	LastLane = Lanes / 2;
}

void FGFLiveChartGenerator::PruneBefore(double Time)
{
	Bricks.RemoveAll([Time](const FGFLiveBrick& B) { return B.Time < Time; });
}

bool FGFLiveChartGenerator::HasOffbeatActivity(double Period) const
{
	// Onsets about half-way between beats in the last ~2 s, more than a couple of times.
	int32 Count = 0;
	for (const FGFOnset& O : Onsets)
	{
		const double Phase = FMath::Fmod(O.Time, Period) / Period;
		Count += (Phase > 0.35 && Phase < 0.65) ? 1 : 0;
	}
	return Count >= 2;
}

int32 FGFLiveChartGenerator::PickLane(double BeatTime, double Period)
{
	// Band balance of onsets close to the same position one bar earlier (music repeats).
	float Low = 0.f, High = 0.f;
	for (const FGFOnset& O : Onsets)
	{
		if (FMath::Abs((O.Time + 4.0 * Period) - BeatTime) <= 0.25 * Period)
		{
			Low += O.Low;
			High += O.Mid + O.High;
		}
	}
	const int32 Centre = (Lanes - 1) / 2;
	int32 Lane = LastLane;
	if (Low >= High)
	{
		Lane += (Lane < Centre) ? 1 : (Lane > Centre + (Lanes % 2 == 0 ? 1 : 0) ? -1 : 0); // drift to the centre
	}
	else
	{
		Lane += (Lane <= Centre) ? -1 : 1; // drift outwards
	}
	Lane = FMath::Clamp(Lane, 0, Lanes - 1);
	LastLane = Lane;
	return Lane;
}

TArray<FGFLiveBrick> FGFLiveChartGenerator::Update(double Now, const FGFBeatTracker& Tracker, const TArray<FGFOnset>& NewOnsets)
{
	TArray<FGFLiveBrick> Created;
	Onsets.Append(NewOnsets);
	Onsets.RemoveAll([Now](const FGFOnset& O) { return O.Time < Now - 4.0; });

	// Follow tempo corrections, except for bricks inside the freeze window.
	for (FGFLiveBrick& B : Bricks)
	{
		if (B.bFrozen)
		{
			continue;
		}
		if (B.Time - Now > FreezeSeconds && Tracker.GetBeatPeriod() > 0.0)
		{
			B.Time = Tracker.TimeOfBeatNumber(B.BeatNumber);
		}
		if (B.Time - Now <= FreezeSeconds)
		{
			B.bFrozen = true;
		}
	}

	if (!Tracker.IsLocked())
	{
		return Created; // warm-up ("Listening...") or lost lock: no new bricks
	}
	const double Period = Tracker.GetBeatPeriod();
	const double EarliestBeat = Tracker.BeatNumberAt(Now + MinLeadSeconds);
	if (NextBeatNumber < 0.0 || NextBeatNumber < EarliestBeat)
	{
		// First lock, or the tempo jumped and we fell behind: restart at the first beat that still has full lead.
		const double Step = Difficulty == EGFBeatDifficulty::Easy ? 1.0 : 0.5;
		NextBeatNumber = FMath::CeilToDouble(EarliestBeat / Step) * Step;
	}

	const bool bOffbeats = Difficulty != EGFBeatDifficulty::Easy && HasOffbeatActivity(Period);
	while (true)
	{
		const double T = Tracker.TimeOfBeatNumber(NextBeatNumber);
		if (T > Now + LookaheadSeconds)
		{
			break;
		}
		const double Frac = NextBeatNumber - FMath::FloorToDouble(NextBeatNumber);
		const bool bOnBeat = Frac < 0.01;
		const bool bWant = bOnBeat || bOffbeats;
		if (bWant && T - Now >= MinLeadSeconds)
		{
			FGFLiveBrick B;
			B.Id = NextId++;
			B.BeatNumber = NextBeatNumber;
			B.Time = T;
			B.Lane = PickLane(T, Period);
			B.Type = EGFBeatNoteType::Tap;
			if (Difficulty == EGFBeatDifficulty::Hard && !bOnBeat)
			{
				B.Type = EGFBeatNoteType::Switch;
			}
			B.CreatedAt = Now;
			Bricks.Add(B);
			Created.Add(B);
		}
		NextBeatNumber += Difficulty == EGFBeatDifficulty::Easy ? 1.0 : 0.5;
	}
	return Created;
}
