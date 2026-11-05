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

float FGFAccelHopRules::GainFor(const FGFAccelHopHit& Hit, EGFHopTileKind Kind, const FGFAccelHopTuning& Tuning)
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
	return FMath::Max(0.f, Gain);
}

bool FGFAccelHopRules::ApplyHit(FGFAccelHopBoost& State, const FGFAccelHopHit& Hit, EGFHopTileKind Kind, double Now, float BeatSeconds, const FGFAccelHopTuning& Tuning)
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
	const float Gain = GainFor(Hit, Kind, Tuning);
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
