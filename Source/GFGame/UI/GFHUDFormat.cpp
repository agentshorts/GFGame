#include "GFHUDFormat.h"

namespace
{
	const TCHAR* Dot()
	{
		return TEXT(" \u00B7 ");
	}

	/** Whole numbers stay whole. Anything else keeps one decimal. A negative pool reads as 0. */
	FString MiningAmount(float Value)
	{
		const float Shown = FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : 0.f;
		const float Rounded = FMath::RoundToFloat(Shown);
		if (FMath::IsNearlyEqual(Shown, Rounded, 0.05f))
		{
			return FString::FromInt(FMath::RoundToInt(Rounded));
		}
		return FString::Printf(TEXT("%.1f"), Shown);
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

FGFNameplateText GFFormatNameplate(const FString& Name, EGFPath Path, bool bVillain, const FString& Title, bool bWithinSight)
{
	FGFNameplateText Out;
	const FString Icon = FGFPathRules::IconId(Path);
	Out.bShowPath = !Icon.IsEmpty();
	Out.PathIcon = Icon;
	Out.bShowVillain = bVillain && bWithinSight;
	Out.VillainIcon = Out.bShowVillain ? FString(TEXT("Villain.Skull")) : FString();
	FString Line = Name.IsEmpty() ? FString(TEXT("Someone")) : Name;
	if (Out.bShowPath)
	{
		Line += FString(TEXT(" [")) + FGFPathRules::DisplayName(Path) + TEXT("]");
	}
	if (Out.bShowVillain)
	{
		Line += TEXT(" *");
		if (!Title.IsEmpty())
		{
			Line += FString(TEXT(" ")) + Title;
		}
	}
	Out.Line = Line;
	return Out;
}

FString GFFormatVillainBadge(bool bOn, float LockSecondsLeft)
{
	if (!bOn)
	{
		return FString();
	}
	if (LockSecondsLeft > 0.f)
	{
		return FString::Printf(TEXT("VILLAIN %ds"), FMath::CeilToInt(LockSecondsLeft));
	}
	return TEXT("VILLAIN");
}

TArray<FString> GFFormatPathCards(const TArray<FGFPathCard>& Cards)
{
	TArray<FString> Lines;
	Lines.Add(TEXT("Pick a path"));
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		const FGFPathCard& Card = Cards[Index];
		Lines.Add(FString::Printf(TEXT("%d  %s"), Index + 1, *Card.Name));
		Lines.Add(Card.Blurb);
	}
	return Lines;
}

bool GFHudRectsOverlap(const FGFHudRect& A, const FGFHudRect& B)
{
	if (A.IsEmpty() || B.IsEmpty())
	{
		return false;
	}
	return A.X < B.Right() && B.X < A.Right() && A.Y < B.Bottom() && B.Y < A.Bottom();
}

bool GFHudRectInside(const FGFHudRect& Rect, float CanvasW, float CanvasH)
{
	if (Rect.IsEmpty())
	{
		return true;
	}
	return Rect.X >= -0.01f && Rect.Y >= -0.01f && Rect.Right() <= CanvasW + 0.01f && Rect.Bottom() <= CanvasH + 0.01f;
}

FGFHudRect GFHudHotbarBounds(float CanvasW, float CanvasH)
{
	FGFHudRect Rect;
	if (CanvasW <= 1.f || CanvasH <= 1.f)
	{
		return Rect;
	}
	Rect.X = 0.f;
	Rect.H = GFHUD::HotbarBottomMargin + GFHUD::HotbarSlotH + GFHUD::HotbarBandGap + GFHUD::HotbarBandH;
	Rect.W = CanvasW;
	Rect.Y = CanvasH - Rect.H;
	return Rect;
}

float GFHudHotbarTop(float CanvasH)
{
	return CanvasH - (GFHUD::HotbarBottomMargin + GFHUD::HotbarSlotH + GFHUD::HotbarBandGap + GFHUD::HotbarBandH);
}

FGFHotbarBandLayout GFLayoutHotbarBand(float CanvasW, float CanvasH, float ItemW, float ElementW)
{
	FGFHotbarBandLayout Out;
	if (CanvasW <= 1.f || CanvasH <= 1.f)
	{
		return Out;
	}
	constexpr float PairGap = 8.f;
	const float Avail = FMath::Max(0.f, CanvasW - 2.f * GFHUD::HotbarSideMargin);
	float Left = FMath::Max(0.f, ItemW);
	float Right = FMath::Max(0.f, ElementW);
	const float Gap = (Left > 0.f && Right > 0.f) ? PairGap : 0.f;
	const float Need = Left + Gap + Right;
	if (Need > Avail && Need > 0.f)
	{
		const float Scale = FMath::Max(0.f, Avail - Gap) / (Left + Right);
		Left *= Scale;
		Right *= Scale;
	}
	const float Total = Left + Gap + Right;
	const float X = (CanvasW - Total) * 0.5f;
	const float Y = FMath::Max(0.f, GFHudHotbarTop(CanvasH));
	if (Left > 0.f)
	{
		Out.Item = { X, Y, Left, GFHUD::HotbarBandH };
	}
	if (Right > 0.f)
	{
		Out.Element = { X + Left + Gap, Y, Right, GFHUD::HotbarBandH };
	}
	return Out;
}

FString GFElementName(EGFMagicElement Element)
{
	switch (Element)
	{
	case EGFMagicElement::Magma: return TEXT("Magma");
	case EGFMagicElement::Root: return TEXT("Root");
	case EGFMagicElement::Frost: return TEXT("Frost");
	default: return TEXT("Spark");
	}
}

FLinearColor GFElementColor(EGFMagicElement Element)
{
	switch (Element)
	{
	case EGFMagicElement::Magma: return FLinearColor(1.f, 0.42f, 0.12f);
	case EGFMagicElement::Root: return FLinearColor(0.42f, 0.86f, 0.32f);
	case EGFMagicElement::Frost: return FLinearColor(0.55f, 0.85f, 1.f);
	default: return FLinearColor(1.f, 0.93f, 0.45f);
	}
}

FGFElementChip GFFormatElementChip(EGFMagicElement Current, EGFMagicElement FactionElement)
{
	FGFElementChip Chip;
	Chip.Label = GFElementName(Current);
	Chip.Color = GFElementColor(Current);
	Chip.bCanSwap = FactionElement != EGFMagicElement::Spark;
	const EGFMagicElement Other = (Current == EGFMagicElement::Spark) ? FactionElement : EGFMagicElement::Spark;
	Chip.Hint = Chip.bCanSwap
		? FString::Printf(TEXT("[%s] swap to %s"), GFHUD::ElementKeyHint(), *GFElementName(Other))
		: FString::Printf(TEXT("[%s] join a faction to swap"), GFHUD::ElementKeyHint());
	return Chip;
}

namespace
{
	FGFHudRect CenterKeep(float CanvasW, float CanvasH, float Half)
	{
		FGFHudRect Rect;
		if (CanvasW <= 1.f || CanvasH <= 1.f || Half <= 0.f)
		{
			return Rect;
		}
		Rect.W = Half * 2.f;
		Rect.H = Half * 2.f;
		Rect.X = CanvasW * 0.5f - Half;
		Rect.Y = CanvasH * 0.5f - Half;
		return Rect;
	}
}

FGFHudRect GFHudStaminaBounds(float CanvasW, float CanvasH)
{
	return CenterKeep(CanvasW, CanvasH, GFHUD::StaminaKeepHalf);
}

FGFHudRect GFHudCrosshairBounds(float CanvasW, float CanvasH)
{
	return CenterKeep(CanvasW, CanvasH, GFHUD::CrosshairKeepHalf);
}

FGFMiningHudView GFFormatMiningHud(EGFMagicElement Element, float CrystalPool, int32 ClawCharges, bool bTargetActive, float TargetedFraction)
{
	FGFMiningHudView View;
	View.Element = Element;
	View.ElementLabel = GFElementName(Element);
	View.Color = GFElementColor(Element);
	View.CrystalPool = FMath::IsFinite(CrystalPool) ? CrystalPool : 0.f;
	View.ClawCharges = ClawCharges;
	View.CrystalLabel = FString::Printf(TEXT("Crystal %s"), *MiningAmount(View.CrystalPool));
	View.ClawLabel = FString::Printf(TEXT("Claw %d"), ClawCharges);
	if (bTargetActive)
	{
		View.bIntegrityVisible = true;
		View.IntegrityFraction = FMath::IsFinite(TargetedFraction) ? FMath::Clamp(TargetedFraction, 0.f, 1.f) : 0.f;
	}
	return View;
}

FGFMiningHudLayout GFLayoutMiningHud(float CanvasW, float CanvasH, bool bIntegrityVisible)
{
	FGFMiningHudLayout Out;
	if (CanvasW <= 1.f || CanvasH <= 1.f)
	{
		return Out;
	}
	const float CenterX = CanvasW * 0.5f;
	const float CenterY = CanvasH * 0.5f;
	Out.Readout.W = GFHUD::MiningReadoutW;
	Out.Readout.H = GFHUD::MiningReadoutH;
	Out.Readout.X = CenterX + GFHUD::StaminaKeepHalf + GFHUD::MiningReadoutGap;
	Out.Readout.Y = CenterY - Out.Readout.H * 0.5f;
	if (bIntegrityVisible)
	{
		Out.Integrity.W = GFHUD::IntegrityBarW;
		Out.Integrity.H = GFHUD::IntegrityBarH;
		Out.Integrity.X = CenterX - Out.Integrity.W * 0.5f;
		Out.Integrity.Y = CenterY + GFHUD::CrosshairKeepHalf + GFHUD::IntegrityBarGap;
	}
	return Out;
}

FGFFieldHudLayout GFLayoutFieldHud(float CanvasW, float CanvasH, int32 TrackerLines, bool bSpotted, bool bInteract)
{
	FGFFieldHudLayout Out;
	if (CanvasW <= 1.f || CanvasH <= 1.f)
	{
		return Out;
	}
	if (TrackerLines > 0)
	{
		const float Height = GFHUD::TrackerPad * 2.f + static_cast<float>(TrackerLines) * GFHUD::TrackerLineH;
		Out.Tracker.W = GFHUD::TrackerWidth;
		Out.Tracker.H = Height;
		Out.Tracker.X = CanvasW - GFHUD::FieldMargin - Out.Tracker.W;
		Out.Tracker.Y = GFHUD::FieldTop;
		const float Limit = CanvasH * 0.5f - GFHUD::StaminaKeepHalf - 8.f;
		if (Out.Tracker.Bottom() > Limit)
		{
			Out.Tracker.H = FMath::Max(0.f, Limit - Out.Tracker.Y);
		}
	}
	if (bSpotted)
	{
		Out.Spotted.W = GFHUD::SpottedWidth;
		Out.Spotted.H = GFHUD::SpottedHeight;
		Out.Spotted.X = (CanvasW - Out.Spotted.W) * 0.5f;
		Out.Spotted.Y = GFHUD::FieldTop;
	}
	if (bInteract)
	{
		Out.Interact.W = GFHUD::InteractWidth;
		Out.Interact.H = GFHUD::InteractHeight;
		Out.Interact.X = (CanvasW - Out.Interact.W) * 0.5f;
		Out.Interact.Y = CanvasH * 0.5f + GFHUD::InteractBelowCenter;
		const float HotbarTop = GFHudHotbarTop(CanvasH);
		if (Out.Interact.Bottom() > HotbarTop)
		{
			Out.Interact.Y = HotbarTop - Out.Interact.H - 8.f;
		}
		const float BelowKeep = CanvasH * 0.5f + GFHUD::StaminaKeepHalf + 8.f;
		if (Out.Interact.Y < BelowKeep)
		{
			Out.Interact.Y = BelowKeep;
		}
	}
	return Out;
}

FString GFQuestStepTitle(EGFQuestStep Step)
{
	switch (Step)
	{
	case EGFQuestStep::TakeCrystal: return TEXT("Take crystal");
	case EGFQuestStep::Escape: return TEXT("Get clear");
	case EGFQuestStep::TalkToElder: return TEXT("Talk to an elder");
	case EGFQuestStep::HuntHide: return TEXT("Hand in hide");
	case EGFQuestStep::HuntSkins: return TEXT("Hand in skins");
	case EGFQuestStep::CraftLook: return TEXT("Craft a look");
	case EGFQuestStep::WinShow: return TEXT("Win the show");
	default: return FString();
	}
}

FString GFFormatRankBadge(EGFFactionRank Rank)
{
	switch (Rank)
	{
	case EGFFactionRank::Initiate: return TEXT("Initiate");
	case EGFFactionRank::Member: return TEXT("Member");
	case EGFFactionRank::Champion: return TEXT("Champion");
	case EGFFactionRank::Leader: return TEXT("Leader");
	default: return TEXT("Outsider");
	}
}

FString GFFormatNameplateWithRank(const FString& NameplateLine, EGFFactionRank Rank)
{
	const FString Badge = GFFormatRankBadge(Rank);
	if (NameplateLine.IsEmpty())
	{
		return Badge;
	}
	return NameplateLine + TEXT(" \u00B7 ") + Badge;
}

FGFQuestTrackerView GFFormatQuestTracker(const FGFQuestCatalog& Catalog, EGFQuestStep Step, int32 Count, EGFFactionRank Rank, bool bAllow)
{
	FGFQuestTrackerView Out;
	Out.Rank = GFFormatRankBadge(Rank);
	if (!bAllow)
	{
		return Out;
	}
	if (Step == EGFQuestStep::Done)
	{
		Out.Lines.Add(Out.Rank);
		return Out;
	}
	Out.bVisible = true;
	Out.LineTitle = Catalog.TitleFor(Step);
	if (Out.LineTitle.IsEmpty())
	{
		Out.LineTitle = TEXT("The Elder's Errand");
	}
	Out.StepTitle = GFQuestStepTitle(Step);
	const int32 Goal = Catalog.GoalFor(Step);
	if (Goal > 0)
	{
		Out.Progress = FString::Printf(TEXT("%d/%d"), FMath::Max(0, Count), Goal);
	}
	Out.Hint = Catalog.HintFor(Step);
	Out.Lines.Add(Out.Rank);
	if (!Out.LineTitle.IsEmpty())
	{
		Out.Lines.Add(Out.LineTitle);
	}
	if (!Out.StepTitle.IsEmpty())
	{
		Out.Lines.Add(Out.StepTitle);
	}
	if (!Out.Progress.IsEmpty())
	{
		Out.Lines.Add(Out.Progress);
	}
	if (!Out.Hint.IsEmpty())
	{
		Out.Lines.Add(Out.Hint);
	}
	return Out;
}

FGFQuestTrackerView GFFormatSideQuestTracker(const FGFQuestCatalog& Catalog, EGFQuestStep Step, int32 Count, EGFFactionRank Rank,
	const FGFSideQuestCatalog& SideCatalog, const FGFSideTrackerInput& Side, bool bAllow)
{
	FGFQuestTrackerView Out = GFFormatQuestTracker(Catalog, Step, Count, Rank, bAllow);
	if (!bAllow || Step != EGFQuestStep::Done || Side.Status != EGFSideQuestStatus::Active)
	{
		return Out;
	}
	const FGFSideQuestLineRule* Line = SideCatalog.FindLine(Side.LineId);
	if (!Line)
	{
		return Out;
	}
	const FGFSideQuestStepRule* StepRule = Line->FindStep(Side.StepId);
	Out.bVisible = true;
	Out.LineTitle = Line->Title.IsEmpty() ? FString(TEXT("Town task")) : Line->Title;
	Out.StepTitle = StepRule ? StepRule->Title : FString();
	Out.Progress.Reset();
	Out.Hint.Reset();
	if (StepRule)
	{
		if (StepRule->Goal > 1)
		{
			Out.Progress = FString::Printf(TEXT("%d/%d"), FMath::Clamp(Side.Count, 0, StepRule->Goal), StepRule->Goal);
		}
		Out.Hint = StepRule->Hint;
	}
	Out.Lines.Reset();
	Out.Lines.Add(Out.Rank);
	for (const FString* Part : { &Out.LineTitle, &Out.StepTitle, &Out.Progress, &Out.Hint })
	{
		if (!Part->IsEmpty())
		{
			Out.Lines.Add(*Part);
		}
	}
	return Out;
}

FGFSpottedAlert GFFormatSpottedAlert(EGFTheftHeat State, float SecondsLeft, bool bAllow)
{
	FGFSpottedAlert Out;
	if (!bAllow || State == EGFTheftHeat::Clear)
	{
		return Out;
	}
	Out.bVisible = true;
	if (State == EGFTheftHeat::Hunted)
	{
		Out.Headline = TEXT("The elder hired dinos");
		Out.Detail = FString::Printf(TEXT("%ds"), FMath::Max(0, FMath::CeilToInt(SecondsLeft)));
		return Out;
	}
	Out.Headline = TEXT("Spotted! A villager saw you take crystal");
	return Out;
}

FGFInteractPrompt GFFormatInteractPrompt(bool bInReach, const FString& KeyLabel, const FString& Verb, bool bAllow)
{
	FGFInteractPrompt Out;
	if (!bAllow || !bInReach || KeyLabel.IsEmpty() || Verb.IsEmpty())
	{
		return Out;
	}
	Out.bVisible = true;
	Out.Line = FString::Printf(TEXT("[%s] %s"), *KeyLabel, *Verb);
	return Out;
}
