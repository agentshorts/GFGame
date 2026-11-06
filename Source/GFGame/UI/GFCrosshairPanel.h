#pragma once

#include "CoreMinimal.h"
#include "GFCrosshairTypes.h"
#include "Widgets/SCompoundWidget.h"

class AGFCharacter;

/** Esc, then Settings, then Crosshair. Live preview, share code, reset. No UMG. */
TSharedRef<SWidget> GFMakeAimSettingsPanel(AGFCharacter* Character, TFunction<void()> OnClose);

/** A short brightening of the live crosshair after a mining hit. The HUD reads Alpha while it paints. */
struct GFGAME_API FGFCrosshairTick
{
	static void Pulse(double NowSeconds, float DurationSeconds);
	static float Alpha(double NowSeconds);
	static void Reset();
};

/** Thickens and brightens Settings for the tick. Alpha 0 leaves them alone. */
GFGAME_API void GFApplyCrosshairTick(FGFCrosshairSettings& Settings, float Alpha);
