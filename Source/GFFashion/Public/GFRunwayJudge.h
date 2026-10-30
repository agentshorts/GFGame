#pragma once

#include "CoreMinimal.h"
#include "GFFashionTypes.h"
#include "GFItemTypes.h"
#include "GFRunwayJudge.generated.h"

UENUM(BlueprintType)
enum class EGFRunwayTier : uint8
{
	C,
	B,
	A,
	S
};

/** One judge's taste: how much each part of the look matters to them. Weights are normalised. */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFRunwayJudgeProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion") float ThemeWeight = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion") float HarmonyWeight = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion") float CompletenessWeight = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion") float NoveltyWeight = 1.f;
};

/** Breakdown of one look (each part 0-10). */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFRunwayScore
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") float Theme = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") float Harmony = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") float Completeness = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") float Novelty = 0.f;
	/** One card per judge (0-10, one decimal). */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") TArray<float> JudgeCards;
	/** Mean of the cards. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") float Total = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") EGFRunwayTier Tier = EGFRunwayTier::C;
};

/** What each tier pays. Fashion pays in its own materials only (Fabric, Ribbon, Glitter). */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFRunwayRewardRules
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Fashion") TArray<FGFItemStack> C = { FGFItemStack(TEXT("Fabric"), 1) };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Fashion") TArray<FGFItemStack> B = { FGFItemStack(TEXT("Fabric"), 2), FGFItemStack(TEXT("Ribbon"), 1) };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Fashion") TArray<FGFItemStack> A = { FGFItemStack(TEXT("Fabric"), 3), FGFItemStack(TEXT("Ribbon"), 2), FGFItemStack(TEXT("Glitter"), 1) };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Fashion") TArray<FGFItemStack> S = { FGFItemStack(TEXT("Fabric"), 4), FGFItemStack(TEXT("Ribbon"), 3), FGFItemStack(TEXT("Glitter"), 2) };
	/** Extra for the top look when more than one walked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Fashion") FGFItemStack WinnerBonus = FGFItemStack(TEXT("Glitter"), 1);
};

/**
 * Runway judging (PLAN.md M3). Deterministic: the same outfit, theme, history and seed always score the same.
 * - Theme: pieces carrying a wanted tag score, banned tags cost double.
 * - Harmony: hue relationships between the coloured pieces (analogous and complementary read well; neutrals are free).
 * - Completeness: slots filled.
 * - Novelty: how different the look is from the player's earlier walks.
 * Each judge weights these their own way and adds a small seeded spread (+/-0.5) so the cards differ.
 */
struct GFFASHION_API FGFRunwayJudge
{
	static constexpr float JudgeSpread = 0.5f;

	static const TArray<FGFRunwayJudgeProfile>& DefaultPanel();

	static float ScoreTheme(const FGFOutfit& Outfit, const FGFFashionTheme& Theme);
	static float ScoreHarmony(const FGFOutfit& Outfit);
	static float ScoreCompleteness(const FGFOutfit& Outfit);
	static float ScoreNovelty(const FGFOutfit& Outfit, const TArray<FGFOutfit>& History);

	static FGFRunwayScore Judge(const FGFOutfit& Outfit, const FGFFashionTheme& Theme, const TArray<FGFOutfit>& History,
		int32 Seed, const FString& ContestantKey, const TArray<FGFRunwayJudgeProfile>& Panel = DefaultPanel());

	static EGFRunwayTier TierFor(float Total);
	static TArray<FGFItemStack> RewardsFor(EGFRunwayTier Tier, bool bWinner, const FGFRunwayRewardRules& Rules);
};
