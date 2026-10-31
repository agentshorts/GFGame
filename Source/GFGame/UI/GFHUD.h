#pragma once

// Canvas HUD (crosshair, hotbar, zone, chat, toasts, Beat Hop judgement pops). No UMG. A one-line Slate text box is the chat entry.
// Common UI replaces this later.

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GFHUD.generated.h"

class UFont;
class UTexture2D;
struct FGHUDChat;
class SWidget;

struct FGFHUDToast
{
	FString Text;
	double ShownAt = 0.0;
};

UCLASS()
class GFGAME_API AGFHUD : public AHUD
{
	GENERATED_BODY()

public:
	AGFHUD();
	virtual ~AGFHUD() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void DrawHUD() override;

	void ToggleControlsHint();
	void DrawPathsAndVillains();
	bool IsAimSettingsOpen() const { return bAimSettingsOpen; }

	bool IsPauseMenuOpen() const { return bPauseMenuOpen; }
	void OpenPauseMenu();
	void ClosePauseMenu();
	void TogglePauseMenu();
	/** 0 Return to hub, 1 Settings, 2 Close, 3 Controls / How to get items. While a confirm is up: 0 Confirm, 1 Cancel. */
	void ActivatePauseEntry(int32 Index);
	/** Shows the runway item hint again (the Esc entry). */
	void ShowItemHowTo();
	static const TCHAR* PauseEntryLabel(int32 Index);
	FString GetPauseEntry(int32 Index) const;
	void ToggleAimSettings();
	void CloseAimSettings();

	bool IsChatOpen() const { return bChatOpen; }
	void OpenChat();
	/** Close without sending. */
	void CloseChat();
	/** Send the current line. An empty line only closes. */
	void SubmitChat();
	/**
	 * True for a couple of ticks after chat closes, so the Enter that submitted does not open a new line.
	 */
	bool ConsumeChatBlock();

private:
	void DrawCrosshair();
	void DrawAimOverlay();
	void RemoveAimSettings();
	void DrawCompass();
	void DrawStaminaArc();
	void DrawLandingMarker();
	void DrawZoneLabel();
	void DrawChat();
	void DrawToasts();
	void DrawHotbar();
	void DrawHint();
	/** Beat Hop: PERFECT / GOOD / MISS and COMBO xN over the brick, plus the lane sparkle (T_VFX_Judgement, T_VFX_Sparkle). */
	void DrawBeatHopPops();
	/** Beat Hop session (#212): the song pick, the run readout and the results panel. */
	void DrawBeatHopSession();
	/** Pebble Mine (#216): verb prompts while the local pawn is on the mine pad. */
	void DrawMineSession();
	/** Fashion runway: step list (current step highlighted) and the score, while the local pawn is on the site. */
	void DrawRunwaySession();
	/** Arena greybox: bout prompts while the local pawn is on the arena pad. */
	void DrawArenaSession();
	/** Grumble Dig greybox: start, dig, results, while the local pawn is on the porch site. */
	void DrawGrumbleSession();
	/** Hallow Night: start, finish, results and hub prompts while the local pawn is on the Depths pad. */
	void DrawDepthsSession();
	/** Ferry stand: fare, destinations and the lifetime pass while the local pawn is at a stop. */
	void DrawFerryStand();
	/** Mining range: items gained since reset, cells broken, and time to clear each row. */
	void DrawMiningRange();
	void DrawPauseMenu();

	void PollToasts();
	void NoteChatLine(int32 Seq, double Now);
	void DismissChat();
	void RemoveChatWidget();
	void SendChat(const FString& Line);

	UFont* HudFont() const;

	/** Slate entry. Incomplete here so this header does not include widget headers. */
	FGHUDChat* Chat = nullptr;
	bool bChatOpen = false;
	/** Survives the close frame so a second Enter in that frame does not open chat again. */
	int32 ChatBlockTicks = 0;

	double ShownAt = 0.0;
	bool bShownAtSet = false;
	bool bHintUserOverride = false;
	bool bHintUserVisible = true;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BeatJudgementTex;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BeatSparkleTex;
	bool bBeatTexLoaded = false;

	TMap<int32, double> ChatSeenAt;
	TArray<FGFHUDToast> Toasts;
	TSharedPtr<SWidget> AimSettingsRoot;
	bool bAimSettingsOpen = false;
	bool bPauseMenuOpen = false;
	/** Item hint on the runway. On until a show ends, and again when the pause menu asks. */
	bool bShowItemHowTo = true;
	bool bItemHowToPinned = false;
	int32 AppliedNotices = 0;
	int32 AppliedResults = 0;
};
