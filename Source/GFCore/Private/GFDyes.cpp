#include "GFDyes.h"

namespace GFDyes
{
	static const FName Rose(TEXT("RoseDye"));
	static const FName Sun(TEXT("SunDye"));
	static const FName Fern(TEXT("FernDye"));
	static const FName Sky(TEXT("SkyDye"));
	static const FName Plum(TEXT("PlumDye"));
	static const FName Chalk(TEXT("ChalkDye"));
}

const TArray<FName>& FGFDyes::AllDyeItems()
{
	static const TArray<FName> All = { GFDyes::Rose, GFDyes::Sun, GFDyes::Fern, GFDyes::Sky, GFDyes::Plum, GFDyes::Chalk };
	return All;
}

bool FGFDyes::IsDyeItem(FName ItemId)
{
	return AllDyeItems().Contains(ItemId);
}

bool FGFDyes::IsValidDyeColor(const FLinearColor& Color)
{
	for (const float V : { Color.R, Color.G, Color.B })
	{
		if (!FMath::IsFinite(V) || V < 0.f || V > 1.f)
		{
			return false;
		}
	}
	return true;
}

FName FGFDyes::DyeForColor(const FLinearColor& Color)
{
	const FLinearColor C(FMath::Clamp(Color.R, 0.f, 1.f), FMath::Clamp(Color.G, 0.f, 1.f), FMath::Clamp(Color.B, 0.f, 1.f));
	const FLinearColor Hsv = C.LinearRGBToHSV(); // R = hue (0..360), G = saturation, B = value
	if (Hsv.G < NeutralSaturation || Hsv.B < 0.05f)
	{
		return GFDyes::Chalk;
	}
	const float H = Hsv.R;
	if (H < 20.f || H >= 320.f) { return GFDyes::Rose; }  // reds and pinks
	if (H < 70.f) { return GFDyes::Sun; }                  // oranges and yellows
	if (H < 170.f) { return GFDyes::Fern; }                // greens
	if (H < 255.f) { return GFDyes::Sky; }                 // cyans and blues
	return GFDyes::Plum;                                   // purples
}

TArray<FGFItemStack> FGFDyes::CostForOutfitPiece(const FLinearColor& Color)
{
	return { FGFItemStack(DyeForColor(Color), PerOutfitPiece) };
}

TArray<FGFItemStack> FGFDyes::CostForBlocks(const FLinearColor& Color, int32 NumBlocks)
{
	if (NumBlocks <= 0)
	{
		return {};
	}
	return { FGFItemStack(DyeForColor(Color), FMath::DivideAndRoundUp(NumBlocks, BlocksPerDye)) };
}
