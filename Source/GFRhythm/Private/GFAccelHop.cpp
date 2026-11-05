#include "GFAccelHop.h"
#include "Misc/PackageName.h"

FString FGFAccelHopRules::MeshPathFor(EGFHopTileKind Kind)
{
	const UEnum* Enum = StaticEnum<EGFHopTileKind>();
	const FString KindName = Enum ? Enum->GetNameStringByValue(static_cast<int64>(Kind)) : FString(TEXT("Standard"));
	const FString Name = FString::Printf(TEXT("SM_Hop_Tile_%s"), *KindName);
	return FString::Printf(TEXT("%s/%s.%s"), TileMeshDir, *Name, *Name);
}

bool FGFAccelHopRules::AssetExists(const FString& ObjectPath)
{
	// Checked before LoadObject so a map opened before the art import does not log missing-asset warnings.
	const FString Package = FPackageName::ObjectPathToPackageName(ObjectPath);
	return !Package.IsEmpty() && FPackageName::DoesPackageExist(Package);
}

FGFJudgeWindows FGFAccelHopRules::WindowsFor(EGFBeatHopMode Mode, const FGFAccelHopTuning& Tuning)
{
	return FGFJudgeWindows::For(Mode, Tuning.Difficulty);
}

FGFAccelHopHit FGFAccelHopRules::JudgeLanding(double NowBeat, float Bpm, const FGFJudgeWindows& Windows, int32 BeatsPerBar)
{
	FGFAccelHopHit Hit;
	if (NowBeat < 0.0 || Bpm <= 0.f)
	{
		return Hit;
	}
	const double Nearest = FMath::RoundToDouble(NowBeat);
	const double ErrorSeconds = (NowBeat - Nearest) * 60.0 / Bpm;
	Hit.BeatIndex = static_cast<int32>(Nearest);
	Hit.ErrorMs = static_cast<float>(ErrorSeconds * 1000.0);
	Hit.Judgement = FGFRhythmJudge::Classify(ErrorSeconds, Windows);
	const int32 Bar = FMath::Max(1, BeatsPerBar);
	Hit.bOnBarLine = Hit.BeatIndex % Bar == 0;
	return Hit;
}

float FGFAccelHopRules::GainFor(const FGFAccelHopHit& Hit, EGFHopTileKind Kind, const FGFAccelHopTuning& Tuning, bool bLit)
{
	float Gain = 0.f;
	switch (Hit.Judgement)
	{
	case EGFJudgement::Perfect: Gain = Tuning.PerfectBoost; break;
	case EGFJudgement::Good: Gain = Tuning.GoodBoost; break;
	default: return 0.f;
	}
	if (Kind == EGFHopTileKind::Boost)
	{
		Gain += Tuning.BoostTileBonus;
	}
	else if (Kind == EGFHopTileKind::Downbeat && Hit.bOnBarLine)
	{
		Gain += Tuning.DownbeatBonus;
	}
	if (bLit)
	{
		Gain *= FMath::Max(1.f, Tuning.LitBoostScale);
	}
	return FMath::Max(0.f, Gain);
}

bool FGFAccelHopRules::ApplyHit(FGFAccelHopBoost& State, const FGFAccelHopHit& Hit, EGFHopTileKind Kind, double Now, float BeatSeconds, const FGFAccelHopTuning& Tuning, bool bLit)
{
	if (Hit.Judgement == EGFJudgement::None)
	{
		return false;
	}
	if (Hit.Judgement == EGFJudgement::Miss)
	{
		if (Tuning.bMissBreaksChain)
		{
			State.Chain = 0;
		}
		return false;
	}
	const float Beat = ClampBeatSeconds(BeatSeconds);
	if (Hit.BeatIndex == State.LastBeatIndex && Now - State.LastHopTime < Beat)
	{
		return false; // the same beat twice (a bounce on the tile edge): one boost per beat
	}
	const float Current = MultiplierAt(State, Now);
	const float Gain = GainFor(Hit, Kind, Tuning, bLit);
	const float Cap = FMath::Max(1.f, Tuning.MaxMultiplier);
	if (Kind == EGFHopTileKind::Landing)
	{
		State.Multiplier = FMath::Min(Cap, FMath::Max(Current, 1.f + Gain));
		State.Chain = 0;
	}
	else
	{
		State.Multiplier = FMath::Min(Cap, Current + Gain);
		++State.Chain;
	}
	State.ExpiresAt = Now + FMath::Max(0.5f, Tuning.BoostBeats) * Beat;
	State.FadeSeconds = FMath::Clamp(Tuning.FadeBeats, 0.f, FMath::Max(0.5f, Tuning.BoostBeats)) * Beat;
	State.LastBeatIndex = Hit.BeatIndex;
	State.LastHopTime = Now;
	return true;
}

