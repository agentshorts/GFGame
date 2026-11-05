// Quest tracker, spotted banner, interact prompt, rank badge, and the hub-key confirm.
// Pure rules. No canvas and no world.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.HUD;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFHUD.h"
#include "GFHUDFormat.h"
#include "GFQuestLine.h"
#include "GFSideQuestLine.h"
#include "GFTheftRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags FieldHudFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool FieldOverlap(const FGFHudRect& A, const FGFHudRect& B)
	{
		return GFHudRectsOverlap(A, B);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuestTrackerFormatTest, "GFGame.HUD.QuestTracker.Format", FieldHudFlags)
bool FGFQuestTrackerFormatTest::RunTest(const FString&)
{
	const FGFQuestCatalog& Catalog = FGFQuestCatalog::Builtin();

	TestEqual(TEXT("Take crystal"), GFQuestStepTitle(EGFQuestStep::TakeCrystal), FString(TEXT("Take crystal")));
	TestEqual(TEXT("Get clear"), GFQuestStepTitle(EGFQuestStep::Escape), FString(TEXT("Get clear")));
	TestEqual(TEXT("Talk to an elder"), GFQuestStepTitle(EGFQuestStep::TalkToElder), FString(TEXT("Talk to an elder")));
	TestEqual(TEXT("Hand in hide"), GFQuestStepTitle(EGFQuestStep::HuntHide), FString(TEXT("Hand in hide")));
	TestEqual(TEXT("Hand in skins"), GFQuestStepTitle(EGFQuestStep::HuntSkins), FString(TEXT("Hand in skins")));
	TestEqual(TEXT("Craft a look"), GFQuestStepTitle(EGFQuestStep::CraftLook), FString(TEXT("Craft a look")));
	TestEqual(TEXT("Win the show"), GFQuestStepTitle(EGFQuestStep::WinShow), FString(TEXT("Win the show")));
	TestTrue(TEXT("Done has no step title"), GFQuestStepTitle(EGFQuestStep::Done).IsEmpty());

	TestEqual(TEXT("Outsider"), GFFormatRankBadge(EGFFactionRank::Outsider), FString(TEXT("Outsider")));
	TestEqual(TEXT("Initiate"), GFFormatRankBadge(EGFFactionRank::Initiate), FString(TEXT("Initiate")));
	TestEqual(TEXT("Member"), GFFormatRankBadge(EGFFactionRank::Member), FString(TEXT("Member")));
	TestEqual(TEXT("Champion"), GFFormatRankBadge(EGFFactionRank::Champion), FString(TEXT("Champion")));
	TestEqual(TEXT("Leader"), GFFormatRankBadge(EGFFactionRank::Leader), FString(TEXT("Leader")));
	TestEqual(TEXT("Nameplate carries the rank"), GFFormatNameplateWithRank(TEXT("Miner"), EGFFactionRank::Member), FString(TEXT("Miner \u00B7 Member")));

	const FGFQuestTrackerView Take = GFFormatQuestTracker(Catalog, EGFQuestStep::TakeCrystal, 0, EGFFactionRank::Outsider, true);
	TestTrue(TEXT("Tracker shows from the first step"), Take.bVisible);
	TestEqual(TEXT("Line title"), Take.LineTitle, FString(TEXT("The Elder's Errand")));
	TestEqual(TEXT("Step title"), Take.StepTitle, FString(TEXT("Take crystal")));
	TestEqual(TEXT("Progress"), Take.Progress, FString(TEXT("0/1")));
	TestEqual(TEXT("Hint from the catalog"), Take.Hint, Catalog.HintFor(EGFQuestStep::TakeCrystal));
	TestEqual(TEXT("Rank in the tracker"), Take.Rank, FString(TEXT("Outsider")));
	TestTrue(TEXT("Lines lead with the rank"), Take.Lines.Num() > 0 && Take.Lines[0] == Take.Rank);
	TestTrue(TEXT("Lines carry the hint"), Take.Lines.Contains(Take.Hint));

	const FGFQuestTrackerView Hide = GFFormatQuestTracker(Catalog, EGFQuestStep::HuntHide, 3, EGFFactionRank::Initiate, true);
	TestEqual(TEXT("Partial progress"), Hide.Progress, FString(TEXT("3/6")));
	TestEqual(TEXT("Hide hint"), Hide.Hint, FString(TEXT("Hand the elder 6 Hide or Scales, in any mix.")));
	TestEqual(TEXT("Rank follows the player"), Hide.Rank, FString(TEXT("Initiate")));

	const FGFQuestTrackerView Negative = GFFormatQuestTracker(Catalog, EGFQuestStep::TakeCrystal, -4, EGFFactionRank::Outsider, true);
	TestEqual(TEXT("Negative count clamps"), Negative.Progress, FString(TEXT("0/1")));

	const FGFQuestTrackerView Done = GFFormatQuestTracker(Catalog, EGFQuestStep::Done, 0, EGFFactionRank::Leader, true);
	TestFalse(TEXT("Done collapses the quest body"), Done.bVisible);
	TestTrue(TEXT("Done drops the step and the hint"), Done.StepTitle.IsEmpty() && Done.Hint.IsEmpty());
	if (TestEqual(TEXT("Rank badge remains"), Done.Lines.Num(), 1))
	{
		TestEqual(TEXT("Leader stays on the badge"), Done.Lines[0], FString(TEXT("Leader")));
	}

	TestFalse(TEXT("Pause hides the tracker"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Pause, EGFHudModal::Game));
	TestFalse(TEXT("Confirm hides the tracker"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::PauseConfirm, EGFHudModal::Game));
	TestFalse(TEXT("Elder menu hides the tracker"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Elder, EGFHudModal::Game));
	TestFalse(TEXT("Chat over pause still hides it"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Chat, EGFHudModal::Pause));
	TestFalse(TEXT("Chat over the elder still hides it"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Chat, EGFHudModal::Elder));
	TestTrue(TEXT("Game shows it"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Game, EGFHudModal::Game));
	TestTrue(TEXT("Chat over the game shows it"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Chat, EGFHudModal::Game));
	TestTrue(TEXT("Aim leaves it up"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Aim, EGFHudModal::Game));

	const FGFQuestTrackerView Blocked = GFFormatQuestTracker(Catalog, EGFQuestStep::TakeCrystal, 0, EGFFactionRank::Member, false);
	TestFalse(TEXT("A closed field HUD drops the body"), Blocked.bVisible);
	TestEqual(TEXT("A closed field HUD drops the lines"), Blocked.Lines.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestTrackerFormatTest, "GFGame.HUD.SideQuestTracker.Format", FieldHudFlags)
bool FGFSideQuestTrackerFormatTest::RunTest(const FString&)
{
	const FGFQuestCatalog& Errand = FGFQuestCatalog::Builtin();
	const FGFSideQuestCatalog& SideRows = FGFSideQuestCatalog::Builtin();
	const FGFSideQuestLineRule* Line = nullptr;
	const FGFSideQuestStepRule* Counted = nullptr;
	for (const FGFSideQuestLineRule& Candidate : SideRows.Lines)
	{
		for (const FGFSideQuestStepRule& StepRule : Candidate.Steps)
		{
			if (!Counted && Candidate.IsPlayable() && StepRule.Goal > 1)
			{
				Line = &Candidate;
				Counted = &StepRule;
			}
		}
	}
	if (!TestNotNull(TEXT("A shipped side line has a counted step"), Counted))
	{
		return false;
	}

	FGFSideTrackerInput Active;
	Active.Status = EGFSideQuestStatus::Active;
	Active.LineId = Line->LineId;
	Active.StepId = Counted->StepId;
	Active.Count = 1;

	const FGFQuestTrackerView Mid = GFFormatSideQuestTracker(Errand, EGFQuestStep::HuntHide, 3, EGFFactionRank::Initiate, SideRows, Active, true);
	const FGFQuestTrackerView ErrandOnly = GFFormatQuestTracker(Errand, EGFQuestStep::HuntHide, 3, EGFFactionRank::Initiate, true);
	TestTrue(TEXT("Errand mid-line wins over an Active side line"), Mid.Lines == ErrandOnly.Lines);
	TestEqual(TEXT("Errand title stays"), Mid.LineTitle, FString(TEXT("The Elder's Errand")));
	TestEqual(TEXT("Errand progress stays"), Mid.Progress, FString(TEXT("3/6")));

	const FGFQuestTrackerView Side = GFFormatSideQuestTracker(Errand, EGFQuestStep::Done, 0, EGFFactionRank::Member, SideRows, Active, true);
	TestTrue(TEXT("Side body shows after the errand"), Side.bVisible);
	TestEqual(TEXT("Side line title"), Side.LineTitle, Line->Title);
	TestEqual(TEXT("Side step title"), Side.StepTitle, Counted->Title);
	TestEqual(TEXT("Side progress"), Side.Progress, FString::Printf(TEXT("1/%d"), Counted->Goal));
	TestEqual(TEXT("Side hint"), Side.Hint, Counted->Hint);
	TestEqual(TEXT("Rank badge stays the errand rank"), Side.Rank, FString(TEXT("Member")));
	TestTrue(TEXT("Side lines lead with the rank"), Side.Lines.Num() > 0 && Side.Lines[0] == Side.Rank);
	TestTrue(TEXT("Side lines carry the title"), Side.Lines.Contains(Line->Title));
	TestTrue(TEXT("Side lines fit the tracker"), Side.Lines.Num() <= 5);

	FGFSideTrackerInput Over = Active;
	Over.Count = Counted->Goal + 4;
	TestEqual(TEXT("Side progress clamps at the goal"), GFFormatSideQuestTracker(Errand, EGFQuestStep::Done, 0, EGFFactionRank::Member, SideRows, Over, true).Progress,
		FString::Printf(TEXT("%d/%d"), Counted->Goal, Counted->Goal));

	const FGFSideQuestStepRule* Single = nullptr;
	for (const FGFSideQuestStepRule& StepRule : Line->Steps)
	{
		if (!Single && StepRule.Goal <= 1)
		{
			Single = &StepRule;
		}
	}
	if (Single)
	{
		FGFSideTrackerInput One = Active;
		One.StepId = Single->StepId;
		One.Count = 0;
		TestTrue(TEXT("A one-shot step has no n/m"), GFFormatSideQuestTracker(Errand, EGFQuestStep::Done, 0, EGFFactionRank::Member, SideRows, One, true).Progress.IsEmpty());
	}

	const FGFQuestTrackerView DoneNone = GFFormatSideQuestTracker(Errand, EGFQuestStep::Done, 0, EGFFactionRank::Leader, SideRows, FGFSideTrackerInput(), true);
	TestFalse(TEXT("Done with no side line collapses"), DoneNone.bVisible);
	TestEqual(TEXT("Collapse keeps the rank badge"), DoneNone.Lines.Num(), 1);
	TestEqual(TEXT("Collapse rank"), DoneNone.Rank, FString(TEXT("Leader")));

	FGFSideTrackerInput Offered = Active;
	Offered.Status = EGFSideQuestStatus::Available;
	TestEqual(TEXT("Available is not tracked"), GFFormatSideQuestTracker(Errand, EGFQuestStep::Done, 0, EGFFactionRank::Member, SideRows, Offered, true).Lines.Num(), 1);
	FGFSideTrackerInput Finished = Active;
	Finished.Status = EGFSideQuestStatus::Done;
	TestEqual(TEXT("A finished side line is not tracked"), GFFormatSideQuestTracker(Errand, EGFQuestStep::Done, 0, EGFFactionRank::Member, SideRows, Finished, true).Lines.Num(), 1);
	FGFSideTrackerInput Unknown = Active;
	Unknown.LineId = TEXT("NoSuchLine");
	TestEqual(TEXT("An unknown line collapses"), GFFormatSideQuestTracker(Errand, EGFQuestStep::Done, 0, EGFFactionRank::Member, SideRows, Unknown, true).Lines.Num(), 1);

	const FGFQuestTrackerView Hidden = GFFormatSideQuestTracker(Errand, EGFQuestStep::Done, 0, EGFFactionRank::Member, SideRows, Active, false);
	TestFalse(TEXT("bAllow false hides the side body"), Hidden.bVisible);
	TestEqual(TEXT("bAllow false hides the rank too"), Hidden.Lines.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSpottedStatesTest, "GFGame.HUD.Spotted.States", FieldHudFlags)
bool FGFSpottedStatesTest::RunTest(const FString&)
{
	FGFTheftHeat Heat;
	const FGFSpottedAlert Clear = GFFormatSpottedAlert(Heat.State, Heat.HuntLeft, true);
	TestFalse(TEXT("Clear hides the banner"), Clear.bVisible);
	TestTrue(TEXT("Clear has no copy"), Clear.Headline.IsEmpty() && Clear.Detail.IsEmpty());

	TestTrue(TEXT("A witness spots the thief"), FGFTheftRules::NoteWitness(Heat, TEXT("Rime")));
	const FGFSpottedAlert Spotted = GFFormatSpottedAlert(Heat.State, Heat.HuntLeft, true);
	TestTrue(TEXT("Spotted shows"), Spotted.bVisible);
	TestEqual(TEXT("Spotted copy"), Spotted.Headline, FString(TEXT("Spotted! A villager saw you take crystal")));
	TestTrue(TEXT("Spotted has no countdown"), Spotted.Detail.IsEmpty());

	TestTrue(TEXT("The elder hires"), FGFTheftRules::NoteHire(Heat));
	const FGFSpottedAlert Hired = GFFormatSpottedAlert(Heat.State, Heat.HuntLeft, true);
	TestTrue(TEXT("Hire shows"), Hired.bVisible);
	TestEqual(TEXT("Hire copy"), Hired.Headline, FString(TEXT("The elder hired dinos")));
	TestEqual(TEXT("Hunt clock"), Hired.Detail, FString(TEXT("120s")));

	const FGFSpottedAlert Partial = GFFormatSpottedAlert(EGFTheftHeat::Hunted, 1.2f, true);
	TestEqual(TEXT("Countdown rounds up"), Partial.Detail, FString(TEXT("2s")));
	TestEqual(TEXT("A zero clock still shows"), GFFormatSpottedAlert(EGFTheftHeat::Hunted, 0.f, true).Detail, FString(TEXT("0s")));
	TestEqual(TEXT("A negative clock clamps"), GFFormatSpottedAlert(EGFTheftHeat::Hunted, -3.f, true).Detail, FString(TEXT("0s")));

	const FGFSpottedAlert Hidden = GFFormatSpottedAlert(EGFTheftHeat::Hunted, Heat.HuntLeft, false);
	TestFalse(TEXT("Pause or the elder menu hides the banner"), Hidden.bVisible);

	TestTrue(TEXT("No hired dino left clears the heat"), FGFTheftRules::AdvanceHeat(Heat, 0.f, true, 0));
	TestTrue(TEXT("Heat is clear"), Heat.State == EGFTheftHeat::Clear);
	const FGFSpottedAlert After = GFFormatSpottedAlert(Heat.State, Heat.HuntLeft, true);
	TestFalse(TEXT("The banner clears with the heat"), After.bVisible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFInteractPromptTest, "GFGame.HUD.InteractPrompt", FieldHudFlags)
bool FGFInteractPromptTest::RunTest(const FString&)
{
	const FGFInteractPrompt Talk = GFFormatInteractPrompt(true, TEXT("X"), TEXT("Talk to Rime Elder"), true);
	TestTrue(TEXT("In reach shows the prompt"), Talk.bVisible);
	TestEqual(TEXT("Key and verb"), Talk.Line, FString(TEXT("[X] Talk to Rime Elder")));

	TestFalse(TEXT("Out of reach hides it"), GFFormatInteractPrompt(false, TEXT("X"), TEXT("Talk to Rime Elder"), true).bVisible);
	TestFalse(TEXT("An empty verb hides it"), GFFormatInteractPrompt(true, TEXT("X"), FString(), true).bVisible);
	TestFalse(TEXT("An empty key hides it"), GFFormatInteractPrompt(true, FString(), TEXT("Talk to Rime Elder"), true).bVisible);
	TestFalse(TEXT("The elder menu hides it"), GFFormatInteractPrompt(true, TEXT("X"), TEXT("Talk to Rime Elder"), false).bVisible);
	TestFalse(TEXT("Pause hides the prompt"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Pause, EGFHudModal::Game));
	TestFalse(TEXT("The elder modal hides the prompt"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Elder, EGFHudModal::Game));
	TestTrue(TEXT("The game shows the prompt"), FGFHudModalRules::ShowsFieldHud(EGFHudModal::Game, EGFHudModal::Game));

	const FGFInteractPrompt Villager = GFFormatInteractPrompt(true, TEXT("X"), TEXT("Talk to Verdant Villager"), true);
	TestEqual(TEXT("Villager prompt"), Villager.Line, FString(TEXT("[X] Talk to Verdant Villager")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFieldHudLayoutTest, "GFGame.HUD.Layout.NoOverlap", FieldHudFlags)
bool FGFFieldHudLayoutTest::RunTest(const FString&)
{
	const FGFFieldHudLayout Empty = GFLayoutFieldHud(1280.f, 720.f, 0, false, false);
	TestTrue(TEXT("Hidden tracker"), Empty.Tracker.IsEmpty());
	TestTrue(TEXT("Hidden banner"), Empty.Spotted.IsEmpty());
	TestTrue(TEXT("Hidden prompt"), Empty.Interact.IsEmpty());
	TestFalse(TEXT("Empty boxes do not overlap the hotbar"), FieldOverlap(Empty.Tracker, GFHudHotbarBounds(1280.f, 720.f)));

	struct FCanvas
	{
		float W = 0.f;
		float H = 0.f;
	};
	const FCanvas Canvases[] = { { 1280.f, 720.f }, { 1920.f, 1080.f } };
	for (const FCanvas& Size : Canvases)
	{
		const float W = Size.W;
		const float H = Size.H;
		const FString Tag = FString::Printf(TEXT("%.0fx%.0f"), W, H);
		const FGFFieldHudLayout Layout = GFLayoutFieldHud(W, H, 5, true, true);
		const FGFHudRect Hotbar = GFHudHotbarBounds(W, H);
		const FGFHudRect Stamina = GFHudStaminaBounds(W, H);
		const FGFHudRect Cross = GFHudCrosshairBounds(W, H);
		const FGFHudRect Boxes[] = { Layout.Tracker, Layout.Spotted, Layout.Interact };
		const TCHAR* Names[] = { TEXT("tracker"), TEXT("banner"), TEXT("prompt") };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestTrue(*FString::Printf(TEXT("%s %s is placed"), *Tag, Names[Index]), !Boxes[Index].IsEmpty());
			TestTrue(*FString::Printf(TEXT("%s %s inside"), *Tag, Names[Index]), GFHudRectInside(Boxes[Index], W, H));
			TestFalse(*FString::Printf(TEXT("%s %s misses the hotbar"), *Tag, Names[Index]), FieldOverlap(Boxes[Index], Hotbar));
			TestFalse(*FString::Printf(TEXT("%s %s misses the stamina arc"), *Tag, Names[Index]), FieldOverlap(Boxes[Index], Stamina));
			TestFalse(*FString::Printf(TEXT("%s %s misses the crosshair"), *Tag, Names[Index]), FieldOverlap(Boxes[Index], Cross));
		}
		TestFalse(*FString::Printf(TEXT("%s tracker misses the banner"), *Tag), FieldOverlap(Layout.Tracker, Layout.Spotted));
		TestFalse(*FString::Printf(TEXT("%s tracker misses the prompt"), *Tag), FieldOverlap(Layout.Tracker, Layout.Interact));
		TestFalse(*FString::Printf(TEXT("%s banner misses the prompt"), *Tag), FieldOverlap(Layout.Spotted, Layout.Interact));

		const TArray<FGFHUDSlotRect> Row = GFLayoutHotbar(9, 0, W, H);
		TestTrue(*FString::Printf(TEXT("%s hotbar has slots"), *Tag), Row.Num() == 9);
		for (const FGFHUDSlotRect& Slot : Row)
		{
			FGFHudRect Box;
			Box.X = Slot.X;
			Box.Y = Slot.Y;
			Box.W = Slot.W;
			Box.H = Slot.H;
			TestTrue(*FString::Printf(TEXT("%s slot sits in the hotbar band"), *Tag), Box.Y >= Hotbar.Y - 0.05f && Box.Bottom() <= Hotbar.Bottom() + 0.05f);
			TestFalse(*FString::Printf(TEXT("%s slot misses the tracker"), *Tag), FieldOverlap(Box, Layout.Tracker));
			TestFalse(*FString::Printf(TEXT("%s slot misses the prompt"), *Tag), FieldOverlap(Box, Layout.Interact));
		}

		// Side line tracker: rank, line title, step title, n/m, hint (5), and the shorter shapes without progress or hint.
		for (int32 SideLines = 2; SideLines <= 5; ++SideLines)
		{
			const FGFFieldHudLayout Side = GFLayoutFieldHud(W, H, SideLines, true, true);
			const FString SideTag = FString::Printf(TEXT("%s side tracker %d lines"), *Tag, SideLines);
			TestTrue(*FString::Printf(TEXT("%s placed"), *SideTag), !Side.Tracker.IsEmpty());
			TestTrue(*FString::Printf(TEXT("%s inside"), *SideTag), GFHudRectInside(Side.Tracker, W, H));
			TestFalse(*FString::Printf(TEXT("%s misses the hotbar"), *SideTag), FieldOverlap(Side.Tracker, Hotbar));
			TestFalse(*FString::Printf(TEXT("%s misses the stamina arc"), *SideTag), FieldOverlap(Side.Tracker, Stamina));
			TestFalse(*FString::Printf(TEXT("%s misses the crosshair"), *SideTag), FieldOverlap(Side.Tracker, Cross));
			TestFalse(*FString::Printf(TEXT("%s misses the banner"), *SideTag), FieldOverlap(Side.Tracker, Side.Spotted));
			TestFalse(*FString::Printf(TEXT("%s misses the prompt"), *SideTag), FieldOverlap(Side.Tracker, Side.Interact));
		}
		const FGFSideQuestCatalog& SideRows = FGFSideQuestCatalog::Builtin();
		for (const FGFSideQuestLineRule& Line : SideRows.Lines)
		{
			for (const FGFSideQuestStepRule& StepRule : Line.Steps)
			{
				FGFSideTrackerInput Input;
				Input.Status = EGFSideQuestStatus::Active;
				Input.LineId = Line.LineId;
				Input.StepId = StepRule.StepId;
				const FGFQuestTrackerView View = GFFormatSideQuestTracker(FGFQuestCatalog::Builtin(), EGFQuestStep::Done, 0, EGFFactionRank::Member, SideRows, Input, true);
				const FGFFieldHudLayout Shipped = GFLayoutFieldHud(W, H, View.Lines.Num(), true, true);
				const FString ShipTag = FString::Printf(TEXT("%s %s %s"), *Tag, *Line.LineId.ToString(), *StepRule.StepId.ToString());
				TestTrue(*FString::Printf(TEXT("%s fits five lines"), *ShipTag), View.Lines.Num() <= 5);
				TestTrue(*FString::Printf(TEXT("%s inside"), *ShipTag), GFHudRectInside(Shipped.Tracker, W, H));
				TestFalse(*FString::Printf(TEXT("%s misses the hotbar"), *ShipTag), FieldOverlap(Shipped.Tracker, Hotbar));
				TestFalse(*FString::Printf(TEXT("%s misses the crosshair"), *ShipTag), FieldOverlap(Shipped.Tracker, Cross));
				TestFalse(*FString::Printf(TEXT("%s misses the stamina arc"), *ShipTag), FieldOverlap(Shipped.Tracker, Stamina));
				TestFalse(*FString::Printf(TEXT("%s misses the banner"), *ShipTag), FieldOverlap(Shipped.Tracker, Shipped.Spotted));
			}
		}

		const FGFFieldHudLayout Chip = GFLayoutFieldHud(W, H, 1, false, false);
		TestTrue(*FString::Printf(TEXT("%s rank chip inside"), *Tag), GFHudRectInside(Chip.Tracker, W, H));
		TestFalse(*FString::Printf(TEXT("%s rank chip misses the crosshair"), *Tag), FieldOverlap(Chip.Tracker, Cross));
		TestFalse(*FString::Printf(TEXT("%s rank chip misses the hotbar"), *Tag), FieldOverlap(Chip.Tracker, Hotbar));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnConfirmEscapeTest, "GFGame.HUD.ReturnConfirm.Escape", FieldHudFlags)
bool FGFReturnConfirmEscapeTest::RunTest(const FString&)
{
	// Pause, Return to hub, confirm is up. Escape used to leave the confirm flag set, so the next hub key traveled.
	FGFHudModalRules::FState State = FGFHudModalRules::Transition(EGFHudModal::Pause, EGFHudModal::Game, EGFHudModalEvent::OpenConfirm);
	TestTrue(TEXT("Confirm is its own modal"), State.Modal == EGFHudModal::PauseConfirm);
	TestTrue(TEXT("Hub key would confirm"), FGFHudModalRules::ConfirmsReturn(State.Modal));

	State = FGFHudModalRules::Transition(State.Modal, State.Restore, EGFHudModalEvent::Escape);
	TestTrue(TEXT("Escape leaves the confirm"), State.Modal == EGFHudModal::Game);
	TestFalse(TEXT("Escape does not confirm the return"), FGFHudModalRules::ConfirmsReturn(State.Modal));

	State = FGFHudModalRules::Transition(State.Modal, State.Restore, EGFHudModalEvent::OpenConfirm);
	TestTrue(TEXT("The next hub key asks again"), State.Modal == EGFHudModal::PauseConfirm);
	TestTrue(TEXT("Only the new confirm accepts the hub key"), FGFHudModalRules::ConfirmsReturn(State.Modal));

	FGFHudModalRules::FState FromGame = FGFHudModalRules::Transition(EGFHudModal::Game, EGFHudModal::Game, EGFHudModalEvent::OpenConfirm);
	TestTrue(TEXT("The fallback key opens the same confirm"), FromGame.Modal == EGFHudModal::PauseConfirm);
	FromGame = FGFHudModalRules::Transition(FromGame.Modal, FromGame.Restore, EGFHudModalEvent::Escape);
	TestFalse(TEXT("Escape from the key confirm does not confirm"), FGFHudModalRules::ConfirmsReturn(FromGame.Modal));
	TestTrue(TEXT("The game is not a confirm"), !FGFHudModalRules::ConfirmsReturn(EGFHudModal::Game));
	TestTrue(TEXT("Pause is not a confirm"), !FGFHudModalRules::ConfirmsReturn(EGFHudModal::Pause));
	TestTrue(TEXT("Chat is not a confirm"), !FGFHudModalRules::ConfirmsReturn(EGFHudModal::Chat));
	TestTrue(TEXT("Aim is not a confirm"), !FGFHudModalRules::ConfirmsReturn(EGFHudModal::Aim));
	return true;
}

#endif
