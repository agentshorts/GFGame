#pragma once

// Canvas HUD (crosshair, hotbar, zone, chat, toasts, quest tracker, spotted alert, interact prompt, Beat Hop judgement pops). No UMG. A one-line Slate text box is the chat entry.
// Common UI replaces this later.

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GFInventoryScreen.h"
#include "GFHUD.generated.h"

class UFont;
class UTexture2D;
class AGFElder;
struct FGHUDChat;
class SWidget;

/** One panel at a time. PauseConfirm is the hub return prompt. Elder is the town elder menu. */
UENUM()
enum class EGFHudModal : uint8
{
	Game,
	Pause,
	PauseConfirm,
	Chat,
	Aim,
	Elder,
	/** The bag (Tab or I): frees the mouse and holds the pawn, the field HUD stays up. */
	Inventory
};

/** The only inputs to AGFHUD::SetHudModal. */
UENUM()
enum class EGFHudModalEvent : uint8
{
	TogglePause,
	OpenPause,
	ClosePause,
	OpenConfirm,
	CancelConfirm,
	OpenChat,
	CloseChat,
	OpenAim,
	CloseAim,
	OpenElder,
	CloseElder,
	Escape,
	/** Teardown. Any modal goes back to the game. */
	Reset,
	OpenInventory,
	CloseInventory,
	/** Tab or I: opens the bag from the game, closes it when it is up. Ignored over other menus. */
	ToggleInventory
};

/** Pages inside the elder menu. Not a second modal. */
UENUM()
enum class EGFElderTab : uint8
{
	Talk,
	Trade,
	Book
};

/** Pure transition for the HUD modal. Tests drive this with no world. */
struct GFGAME_API FGFHudModalRules
{
	struct FState
	{
		EGFHudModal Modal = EGFHudModal::Game;
		/** Where chat returns. Game for every other modal. */
		EGFHudModal Restore = EGFHudModal::Game;
	};

