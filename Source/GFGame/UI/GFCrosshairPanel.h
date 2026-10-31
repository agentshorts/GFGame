#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AGFCharacter;

/** Esc → Settings → Crosshair. Live preview, share code, reset. No UMG. */
TSharedRef<SWidget> GFMakeAimSettingsPanel(AGFCharacter* Character, TFunction<void()> OnClose);
