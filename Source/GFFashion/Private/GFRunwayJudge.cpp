#include "GFRunwayJudge.h"
#include "Math/RandomStream.h"

const TArray<FGFRunwayJudgeProfile>& FGFRunwayJudge::DefaultPanel()
{
	auto Make = [](const TCHAR* Name, float Theme, float Harmony, float Complete, float Novelty)
	{
		FGFRunwayJudgeProfile P;
		P.Name = Name;
		P.ThemeWeight = Theme;
		P.HarmonyWeight = Harmony;
		P.CompletenessWeight = Complete;
		P.NoveltyWeight = Novelty;
		return P;
	};
	static const TArray<FGFRunwayJudgeProfile> Panel = {
		Make(TEXT("Juniper Quill"), 2.0f, 1.0f, 1.0f, 0.5f),      // lives for the theme
		Make(TEXT("Basil Bramblecoat"), 1.0f, 2.0f, 1.0f, 0.5f),  // colour first
		Make(TEXT("Pip Sparkletooth"), 1.0f, 0.5f, 1.0f, 2.0f),   // loves something new
	};
	return Panel;
}

float FGFRunwayJudge::ScoreTheme(const FGFOutfit& Outfit, const FGFFashionTheme& Theme)
{
	const int32 NumSlots = static_cast<int32>(EGFOutfitSlot::Count);
	double Points = 0.0;
	for (int32 i = 0; i < NumSlots; ++i)
	{
		const FGFFashionPiece* P = Outfit.GetPiece(static_cast<EGFOutfitSlot>(i));
		if (!P)
		{
			continue;
		}
		int32 Wanted = 0, Banned = 0;
		for (const FName& Tag : P->Tags)
		{
			Wanted += Theme.WantedTags.Contains(Tag) ? 1 : 0;
			Banned += Theme.BannedTags.Contains(Tag) ? 1 : 0;
		}
		Points += (Wanted > 0 ? 2.0 : 0.0) - 2.0 * Banned;
	}
	return static_cast<float>(FMath::Clamp(Points / (2.0 * NumSlots) * 10.0, 0.0, 10.0));
}

float FGFRunwayJudge::ScoreHarmony(const FGFOutfit& Outfit)
{
	TArray<float> Hues;
	for (int32 i = 0; i < static_cast<int32>(EGFOutfitSlot::Count); ++i)
	{
		FLinearColor C;
		if (Outfit.GetColor(static_cast<EGFOutfitSlot>(i), C))
		{
			const FLinearColor Hsv = C.LinearRGBToHSV();
			if (Hsv.G >= 0.2f && Hsv.B >= 0.15f)
			{
				Hues.Add(Hsv.R); // neutrals (greys, creams, near-black) go with anything
			}
		}
	}
	if (Outfit.IsEmpty())
	{
		return 0.f;
	}
	if (Hues.Num() <= 1)
	{
		return 8.f;
	}
	double Sum = 0.0;
	int32 Pairs = 0;
	for (int32 a = 0; a < Hues.Num(); ++a)
	{
		for (int32 b = a + 1; b < Hues.Num(); ++b)
		{
			float D = FMath::Abs(Hues[a] - Hues[b]);
			D = FMath::Min(D, 360.f - D);
			double Pair = 0.3;               // clash
			if (D <= 35.f) { Pair = 1.0; }   // analogous / matching
			else if (D >= 150.f) { Pair = 0.9; } // complementary
			else if (D >= 105.f && D <= 135.f) { Pair = 0.7; } // triadic
			Sum += Pair;
			++Pairs;
		}
	}
	return static_cast<float>(2.0 + 8.0 * Sum / Pairs);
}

float FGFRunwayJudge::ScoreCompleteness(const FGFOutfit& Outfit)
{
	return 10.f * Outfit.NumFilled() / static_cast<float>(EGFOutfitSlot::Count);
}