	static FState Transition(EGFHudModal Modal, EGFHudModal Restore, EGFHudModalEvent Event);
	static bool IsPause(EGFHudModal Modal);
	/** Pawn look and move stay off while a menu is up, including chat opened over one. */
	static bool HoldsPawn(EGFHudModal Modal, EGFHudModal Restore);
	/** Cursor free and clickable. Chat and aim set their own mode. */
	static bool FreesMouse(EGFHudModal Modal);
	/**
	 * Quest tracker, spotted banner, interact prompt, and the rank badge.
	 * Off while pause or the elder menu is up, including when chat is open over one of them.
	 */
	static bool ShowsFieldHud(EGFHudModal Modal, EGFHudModal Restore);
	/**
	 * The hub key confirms a return only while the confirm modal is up.
	 * Escape leaves that modal, so the next press asks again.
	 */
	static bool ConfirmsReturn(EGFHudModal Modal);
};

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
	bool IsAimSettingsOpen() const { return Modal == EGFHudModal::Aim; }

	EGFHudModal GetHudModal() const { return Modal; }
	EGFHudModal GetHudRestore() const { return Restore; }
	/**
	 * The only writer of the modal. Open, close, Escape, and teardown all come through here.
	 * A refused event leaves the modal as it was.
	 */
	void SetHudModal(EGFHudModalEvent Event);
	/** Local toast. The server sends the text. Empty text is ignored. */
	void PushToast(const FString& Text);

	/** How the owning controller's input is set while the pause menu is open or shut. */
	struct FPauseInput
	{
		/** Game and UI with the cursor free, so Escape still reaches the game mapping and the mouse can click entries. */
		bool bGameAndUI = false;
		bool bShowCursor = false;
		bool bClickEvents = false;
		/** False holds the pawn's input (look, move, mine, jump) while the menu is up. */
		bool bPawnInput = true;
	};
	static FPauseInput PauseInputFor(bool bMenuOpen);

	bool IsPauseMenuOpen() const { return FGFHudModalRules::IsPause(Modal); }
	void OpenPauseMenu();
	void ClosePauseMenu();
	void TogglePauseMenu();
	/** Escape: close chat, aim, confirm, or the elder menu. From the game it opens pause. */
	void HandleEscape();
	/** Server-validated interact. Refused while aim is open so the menus do not stack. */
	void OpenElderMenu(AGFElder* Elder, FName TownId);
	EGFElderTab GetElderTab() const { return ElderTab; }
	void SetElderTab(EGFElderTab Tab);
	/** 0 Return to hub, 1 Settings, 2 Close, 3 Controls / How to get items. While a confirm is up: 0 Confirm, 1 Cancel. */
	void ActivatePauseEntry(int32 Index);
	/** Shows the runway item hint again (the Esc entry). */
	void ShowItemHowTo();
	static const TCHAR* PauseEntryLabel(int32 Index);
	FString GetPauseEntry(int32 Index) const;
	void ToggleAimSettings();
	void CloseAimSettings();
	/** Clicks on the pause entries (hit boxes GFPause_<index>). */
	virtual void NotifyHitBoxClick(FName BoxName) override;

	/** Clicks and drops in the bag (hit boxes GFInv_*). The press is NotifyHitBoxClick. */
	virtual void NotifyHitBoxRelease(FName BoxName) override;

	bool IsInventoryOpen() const { return Modal == EGFHudModal::Inventory; }
	/** Tab or I. */
	void ToggleInventory();
	void OpenInventory();
	void CloseInventory();
	EGFInventoryTab GetInventoryTab() const { return InvTab; }
	void SetInventoryTab(EGFInventoryTab Tab);
	int32 GetInventoryPage() const { return InvPage; }
	/** Everything the bag lists, all tabs (FGFInventoryView::Build on the owning player state). */
	TArray<FGFInventoryEntry> GetInventoryEntries() const;
	/** The current tab and page, in cell order. Cell hit box GFInv_Cell_<i> is entry i. Built each draw. */
	const TArray<FGFInventoryEntry>& GetInventoryPageEntries() const { return InvPageEntries; }
	const FGFInventoryCursor& GetInventoryCursor() const { return InvCursor; }
	/** Number key while the bag is open: the held or hovered item goes to that slot. */
	void InventoryNumberKey(int32 Slot);
	/** Last hotbar move this HUD sent (AGFPlayerState::HotbarResults key), or INDEX_NONE. */
	int32 GetLastHotbarRequest() const { return LastHotbarRequest; }

	bool IsChatOpen() const { return Modal == EGFHudModal::Chat; }
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
	void DrawFieldHud();
	void DrawElderMenu();
	void ActivateElderBox(FName BoxName);
	void DrawInventoryScreen();
	/** Icon, or a swatch with a short label, inside a tile. Shared by the hotbar and the bag. */
	void DrawSlotArt(FName Id, float X, float Y, float W, float H, bool bSpent);
	/** Hit box name to what it points at. Unknown names are Nothing. */
	FGFInvTarget InventoryTargetFor(FName BoxName) const;
	void SubmitHotbarMove(const TOptional<FGFHotbarMove>& Move);
	/** Rebuilds InvPageEntries for the current tab and page (clamping the page). */
	void RefreshInventoryPage(const FGFInventoryLayout& Layout);
	/**
	 * Input mode, cursor and pawn input for whichever modal is up (PauseInputFor for pause, confirm, and the elder menu).
	 * Chat and the aim panel set their own UI-only mode while open; when they close this puts back the menu or the game.
	 */
	void ApplyMenuInput();
	void BuildChatWidget();
	void FocusChat();
	void BuildAimWidget();
	void FocusAim();

	void PollToasts();
	void NoteChatLine(int32 Seq, double Now);
	void DismissChat();
	void RemoveChatWidget();
	void SendChat(const FString& Line);

	UFont* HudFont() const;
	/** Item row icon, else /Game/Art/Items/Textures/T_Icon_<Id>, else null. Cached. */
	UTexture2D* SlotIcon(FName SlotId);
	/** Item display name, else the id spaced out. "Hands" for Fists, "Empty" for None. */
	FString SlotDisplayName(FName SlotId) const;
	/** Block DebugColor, else a stable colour from the id. */
	FLinearColor SlotSwatch(FName SlotId) const;

	/** Slate entry. Incomplete here so this header does not include widget headers. */
	FGHUDChat* Chat = nullptr;
	/** Survives the close frame so a second Enter in that frame does not open chat again. */
	int32 ChatBlockTicks = 0;
	EGFHudModal Modal = EGFHudModal::Game;
	EGFHudModal Restore = EGFHudModal::Game;

	double ShownAt = 0.0;
	bool bShownAtSet = false;
	bool bHintUserOverride = false;
	bool bHintUserVisible = true;

	/** Hotbar and inventory icons per slot id. Null is a cached miss (the slot draws a colour swatch). */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTexture2D>> IconCache;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BeatJudgementTex;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BeatSparkleTex;
	bool bBeatTexLoaded = false;

	TMap<int32, double> ChatSeenAt;
	TArray<FGFHUDToast> Toasts;
	TSharedPtr<SWidget> AimSettingsRoot;
	/** The pawn whose input the open menu holds. */
	TWeakObjectPtr<APawn> HeldPawn;
	TWeakObjectPtr<AGFElder> ElderActor;
	FName ElderTownId;
	/** Talk, Trade, Elder Book. Reset to Talk each time the menu opens. */
	EGFElderTab ElderTab = EGFElderTab::Talk;
	/** Item hint on the runway. On until a show ends, and again when the pause menu asks. */
	bool bShowItemHowTo = true;
	bool bItemHowToPinned = false;
	int32 AppliedNotices = 0;
	int32 AppliedResults = 0;

	/** Bag state. Local only; the bar itself lives on the player state. */
	FGFInventoryCursor InvCursor;
	EGFInventoryTab InvTab = EGFInventoryTab::Blocks;
	int32 InvPage = 0;
	int32 InvPageCount = 1;
	TArray<FGFInventoryEntry> InvPageEntries;
	/** What the mouse is over this frame (tooltips and number keys). */
	FGFInvTarget InvHover;
	int32 LastHotbarRequest = INDEX_NONE;
	bool bLastHotbarReported = true;
};