float FGFAccelHopRules::MultiplierAt(const FGFAccelHopBoost& State, double Now)
{
	if (State.Multiplier <= 1.f || Now >= State.ExpiresAt)
	{
		return 1.f;
	}
	const double Remaining = State.ExpiresAt - Now;
	if (State.FadeSeconds > 0.f && Remaining < State.FadeSeconds)
	{
		const float Alpha = static_cast<float>(Remaining / State.FadeSeconds);
		return FMath::Lerp(1.f, State.Multiplier, Alpha);
	}
	return State.Multiplier;
}

float FGFAccelHopRules::BeatPulse(double NowBeat)
{
	if (NowBeat < 0.0)
	{
		return 0.f;
	}
	const float Phase = static_cast<float>(NowBeat - FMath::FloorToDouble(NowBeat));
	const float Left = 1.f - Phase;
	return Left * Left * Left;
}

float FGFAccelHopRules::ClampBeatSeconds(float BeatSeconds)
{
	// The beat clock clamps BPM to 30..300, so a beat is 0.2..2 seconds.
	return FMath::Clamp(BeatSeconds, 0.2f, 2.f);
}

bool FGFAccelHopRules::ServerAccepts(const FGFAccelHopBoost& State, double Now, const FGFAccelHopTuning& Tuning)
{
	return Now - State.LastHopTime >= Tuning.MinHopGapSeconds;
}

float FGFAccelHopLitRules::WeightFor(EGFHopTileKind Kind, const FGFAccelHopLitTuning& Tuning)
{
	switch (Kind)
	{
	case EGFHopTileKind::Boost: return FMath::Max(0.f, Tuning.BoostWeight);
	case EGFHopTileKind::Downbeat: return FMath::Max(0.f, Tuning.DownbeatWeight);
	case EGFHopTileKind::Landing: return FMath::Max(0.f, Tuning.LandingWeight);
	default: return FMath::Max(0.f, Tuning.StandardWeight);
	}
}

float FGFAccelHopLitRules::Jitter(int32 Index, int32 Seed)
{
	// Small integer hash (no global random stream), so a pick is the same on every run of the same inputs.
	uint32 H = static_cast<uint32>(Index) * 2654435761u ^ (static_cast<uint32>(Seed) + 0x9E3779B9u) * 40503u;
	H ^= H >> 15;
	H *= 0x2C1B3C6Du;
	H ^= H >> 12;
	H *= 0x297A2D39u;
	H ^= H >> 15;
	return static_cast<float>(H & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

int32 FGFAccelHopLitRules::PickLitIndex(const TArray<FGFHopTileSlot>& Slots, float AnchorAlong, float Direction, int32 AvoidIndex, int32 Seed, const FGFAccelHopLitTuning& Tuning)
{
	const float Dir = Direction < 0.f ? -1.f : 1.f;
	const float Hop = FMath::Max(50.f, Tuning.HopLengthCm);
	const float MinAhead = FMath::Max(0.f, Tuning.MinAheadHops) * Hop;
	const float MaxAhead = FMath::Max(MinAhead + 1.f, FMath::Max(0.5f, Tuning.MaxAheadHops) * Hop);
	const float Prefer = FMath::Clamp(Tuning.PreferAheadHops * Hop, MinAhead, MaxAhead);
	const float Span = MaxAhead - MinAhead;
	int32 Best = INDEX_NONE;
	float BestScore = -1.f;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Index == AvoidIndex)
		{
			continue;
		}
		const float Ahead = (Slots[Index].Along - AnchorAlong) * Dir;
		if (Ahead < MinAhead || Ahead > MaxAhead)
		{
			continue;
		}
		const float Weight = WeightFor(Slots[Index].Kind, Tuning);
		if (Weight <= 0.f)
		{
			continue;
		}
		// Weight dominates (0.5..1 of it); the distance pull is at most 0.25, a tie-break between near equals.
		const float Score = Weight * (0.5f + 0.5f * Jitter(Index, Seed)) - 0.25f * FMath::Abs(Ahead - Prefer) / Span;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

bool FGFAccelHopLitRules::KeepLit(const TArray<FGFHopTileSlot>& Slots, int32 LitIndex, float AnchorAlong, float Direction, const FGFAccelHopLitTuning& Tuning)
{
	if (!Slots.IsValidIndex(LitIndex))
	{
		return false;
	}
	const float Dir = Direction < 0.f ? -1.f : 1.f;
	const float Ahead = (Slots[LitIndex].Along - AnchorAlong) * Dir;
	const float Far = FMath::Max(0.5f, Tuning.MaxAheadHops) * FMath::Max(50.f, Tuning.HopLengthCm) * 1.5f;
	return Ahead >= -FMath::Max(0.f, Tuning.KeepBehindCm) && Ahead <= Far;
}

float FGFAccelHopLitRules::LitPulse(double NowBeat, float PulseBeats)
{
	if (NowBeat < 0.0)
	{
		return 0.f;
	}
	const float Width = FMath::Clamp(PulseBeats, 0.05f, 1.f);
	const float Phase = static_cast<float>(NowBeat - FMath::FloorToDouble(NowBeat));
	if (Phase >= Width)
	{
		return 0.f;
	}
	const float Left = 1.f - Phase / Width;
	return Left * Left;
}
