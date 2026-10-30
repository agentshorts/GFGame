#include "GFRhythmJudge.h"
#include "GFBuiltInSongs.h"
#include "GFEconomy.h"
#include "Engine/DataTable.h"

FGFJudgeWindows FGFJudgeWindows::For(EGFBeatHopMode Mode, EGFBeatDifficulty Difficulty)
{
	FGFJudgeWindows W;
	const bool bEasy = Difficulty == EGFBeatDifficulty::Easy;
	if (Mode == EGFBeatHopMode::Live)
	{
		W.PerfectMs = bEasy ? 100.f : 80.f;
		W.GoodMs = bEasy ? 200.f : 160.f;
	}
	else
	{
		W.PerfectMs = bEasy ? 70.f : 50.f;
		W.GoodMs = bEasy ? 150.f : 110.f;
	}
	return W;
}

float FGFRhythmScore::Accuracy() const
{
	const int32 N = Judged();
	return N > 0 ? (Perfects + 0.75f * Goods) / static_cast<float>(N) : 0.f;
}

EGFJudgement FGFRhythmJudge::Classify(double ErrorSeconds, const FGFJudgeWindows& W)
{
	// Compare in whole microseconds so window edges are exact (no float drift at +/-50.000 ms).
	const int64 ErrUs = FMath::Abs(static_cast<int64>(FMath::RoundToDouble(ErrorSeconds * 1e6)));
	if (ErrUs <= static_cast<int64>(FMath::RoundToDouble(W.PerfectMs * 1000.0)))
	{
		return EGFJudgement::Perfect;
	}
	if (ErrUs <= static_cast<int64>(FMath::RoundToDouble(W.GoodMs * 1000.0)))
	{
		return EGFJudgement::Good;
	}
	return EGFJudgement::Miss;
}

int32 FGFRhythmJudge::MultiplierForCombo(int32 Combo)
{
	return FMath::Clamp(1 + FMath::Max(0, Combo) / ComboPerMultiplierStep, 1, MaxMultiplier);
}

EGFBeatHopGrade FGFRhythmJudge::GradeFor(const FGFRhythmScore& S)
{
	const float Acc = S.Accuracy();
	if (Acc >= 0.95f) { return EGFBeatHopGrade::S; }
	if (Acc >= 0.85f) { return EGFBeatHopGrade::A; }
	if (Acc >= 0.70f) { return EGFBeatHopGrade::B; }
	return EGFBeatHopGrade::C;
}

EGFJudgement FGFRhythmJudge::JudgeLanding(double LandingTime, int32 LandingLane, double NoteTime, int32 NoteLane)
{
	const EGFJudgement J = LandingLane != NoteLane ? EGFJudgement::Miss : Classify(LandingTime - NoteTime, Windows);
	Apply(J);
	return J;
}

void FGFRhythmJudge::AddMiss()
{
	Apply(EGFJudgement::Miss);
}

void FGFRhythmJudge::Apply(EGFJudgement J)
{
	switch (J)
	{
	case EGFJudgement::Perfect:
	case EGFJudgement::Good:
	{
		const int32 Mult = MultiplierForCombo(Score.Combo);
		Score.Score += (J == EGFJudgement::Perfect ? PerfectPoints : GoodPoints) * Mult;
		(J == EGFJudgement::Perfect ? Score.Perfects : Score.Goods)++;
		Score.Combo++;
		Score.BestCombo = FMath::Max(Score.BestCombo, Score.Combo);
		break;
	}
	case EGFJudgement::Miss:
		Score.Misses++;
		Score.Combo = 0;
		break;
	default:
		break;
	}
}

// ------------------------------------------------------------------------------------------------

EGFBrickLight FGFBrickLighting::LightAt(double NowBeat, double NoteBeat, double LenBeats, double WindowBeats, bool bHit, bool bMissed)
{
	if (bHit) { return EGFBrickLight::Hit; }
	if (bMissed) { return EGFBrickLight::Missed; }
	if (NowBeat < NoteBeat - DimLeadBeats) { return EGFBrickLight::Off; }
	if (NowBeat < NoteBeat - WindowBeats) { return EGFBrickLight::Dim; }
	if (NowBeat <= NoteBeat + FMath::Max(0.0, LenBeats) + WindowBeats) { return EGFBrickLight::Lit; }
	return EGFBrickLight::Missed;
}

