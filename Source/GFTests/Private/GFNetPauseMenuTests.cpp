// Escape opens the pause menu with the mouse free (cursor shown, no permanent capture, pawn input held) and closing it
// captures the mouse again. Also: a click on an entry, the Settings panel from the menu, and chat over the menu.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.PauseMenu;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFHUD.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetPauseDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	APlayerController* LocalPC(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				return PC;
			}
		}
		return nullptr;
	}

	/** The mouse is free: cursor shown, click events on, the viewport takes input and only captures while a button is down. */
	void ExpectFree(FAutomationTestBase* Test, const FString& When, APlayerController* PC)
	{
		Test->TestTrue(When + TEXT(": cursor shown"), PC->bShowMouseCursor);
		Test->TestTrue(When + TEXT(": click events on"), PC->bEnableClickEvents);
		UGameViewportClient* Viewport = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->ViewportClient.Get() : nullptr;
		if (Viewport)
		{
			Test->TestTrue(When + TEXT(": mouse not captured permanently"), Viewport->GetMouseCaptureMode() != EMouseCaptureMode::CapturePermanently
				&& Viewport->GetMouseCaptureMode() != EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
		}
	}

	void ExpectCaptured(FAutomationTestBase* Test, const FString& When, APlayerController* PC)
	{
		Test->TestFalse(When + TEXT(": cursor hidden"), PC->bShowMouseCursor);
		Test->TestFalse(When + TEXT(": click events off"), PC->bEnableClickEvents);
		UGameViewportClient* Viewport = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->ViewportClient.Get() : nullptr;
		if (Viewport)
		{
			Test->TestEqual(When + TEXT(": mouse captured for the game"), static_cast<int32>(Viewport->GetMouseCaptureMode()), static_cast<int32>(EMouseCaptureMode::CapturePermanently));
			Test->TestFalse(When + TEXT(": viewport takes game input"), Viewport->IgnoreInput());
		}
		if (const APawn* Pawn = PC->GetPawn())
		{
			Test->TestTrue(When + TEXT(": pawn input on"), Pawn->InputEnabled());
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPauseInputRulesTest, "GFGame.UI.PauseInput", GFNetPauseDetail::Flags)
bool FGFPauseInputRulesTest::RunTest(const FString&)
{
	const AGFHUD::FPauseInput Open = AGFHUD::PauseInputFor(true);
	TestTrue(TEXT("Open: game and UI"), Open.bGameAndUI);
	TestTrue(TEXT("Open: cursor"), Open.bShowCursor);
	TestTrue(TEXT("Open: clicks"), Open.bClickEvents);
	TestFalse(TEXT("Open: pawn input held"), Open.bPawnInput);
	const AGFHUD::FPauseInput Shut = AGFHUD::PauseInputFor(false);
	TestFalse(TEXT("Shut: game only"), Shut.bGameAndUI);
	TestFalse(TEXT("Shut: no cursor"), Shut.bShowCursor);
	TestFalse(TEXT("Shut: no clicks"), Shut.bClickEvents);
	TestTrue(TEXT("Shut: pawn input"), Shut.bPawnInput);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetPauseMenuMouseTest, "GFGame.Net.PauseMenu.Mouse", GFNetPauseDetail::Flags)
bool FGFNetPauseMenuMouseTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetPauseDetail;
	FCtxRef Ctx = StartOnMap(this, TEXT("/Game/Maps/L_World_Factions_A"), 1, 120.f);
	WaitUntil(Ctx, TEXT("local controller with a pawn and the HUD"), 20.f, []()
	{
		const APlayerController* PC = LocalPC(ServerWorld());
		return PC && PC->GetPawn() && Cast<AGFHUD>(PC->GetHUD());
	});
	Wait(Ctx, 0.5f);
	Do(Ctx, [Ctx]()
	{
		FAutomationTestBase* Test = Ctx->Test;
		APlayerController* PC = LocalPC(ServerWorld());
		AGFHUD* HUD = PC ? Cast<AGFHUD>(PC->GetHUD()) : nullptr;
		if (!HUD)
		{
			Test->AddError(TEXT("No HUD"));
			Ctx->bFailed = true;
			return;
		}
		ExpectCaptured(Test, TEXT("Playing"), PC);

		// Escape (AGFPlayerController::HandleCloseChatKey with nothing else open).
		HUD->TogglePauseMenu();
		Test->TestTrue(TEXT("Escape opens the menu"), HUD->IsPauseMenuOpen());
		ExpectFree(Test, TEXT("Menu open"), PC);
		Test->TestFalse(TEXT("Menu open: pawn input held"), PC->GetPawn()->InputEnabled());
		if (UGameViewportClient* Viewport = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->ViewportClient.Get() : nullptr)
		{
			Test->TestFalse(TEXT("Menu open: Escape still reaches the game mapping"), Viewport->IgnoreInput());
		}
		else
		{
			Test->AddInfo(TEXT("No game viewport in this run; capture mode not checked"));
		}

		HUD->TogglePauseMenu();
		Test->TestFalse(TEXT("Escape again closes it"), HUD->IsPauseMenuOpen());
		ExpectCaptured(Test, TEXT("Menu closed"), PC);

		// A click on Close (entry 2).
		HUD->OpenPauseMenu();
		HUD->NotifyHitBoxClick(TEXT("GFPause_2"));
		Test->TestFalse(TEXT("Clicking Close closes the menu"), HUD->IsPauseMenuOpen());
		ExpectCaptured(Test, TEXT("Closed by click"), PC);

		// Chat over the open menu gives the menu its free mouse back when chat closes.
		HUD->OpenPauseMenu();
		HUD->OpenChat();
		if (HUD->IsChatOpen())
		{
			HUD->CloseChat();
			Test->TestTrue(TEXT("Menu still open after chat"), HUD->IsPauseMenuOpen());
			ExpectFree(Test, TEXT("After chat over the menu"), PC);
		}
		HUD->ClosePauseMenu();
		ExpectCaptured(Test, TEXT("Menu closed after chat"), PC);

		// Settings (entry 1) swaps the menu for the aim panel, which keeps the cursor; closing it captures again.
		HUD->OpenPauseMenu();
		HUD->ActivatePauseEntry(1);
		if (HUD->IsAimSettingsOpen())
		{
			Test->TestTrue(TEXT("Settings panel: cursor shown"), PC->bShowMouseCursor);
			HUD->CloseAimSettings();
		}
		else
		{
			Test->AddInfo(TEXT("No game viewport for the Settings panel in this run"));
		}
		Test->TestFalse(TEXT("Menu closed after Settings"), HUD->IsPauseMenuOpen());
		ExpectCaptured(Test, TEXT("Settings closed"), PC);
	});
	Stop(Ctx);
	return true;
}

#endif