// Elder menu input, the rule that it never sits on top of (or replaces) another menu, and when the server session closes.
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
	// #304 pass 2: the open is a server reply. It never replaces whatever the player opened meanwhile.
	const FGFHudModalRules::FState FromPause = FGFHudModalRules::Transition(EGFHudModal::Pause, EGFHudModal::Game, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Elder does not replace pause"), FromPause.Modal == EGFHudModal::Pause);
	TestFalse(TEXT("Refused open has no elder"), FGFHudModalRules::HasElder(FromPause.Modal, FromPause.Restore));

	const FGFHudModalRules::FState PauseOnElder = FGFHudModalRules::Transition(EGFHudModal::Elder, EGFHudModal::Game, EGFHudModalEvent::OpenPause);
	TestTrue(TEXT("Pause does not cover the elder"), PauseOnElder.Modal == EGFHudModal::Elder);
	const FGFHudModalRules::FState ToggleOnElder = FGFHudModalRules::Transition(EGFHudModal::Elder, EGFHudModal::Game, EGFHudModalEvent::TogglePause);
	TestTrue(TEXT("Escape's pause toggle does not cover the elder"), ToggleOnElder.Modal == EGFHudModal::Elder);

	const FGFHudModalRules::FState FromConfirm = FGFHudModalRules::Transition(EGFHudModal::PauseConfirm, EGFHudModal::Game, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Elder does not replace the confirm"), FromConfirm.Modal == EGFHudModal::PauseConfirm);

	const FGFHudModalRules::FState FromBag = FGFHudModalRules::Transition(EGFHudModal::Inventory, EGFHudModal::Game, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Elder does not replace the bag"), FromBag.Modal == EGFHudModal::Inventory);

	FGFHudModalRules::FState UnderChat = FGFHudModalRules::Transition(EGFHudModal::Pause, EGFHudModal::Game, EGFHudModalEvent::OpenChat);
	UnderChat = FGFHudModalRules::Transition(UnderChat.Modal, UnderChat.Restore, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Pause under chat is kept"), UnderChat.Modal == EGFHudModal::Chat && UnderChat.Restore == EGFHudModal::Pause);
	UnderChat = FGFHudModalRules::Transition(UnderChat.Modal, UnderChat.Restore, EGFHudModalEvent::CloseChat);
	TestTrue(TEXT("Chat closes onto pause"), UnderChat.Modal == EGFHudModal::Pause);

	FGFHudModalRules::FState ChatOverGame = FGFHudModalRules::Transition(EGFHudModal::Game, EGFHudModal::Game, EGFHudModalEvent::OpenChat);
	ChatOverGame = FGFHudModalRules::Transition(ChatOverGame.Modal, ChatOverGame.Restore, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Chat over the game queues the elder under it"), ChatOverGame.Modal == EGFHudModal::Chat && ChatOverGame.Restore == EGFHudModal::Elder);
	TestTrue(TEXT("That counts as the elder being up"), FGFHudModalRules::HasElder(ChatOverGame.Modal, ChatOverGame.Restore));

	const FGFHudModalRules::FState AimBlocks = FGFHudModalRules::Transition(EGFHudModal::Aim, EGFHudModal::Game, EGFHudModalEvent::OpenElder);
	TestTrue(TEXT("Aim is left as it was"), AimBlocks.Modal == EGFHudModal::Aim);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderMenuSessionCloseTest, "GFGame.UI.ElderMenu.SessionClose", ElderMenuFlags)
bool FGFElderMenuSessionCloseTest::RunTest(const FString&)
{
	using FState = FGFHudModalRules::FState;
	auto Step = [](const FState& S, EGFHudModalEvent E) { return FGFHudModalRules::Transition(S.Modal, S.Restore, E); };
	const FState Elder{ EGFHudModal::Elder, EGFHudModal::Game };
	const FState ChatOverElder = Step(Elder, EGFHudModalEvent::OpenChat);
	TestTrue(TEXT("chat over the elder"), ChatOverElder.Modal == EGFHudModal::Chat && ChatOverElder.Restore == EGFHudModal::Elder);
	TestFalse(TEXT("opening chat keeps the session"), FGFHudModalRules::EndsElderSession(Elder, ChatOverElder));
	TestFalse(TEXT("closing chat back onto the elder keeps it"), FGFHudModalRules::EndsElderSession(ChatOverElder, Step(ChatOverElder, EGFHudModalEvent::CloseChat)));

	// #304 pass 2: these left the elder from under chat and never told the server.
	const FState ClosedUnder = Step(ChatOverElder, EGFHudModalEvent::CloseElder);
	TestTrue(TEXT("close under chat leaves chat over the game"), ClosedUnder.Modal == EGFHudModal::Chat && ClosedUnder.Restore == EGFHudModal::Game);
	TestTrue(TEXT("close under chat ends the session"), FGFHudModalRules::EndsElderSession(ChatOverElder, ClosedUnder));
	TestTrue(TEXT("teardown under chat ends the session"), FGFHudModalRules::EndsElderSession(ChatOverElder, Step(ChatOverElder, EGFHudModalEvent::Reset)));

	TestTrue(TEXT("escape from the elder ends it"), FGFHudModalRules::EndsElderSession(Elder, Step(Elder, EGFHudModalEvent::Escape)));
	TestTrue(TEXT("the hub prompt over the elder ends it"), FGFHudModalRules::EndsElderSession(Elder, Step(Elder, EGFHudModalEvent::OpenConfirm)));
	const FState Game{ EGFHudModal::Game, EGFHudModal::Game };
	TestFalse(TEXT("nothing to end without an elder"), FGFHudModalRules::EndsElderSession(Game, Step(Game, EGFHudModalEvent::OpenPause)));
	return true;
}

#endif
