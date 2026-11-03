// Elder menu input and the rule that it never sits on top of pause.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.UI.ElderMenu;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFHUD.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ElderMenuFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderMenuInputTest, "GFGame.UI.ElderMenu.Input", ElderMenuFlags)
bool FGFElderMenuInputTest::RunTest(const FString&)
{
	const FGFHudModalRules::FState Open = FGFHudModalRules::Transition(EGFHudModal::Game, EGFHudModal::Game, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Interact opens the elder menu"), Open.Modal == EGFHudModal::Elder);
	TestTrue(TEXT("Elder frees the mouse"), FGFHudModalRules::FreesMouse(Open.Modal));
	TestTrue(TEXT("Elder holds the pawn"), FGFHudModalRules::HoldsPawn(Open.Modal, Open.Restore));
	const AGFHUD::FPauseInput Free = AGFHUD::PauseInputFor(true);
	TestTrue(TEXT("Same free mouse as pause"), Free.bGameAndUI && Free.bShowCursor && Free.bClickEvents && !Free.bPawnInput);
	TestTrue(TEXT("Elder uses that table"), FGFHudModalRules::FreesMouse(Open.Modal) == Free.bShowCursor);

	const FGFHudModalRules::FState Shut = FGFHudModalRules::Transition(Open.Modal, Open.Restore, EGFHudModalEvent::Escape);
	TestTrue(TEXT("Escape closes it"), Shut.Modal == EGFHudModal::Game);
	TestFalse(TEXT("Game captures the mouse"), FGFHudModalRules::FreesMouse(Shut.Modal));
	TestFalse(TEXT("Game returns the pawn"), FGFHudModalRules::HoldsPawn(Shut.Modal, Shut.Restore));
	const AGFHUD::FPauseInput Held = AGFHUD::PauseInputFor(false);
	TestTrue(TEXT("Shut table"), !Held.bGameAndUI && !Held.bShowCursor && !Held.bClickEvents && Held.bPawnInput);

	const FGFHudModalRules::FState ByClose = FGFHudModalRules::Transition(EGFHudModal::Elder, EGFHudModal::Game, EGFHudModalEvent::CloseElder);
	TestTrue(TEXT("Close entry"), ByClose.Modal == EGFHudModal::Game);

	FGFHudModalRules::FState Chat = FGFHudModalRules::Transition(EGFHudModal::Game, EGFHudModal::Game, EGFHudModalEvent::OpenPause);
	Chat = FGFHudModalRules::Transition(Chat.Modal, Chat.Restore, EGFHudModalEvent::OpenChat);
	TestTrue(TEXT("Chat over pause"), Chat.Modal == EGFHudModal::Chat && Chat.Restore == EGFHudModal::Pause);
	TestTrue(TEXT("Pawn stays held under chat"), FGFHudModalRules::HoldsPawn(Chat.Modal, Chat.Restore));
	Chat = FGFHudModalRules::Transition(Chat.Modal, Chat.Restore, EGFHudModalEvent::CloseChat);
	TestTrue(TEXT("Chat returns to pause"), Chat.Modal == EGFHudModal::Pause);

	const FGFHudModalRules::FState Confirm = FGFHudModalRules::Transition(EGFHudModal::Pause, EGFHudModal::Game, EGFHudModalEvent::OpenConfirm);
	TestTrue(TEXT("Confirm is its own modal"), Confirm.Modal == EGFHudModal::PauseConfirm);
	const FGFHudModalRules::FState Escaped = FGFHudModalRules::Transition(Confirm.Modal, Confirm.Restore, EGFHudModalEvent::Escape);
	TestTrue(TEXT("Escape cancels the confirm"), Escaped.Modal == EGFHudModal::Game);

	const FGFHudModalRules::FState HomeOnElder = FGFHudModalRules::Transition(EGFHudModal::Elder, EGFHudModal::Game, EGFHudModalEvent::OpenConfirm);
	TestTrue(TEXT("Home replaces the elder menu with the confirm"), HomeOnElder.Modal == EGFHudModal::PauseConfirm);
	TestFalse(TEXT("Elder is not also open"), HomeOnElder.Modal == EGFHudModal::Elder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderMenuNoStackTest, "GFGame.UI.ElderMenu.NoStack", ElderMenuFlags)
bool FGFElderMenuNoStackTest::RunTest(const FString&)
{
	const FGFHudModalRules::FState FromPause = FGFHudModalRules::Transition(EGFHudModal::Pause, EGFHudModal::Game, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Elder replaces pause"), FromPause.Modal == EGFHudModal::Elder);
	TestFalse(TEXT("Pause is not also open"), FGFHudModalRules::IsPause(FromPause.Modal));

	const FGFHudModalRules::FState PauseOnElder = FGFHudModalRules::Transition(EGFHudModal::Elder, EGFHudModal::Game, EGFHudModalEvent::OpenPause);
	TestTrue(TEXT("Pause does not cover the elder"), PauseOnElder.Modal == EGFHudModal::Elder);
	const FGFHudModalRules::FState ToggleOnElder = FGFHudModalRules::Transition(EGFHudModal::Elder, EGFHudModal::Game, EGFHudModalEvent::TogglePause);
	TestTrue(TEXT("Escape's pause toggle does not cover the elder"), ToggleOnElder.Modal == EGFHudModal::Elder);

	const FGFHudModalRules::FState FromConfirm = FGFHudModalRules::Transition(EGFHudModal::PauseConfirm, EGFHudModal::Game, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Elder replaces the confirm"), FromConfirm.Modal == EGFHudModal::Elder);
	TestFalse(TEXT("Confirm is not also open"), FromConfirm.Modal == EGFHudModal::PauseConfirm);

	FGFHudModalRules::FState UnderChat = FGFHudModalRules::Transition(EGFHudModal::Pause, EGFHudModal::Game, EGFHudModalEvent::OpenChat);
	UnderChat = FGFHudModalRules::Transition(UnderChat.Modal, UnderChat.Restore, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Chat stays the only modal"), UnderChat.Modal == EGFHudModal::Chat && UnderChat.Restore == EGFHudModal::Elder);
	UnderChat = FGFHudModalRules::Transition(UnderChat.Modal, UnderChat.Restore, EGFHudModalEvent::CloseChat);
	TestTrue(TEXT("Chat closes onto the elder, not pause"), UnderChat.Modal == EGFHudModal::Elder);
	TestFalse(TEXT("Still one modal"), FGFHudModalRules::IsPause(UnderChat.Modal));

	const FGFHudModalRules::FState AimBlocks = FGFHudModalRules::Transition(EGFHudModal::Aim, EGFHudModal::Game, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Aim is left as it was"), AimBlocks.Modal == EGFHudModal::Aim);
	return true;
}

#endif
