#pragma once

// Pure layout and label maths for AGFHUD. No widgets, no world. The canvas HUD calls these; tests lock them.

#include "CoreMinimal.h"
#include "GFTerritoryTypes.h"
#include "GFPathRules.h"
#include "GFQuestLine.h"
#include "GFTheftRules.h"
#include "GFMagicTypes.h"

namespace GFHUD
{
	/** Chat stays fully opaque for this long, then fades across ChatFadeSeconds. */
	constexpr float ChatHoldSeconds = 15.f;
	constexpr float ChatFadeSeconds = 1.5f;

	/** Controls hint hides itself after this long, until H sets an explicit choice. */
	constexpr float HintAutoHideSeconds = 30.f;

	/** Land notices and chat result codes stay on screen this long. */
	constexpr float ToastSeconds = 4.f;

	constexpr int32 VisibleChatLines = 8;

	/** Square slots: an icon, the key number top left, the count bottom right. */
	constexpr float HotbarSlotW = 56.f;
	constexpr float HotbarSlotH = 56.f;
	constexpr float HotbarGap = 4.f;
	constexpr float HotbarBottomMargin = 16.f;
	constexpr float HotbarSideMargin = 16.f;
	/** Floor used while squeezing the row onto a narrow canvas. The row still fits if this overflows. */
	constexpr float HotbarMinSlotW = 8.f;
	/** Element chip row above the slots ("Spark" plus the R / D-pad Right swap hint). Part of the hotbar bounds. */
	constexpr float HotbarBandH = 22.f;
	constexpr float HotbarBandGap = 8.f;
	/** Key hint for ToggleElement. GFGame.HUD.Hotbar.ElementChip checks it against the key table. */
	inline const TCHAR* ElementKeyHint()
	{
		return TEXT("R / D-pad Right");
	}

	/** Name-tag slots. AGFGameMode assigns ColorIndex from 0. The palette wraps. */
	constexpr int32 PlayerColorCount = 10;

	/** Villain skull and title hide past this. The class icon stays. */
	constexpr float VillainSightCm = 6000.f;

	/** Field HUD (tracker, spotted banner, interact prompt). Shared by the canvas and the overlap tests. */
	constexpr float FieldMargin = 16.f;
	constexpr float FieldTop = 48.f;
	constexpr float TrackerWidth = 360.f;
	constexpr float TrackerLineH = 18.f;
	constexpr float TrackerPad = 8.f;
	constexpr float SpottedWidth = 420.f;
	constexpr float SpottedHeight = 52.f;
	constexpr float InteractWidth = 300.f;
	constexpr float InteractHeight = 24.f;
	/** Top of the interact prompt sits this far below the screen center, clear of the crosshair and the stamina arc. */
	constexpr float InteractBelowCenter = 32.f;
	/** Half-size of the stamina keep-out. The arc is a 16 radius stroke with thickness 2. */
	constexpr float StaminaKeepHalf = 20.f;
	/** Half-size of the default crosshair keep-out (gap 4 plus size 10, plus the stroke). */
	constexpr float CrosshairKeepHalf = 18.f;

	inline const TCHAR* ControlsHint()
	{
		return TEXT("WASD move, Shift sprint, Ctrl dash, Q swing, E yank, Space jump, LMB dig, RMB place or aim, wheel or 1-9 hotbar, Alt+wheel zoom, MMB shoulder, V view, Enter chat, Esc menu, Home return");
	}
}

/** One hotbar cell in canvas pixels. bSelected is AGFCharacter::SelectedHotbarSlot. */
struct GFGAME_API FGFHUDSlotRect
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;
	bool bSelected = false;
};

/**
 * Land and state at the pawn.
 * Faction: "Rime · safe" or "Rime · unsafe". Neutral is "Neutral" (hazards do not apply there).
 * Wild with a hazard name: "Wild · LavaBridge". Otherwise "Wild".
 * HazardName is the hazard id, or its kind when the id is none. Ignored unless the zone is wild and bHazard is set.
 */
GFGAME_API FString GFFormatZoneLabel(EGFTerritoryZone Zone, FName FactionId, bool bSafe, bool bHazard, FName HazardName);

/** World-zone line. Tier 0 appends " · safe". Empty display name stays empty. */
GFGAME_API FString GFFormatWorldZoneLabel(const FString& DisplayName, int32 DangerTier);

/**
 * Compass icon X for a view-relative yaw. ±100 degrees spans the strip.
 * Yaw past that pins to the nearer edge, so a marker behind the player stays on the strip.
 */
GFGAME_API float GFCompassIconX(float RelativeYawDeg, float CanvasW);

/** 1 until ChatHoldSeconds, then a linear fade to 0. Negative ages stay fully visible. */
GFGAME_API float GFChatLineAlpha(float AgeSeconds);

/**
 * One centred row along the bottom. Slots keep HotbarSlotW until the row would pass the side margins, then they shrink.
 * SelectedIndex of INDEX_NONE, or any index outside the row, selects nothing.
 */
GFGAME_API TArray<FGFHUDSlotRect> GFLayoutHotbar(int32 Count, int32 SelectedIndex, float CanvasW, float CanvasH);

/** Palette colour for a player ColorIndex. A negative index (no player state) is white. */
GFGAME_API FLinearColor GFPlayerColor(int32 ColorIndex);

/** Auto-hide until the player presses H. After that, bUserWantsVisible sticks. */
GFGAME_API bool GFHintVisible(float ElapsedSeconds, bool bUserOverride, bool bUserWantsVisible);

