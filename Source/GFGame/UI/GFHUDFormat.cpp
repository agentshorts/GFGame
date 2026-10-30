#include "GFHUDFormat.h"

namespace
{
	const TCHAR* Dot()
	{
		return TEXT(" \u00B7 ");
	}
}

FString GFFormatZoneLabel(EGFTerritoryZone Zone, FName FactionId, bool bSafe, bool bHazard, FName HazardName)
{
	if (Zone == EGFTerritoryZone::Faction)
	{
		const FString Land = FactionId.IsNone() ? FString(TEXT("Land")) : FactionId.ToString();
		return Land + Dot() + (bSafe ? TEXT("safe") : TEXT("unsafe"));
	}
	if (Zone == EGFTerritoryZone::Neutral)
	{
		return TEXT("Neutral");
	}
	if (bHazard && !HazardName.IsNone())
	{
		return FString(TEXT("Wild")) + Dot() + HazardName.ToString();
	}
	return TEXT("Wild");
}

FString GFFormatWorldZoneLabel(const FString& DisplayName, int32 DangerTier)
{
	if (DisplayName.IsEmpty())
	{
		return FString();
	}
	if (DangerTier <= 0)
	{
		return DisplayName + Dot() + TEXT("safe");
	}
	return DisplayName;
}

float GFCompassIconX(float RelativeYawDeg, float CanvasW)
{
	constexpr float Pad = 16.f;
	constexpr float HalfAngle = 100.f;
	const float Clamped = FMath::Clamp(RelativeYawDeg, -HalfAngle, HalfAngle);
	const float T = (Clamped / HalfAngle + 1.f) * 0.5f;
	const float Right = FMath::Max(Pad, CanvasW - Pad);
	return FMath::Lerp(Pad, Right, T);
}

float GFChatLineAlpha(float AgeSeconds)
{
	if (AgeSeconds <= GFHUD::ChatHoldSeconds)
	{
		return 1.f;
	}
	if (GFHUD::ChatFadeSeconds <= 0.f)
	{
		return 0.f;
	}
	const float T = (AgeSeconds - GFHUD::ChatHoldSeconds) / GFHUD::ChatFadeSeconds;
	return FMath::Clamp(1.f - T, 0.f, 1.f);
}

TArray<FGFHUDSlotRect> GFLayoutHotbar(int32 Count, int32 SelectedIndex, float CanvasW, float CanvasH)
{
	TArray<FGFHUDSlotRect> Out;
	if (Count <= 0 || CanvasW <= 1.f || CanvasH <= 1.f)
	{
		return Out;
	}

	const float Side = GFHUD::HotbarSideMargin;
	const float Avail = FMath::Max(0.f, CanvasW - 2.f * Side);
	const int32 Gaps = Count - 1;
	float Gap = GFHUD::HotbarGap;
	float SlotW = GFHUD::HotbarSlotW;
	const float Natural = Count * SlotW + Gaps * Gap;
	if (Natural > Avail && Avail > 0.f)
	{
		SlotW = (Avail - Gaps * Gap) / static_cast<float>(Count);
		if (SlotW < GFHUD::HotbarMinSlotW)
		{
			SlotW = GFHUD::HotbarMinSlotW;
			Gap = Gaps > 0 ? (Avail - Count * SlotW) / static_cast<float>(Gaps) : 0.f;
			if (Gap < 0.f)
			{
				Gap = 0.f;
				SlotW = Avail / static_cast<float>(Count);
			}
		}
	}

	const float SlotH = GFHUD::HotbarSlotH;
	const float Total = Count * SlotW + Gaps * Gap;
	const float StartX = (CanvasW - Total) * 0.5f;
	const float Y = FMath::Max(0.f, CanvasH - GFHUD::HotbarBottomMargin - SlotH);

	Out.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FGFHUDSlotRect Slot;
		Slot.X = StartX + Index * (SlotW + Gap);
		Slot.Y = Y;
		Slot.W = SlotW;
		Slot.H = SlotH;
		Slot.bSelected = (Index == SelectedIndex);
		Out.Add(Slot);
	}
	return Out;
}

FLinearColor GFPlayerColor(int32 ColorIndex)
{
	static const FLinearColor Palette[] = {
		FLinearColor(0.90f, 0.28f, 0.24f),
		FLinearColor(0.26f, 0.52f, 0.95f),
		FLinearColor(0.22f, 0.74f, 0.38f),
		FLinearColor(0.96f, 0.76f, 0.22f),
		FLinearColor(0.66f, 0.36f, 0.92f),
		FLinearColor(0.95f, 0.50f, 0.18f),
		FLinearColor(0.20f, 0.80f, 0.78f),
		FLinearColor(0.95f, 0.42f, 0.64f),
		FLinearColor(0.62f, 0.86f, 0.24f),
		FLinearColor(0.42f, 0.40f, 0.92f),
	};
	static_assert(UE_ARRAY_COUNT(Palette) == GFHUD::PlayerColorCount, "GFPlayerColor palette must match PlayerColorCount");
	if (ColorIndex < 0)
	{
		return FLinearColor::White;
	}
	return Palette[ColorIndex % GFHUD::PlayerColorCount];
}

bool GFHintVisible(float ElapsedSeconds, bool bUserOverride, bool bUserWantsVisible)
{
	if (bUserOverride)
	{
		return bUserWantsVisible;
	}
	return ElapsedSeconds < GFHUD::HintAutoHideSeconds;
}
