#pragma once

// Pure layout and label maths for AGFHUD. No widgets, no world. The canvas HUD calls these; tests lock them.

#include "CoreMinimal.h"
#include "GFTerritoryTypes.h"
#include "GFPathRules.h"

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

	constexpr float HotbarSlotW = 72.f;
	constexpr float HotbarSlotH = 28.f;
	constexpr float HotbarGap = 4.f;
	constexpr float HotbarBottomMargin = 16.f;
	constexpr float HotbarSideMargin = 16.f;
	/** Floor used while squeezing the row onto a narrow canvas. The row still fits if this overflows. */
	constexpr float HotbarMinSlotW = 8.f;

	/** Name-tag slots. AGFGameMode assigns ColorIndex from 0. The palette wraps. */
	constexpr int32 PlayerColorCount = 10;

	/** Villain skull and title hide past this. The class icon stays. */
	constexpr float VillainSightCm = 6000.f;

	inline const TCHAR* ControlsHint()
	{
		return TEXT("WASD move, Shift sprint, Ctrl dash, Q swing, E yank, Space jump, LMB dig, RMB place or aim, wheel block, Alt+wheel zoom, MMB shoulder, V view, Enter chat, Esc settings");
	}
}

/** One hotbar cell in canvas pixels. bSelected is the cell AGFCharacter::SelectedBlock matches. */
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