float FGFBrickLighting::GlowAt(double NowBeat, double NoteBeat, double LenBeats, double WindowBeats, bool bHit, bool bMissed)
{
	switch (LightAt(NowBeat, NoteBeat, LenBeats, WindowBeats, bHit, bMissed))
	{
	case EGFBrickLight::Off: return 0.f;
	case EGFBrickLight::Dim:
	{
		const double T = FMath::Clamp((NowBeat - (NoteBeat - DimLeadBeats)) / FMath::Max(DimLeadBeats - WindowBeats, 0.01), 0.0, 1.0);
		return static_cast<float>(FMath::Lerp(0.15, 0.45, T));
	}
	case EGFBrickLight::Lit:
	{
		if (NowBeat >= NoteBeat && NowBeat <= NoteBeat + LenBeats) { return 1.f; }
		const double Dist = NowBeat < NoteBeat ? NoteBeat - NowBeat : NowBeat - (NoteBeat + LenBeats);
		return static_cast<float>(FMath::Lerp(1.0, 0.7, FMath::Clamp(Dist / FMath::Max(WindowBeats, 0.001), 0.0, 1.0)));
	}
	case EGFBrickLight::Hit: return 0.6f;
	default: return 0.05f;
	}
}

bool FGFBrickLighting::IsLandable(double NowBeat, double NoteBeat, double LenBeats, double WindowBeats)
{
	return LightAt(NowBeat, NoteBeat, LenBeats, WindowBeats, false, false) == EGFBrickLight::Lit;
}

// ------------------------------------------------------------------------------------------------

int32 FGFBeatHopRewards::Scale(int32 Count, bool bCozy, float CozyScale)
{
	if (Count <= 0) { return 0; }
	if (!bCozy) { return Count; }
	return FMath::Max(1, FMath::FloorToInt32(Count * CozyScale));
}

TArray<FGFItemStack> FGFBeatHopRewards::ForLanding(EGFJudgement Judgement, const FGFBeatHopRewardRules& Rules, bool bCozy)
{
	TArray<FGFItemStack> Out;
	const int32 Base = Judgement == EGFJudgement::Perfect ? Rules.PerfectLanding : (Judgement == EGFJudgement::Good ? Rules.GoodLanding : 0);
	const int32 N = Scale(Base, bCozy, Rules.CozyScale);
	if (N > 0 && !Rules.LandingItem.IsNone())
	{
		Out.Emplace(Rules.LandingItem, N);
	}
	return Out;
}

TArray<FGFItemStack> FGFBeatHopRewards::ForSongEnd(const FGFRhythmScore& Score, int32 TotalJudgedNotes, const FGFBeatHopRewardRules& Rules, bool bCozy)
{
	TArray<FGFItemStack> Out;
	if (Score.Judged() <= 0)
	{
		return Out; // nothing played, nothing paid
	}
	const int32 GradeIdx = static_cast<int32>(FGFRhythmJudge::GradeFor(Score));
	const int32 GradeBase = Rules.GradeYield.IsValidIndex(GradeIdx) ? Rules.GradeYield[GradeIdx] : 0;
	if (const int32 N = Scale(GradeBase, bCozy, Rules.CozyScale))
	{
		Out.Emplace(Rules.GradeItem, N);
	}
	if (Rules.ComboPerBonus > 0)
	{
		if (const int32 N = Scale(Score.BestCombo / Rules.ComboPerBonus, bCozy, Rules.CozyScale))
		{
			Out.Emplace(Rules.LandingItem, N);
		}
	}
	const bool bFullCombo = TotalJudgedNotes > 0 && Score.Misses == 0 && Score.Judged() >= TotalJudgedNotes;
	if (bFullCombo && !Rules.FullComboItem.IsNone() && !bCozy)
	{
		Out.Emplace(Rules.FullComboItem, 1); // the rare stays whole; Cozy (no-fail) runs don't earn it
	}
	return Out;
}

bool FGFBeatHopRewards::AreMaterialsOnly(const TArray<FGFItemStack>& Stacks, const UDataTable* Items, FString* OutBadId)
{
	for (const FGFItemStack& S : Stacks)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, S.ItemId);
		if (!Row || Row->Tier <= 0 || Row->MaterialCategory == EGFMaterialCategory::None || !S.IsValid())
		{
			if (OutBadId) { *OutBadId = S.ItemId.ToString(); }
			return false;
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------

bool FGFBeatHopSessionRules::IsModeAllowed(EGFBeatHopMode Mode, int32 NumPlayers, FString* OutReason)
{
	if (NumPlayers < 1)
	{
		if (OutReason) { *OutReason = TEXT("no players"); }
		return false;
	}
	if (Mode == EGFBeatHopMode::Live && NumPlayers > 1)
	{
		if (OutReason) { *OutReason = TEXT("Co-op plays built-in songs only. \"Play to my music\" is a solo mode."); }
		return false;
	}
	return true;
}

bool FGFBeatHopSessionRules::CanStart(EGFBeatHopMode Mode, int32 NumPlayers, FName SongId, EGFBeatDifficulty Difficulty, FString* OutReason)
{
	if (!IsModeAllowed(Mode, NumPlayers, OutReason))
	{
		return false;
	}
	if (Mode == EGFBeatHopMode::Songs && !FGFBuiltInSongs::Find(SongId, Difficulty))
	{
		if (OutReason) { *OutReason = FString::Printf(TEXT("no built-in song %s"), *SongId.ToString()); }
		return false;
	}
	return true;
}