struct GFGAME_API FGFNameplateText
{
	FString Line;
	FString PathIcon;
	FString VillainIcon;
	bool bShowPath = false;
	bool bShowVillain = false;
};

/** Class icon always (once picked). Villain skull and title only inside sight and only while armed. */
GFGAME_API FGFNameplateText GFFormatNameplate(const FString& Name, EGFPath Path, bool bVillain, const FString& Title, bool bWithinSight);

/** "VILLAIN" plus the disarm-lock countdown, or empty when it is off. */
GFGAME_API FString GFFormatVillainBadge(bool bOn, float LockSecondsLeft);

/** One line per class card, for the first-spawn pick. */
GFGAME_API TArray<FString> GFFormatPathCards(const TArray<FGFPathCard>& Cards);

/** Canvas box. An empty box (W or H at or below 0) never overlaps and counts as inside the canvas. */
struct GFGAME_API FGFHudRect
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;

	bool IsEmpty() const { return W <= 0.f || H <= 0.f; }
	float Right() const { return X + W; }
	float Bottom() const { return Y + H; }
};

/** True when the boxes share interior area. Shared edges do not count. */
GFGAME_API bool GFHudRectsOverlap(const FGFHudRect& A, const FGFHudRect& B);

/** True when Rect sits inside the canvas. An empty rect does. */
GFGAME_API bool GFHudRectInside(const FGFHudRect& Rect, float CanvasW, float CanvasH);

/** Bottom strip the hotbar row and its element chip occupy, full canvas width. */
GFGAME_API FGFHudRect GFHudHotbarBounds(float CanvasW, float CanvasH);

/** Y of the top of the hotbar bounds. The controls hint and the interact prompt stay above it. */
GFGAME_API float GFHudHotbarTop(float CanvasH);

/**
 * The row above the slots: the selected item's name on the left, the element chip on the right, centred as a pair.
 * Widths are measured text plus padding. A zero width leaves that box empty. Both stay inside the hotbar bounds.
 */
struct GFGAME_API FGFHotbarBandLayout
{
	FGFHudRect Item;
	FGFHudRect Element;
};

GFGAME_API FGFHotbarBandLayout GFLayoutHotbarBand(float CanvasW, float CanvasH, float ItemW, float ElementW);

/** Current magic element on the hotbar. bCanSwap is false while the faction element is Spark too (no faction yet). */
struct GFGAME_API FGFElementChip
{
	FString Label;
	FString Hint;
	FLinearColor Color = FLinearColor::White;
	bool bCanSwap = false;
};

GFGAME_API FString GFElementName(EGFMagicElement Element);
GFGAME_API FLinearColor GFElementColor(EGFMagicElement Element);
GFGAME_API FGFElementChip GFFormatElementChip(EGFMagicElement Current, EGFMagicElement FactionElement);

/** Square around the screen center that covers the stamina arc. */
GFGAME_API FGFHudRect GFHudStaminaBounds(float CanvasW, float CanvasH);

/** Square around the screen center that covers the default crosshair. */
GFGAME_API FGFHudRect GFHudCrosshairBounds(float CanvasW, float CanvasH);

/**
 * Quest tracker (top right), spotted banner (top center), interact prompt (under the crosshair).
 * A zero line count or a false flag leaves that box empty.
 */
struct GFGAME_API FGFFieldHudLayout
{
	FGFHudRect Tracker;
	FGFHudRect Spotted;
	FGFHudRect Interact;
};

GFGAME_API FGFFieldHudLayout GFLayoutFieldHud(float CanvasW, float CanvasH, int32 TrackerLines, bool bSpotted, bool bInteract);

/** Short step name. The CSV title is the line title, not this. Done is empty. */
GFGAME_API FString GFQuestStepTitle(EGFQuestStep Step);

/** Outsider, Initiate, Member, Champion, Leader. */
GFGAME_API FString GFFormatRankBadge(EGFFactionRank Rank);

/** Nameplate line plus the rank badge. */
GFGAME_API FString GFFormatNameplateWithRank(const FString& NameplateLine, EGFFactionRank Rank);

/**
 * Top-right tracker. bVisible is the quest body. Done collapses that body and leaves the rank badge.
 * bAllow false (pause, confirm, or the elder menu) hides both.
 */
struct GFGAME_API FGFQuestTrackerView
{
	bool bVisible = false;
	FString LineTitle;
	FString StepTitle;
	FString Progress;
	FString Hint;
	FString Rank;
	TArray<FString> Lines;
};

GFGAME_API FGFQuestTrackerView GFFormatQuestTracker(const FGFQuestCatalog& Catalog, EGFQuestStep Step, int32 Count, EGFFactionRank Rank, bool bAllow);

/**
 * Spotted, then the hire banner with the heat countdown. Clear, or bAllow false, hides it.
 * The theft rules own the clock. This only formats the snapshot.
 */
struct GFGAME_API FGFSpottedAlert
{
	bool bVisible = false;
	FString Headline;
	FString Detail;
};

GFGAME_API FGFSpottedAlert GFFormatSpottedAlert(EGFTheftHeat State, float SecondsLeft, bool bAllow);

/** "[X] Talk to Rime Elder" while an interactable is in reach and the field HUD is allowed. */
struct GFGAME_API FGFInteractPrompt
{
	bool bVisible = false;
	FString Line;
};

GFGAME_API FGFInteractPrompt GFFormatInteractPrompt(bool bInReach, const FString& KeyLabel, const FString& Verb, bool bAllow);