float FGFRunwayJudge::ScoreNovelty(const FGFOutfit& Outfit, const TArray<FGFOutfit>& History)
{
	const TArray<FName> Ids = Outfit.GetPieceIds();
	if (Ids.Num() == 0)
	{
		return 0.f;
	}
	int32 MaxShared = 0;
	for (const FGFOutfit& Past : History)
	{
		const TArray<FName> PastIds = Past.GetPieceIds();
		int32 Shared = 0;
		for (const FName& Id : Ids)
		{
			Shared += PastIds.Contains(Id) ? 1 : 0;
		}
		MaxShared = FMath::Max(MaxShared, Shared);
	}
	return 10.f * (1.f - static_cast<float>(MaxShared) / Ids.Num());
}

EGFRunwayTier FGFRunwayJudge::TierFor(float Total)
{
	if (Total >= 8.5f) { return EGFRunwayTier::S; }
	if (Total >= 7.0f) { return EGFRunwayTier::A; }
	if (Total >= 5.0f) { return EGFRunwayTier::B; }
	return EGFRunwayTier::C;
}

FGFRunwayScore FGFRunwayJudge::Judge(const FGFOutfit& Outfit, const FGFFashionTheme& Theme, const TArray<FGFOutfit>& History,
	int32 Seed, const FString& ContestantKey, const TArray<FGFRunwayJudgeProfile>& Panel)
{
	FGFRunwayScore S;
	S.Theme = ScoreTheme(Outfit, Theme);
	S.Harmony = ScoreHarmony(Outfit);
	S.Completeness = ScoreCompleteness(Outfit);
	S.Novelty = ScoreNovelty(Outfit, History);

	double Sum = 0.0;
	for (int32 j = 0; j < Panel.Num(); ++j)
	{
		const FGFRunwayJudgeProfile& P = Panel[j];
		const double W = FMath::Max(P.ThemeWeight + P.HarmonyWeight + P.CompletenessWeight + P.NoveltyWeight, KINDA_SMALL_NUMBER);
		double Card = (S.Theme * P.ThemeWeight + S.Harmony * P.HarmonyWeight + S.Completeness * P.CompletenessWeight + S.Novelty * P.NoveltyWeight) / W;
		// Seeded spread: stable per show, judge and contestant (FCrc is stable across runs and platforms).
		const uint32 Mix = HashCombine(HashCombine(static_cast<uint32>(Seed), FCrc::StrCrc32(*ContestantKey)), static_cast<uint32>(j + 1) * 2654435761u);
		FRandomStream Rng(static_cast<int32>(Mix));
		Card += Rng.FRandRange(-JudgeSpread, JudgeSpread);
		Card = FMath::RoundToDouble(FMath::Clamp(Card, 0.0, 10.0) * 10.0) / 10.0;
		S.JudgeCards.Add(static_cast<float>(Card));
		Sum += Card;
	}
	S.Total = Panel.Num() > 0 ? static_cast<float>(Sum / Panel.Num()) : 0.f;
	S.Tier = TierFor(S.Total);
	return S;
}

TArray<FGFItemStack> FGFRunwayJudge::RewardsFor(EGFRunwayTier Tier, bool bWinner, const FGFRunwayRewardRules& Rules)
{
	TArray<FGFItemStack> Out;
	switch (Tier)
	{
	case EGFRunwayTier::S: Out = Rules.S; break;
	case EGFRunwayTier::A: Out = Rules.A; break;
	case EGFRunwayTier::B: Out = Rules.B; break;
	default: Out = Rules.C; break;
	}
	if (bWinner && Rules.WinnerBonus.IsValid())
	{
		if (FGFItemStack* Existing = Out.FindByPredicate([&Rules](const FGFItemStack& St) { return St.ItemId == Rules.WinnerBonus.ItemId; }))
		{
			Existing->Count += Rules.WinnerBonus.Count;
		}
		else
		{
			Out.Add(Rules.WinnerBonus);
		}
	}
	return Out;
}
