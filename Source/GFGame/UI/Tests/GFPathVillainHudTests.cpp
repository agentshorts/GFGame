// Nameplate, villain badge and the first-spawn path cards. No widgets.
#include "Misc/AutomationTest.h"
#include "GFHUDFormat.h"
#include "GFPathRules.h"
#include "GFVillainRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags PathHudFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathVillainHudTest, "GFGame.HUD.PathsAndVillain", PathHudFlags)
bool FGFPathVillainHudTest::RunTest(const FString&)
{
	TestEqual(TEXT("Hint copy stays the movement line"), FString(GFHUD::ControlsHint()),
		FString(TEXT("WASD move, Shift sprint, Ctrl dash, Q swing, E yank, Space jump, LMB dig, RMB place or aim, wheel block, Alt+wheel zoom, MMB shoulder, V view, Enter chat, Esc settings")));
	TestEqual(TEXT("Villain sight matches the rule"), GFHUD::VillainSightCm, FGFVillainRules::SightCm);

	const FGFNameplateText None = GFFormatNameplate(FString(), EGFPath::None, false, FString(), true);
	TestEqual(TEXT("Nameless"), None.Line, FString(TEXT("Someone")));
	TestFalse(TEXT("No class yet"), None.bShowPath);
	TestFalse(TEXT("Not a villain"), None.bShowVillain);

	const FGFNameplateText Near = GFFormatNameplate(TEXT("Ada"), EGFPath::Delver, true, TEXT("the Rude"), true);
	TestTrue(TEXT("Class icon"), Near.bShowPath);
	TestEqual(TEXT("Delver icon"), Near.PathIcon, FString(TEXT("Path.Delver")));
	TestTrue(TEXT("Skull in sight"), Near.bShowVillain);
	TestEqual(TEXT("Skull icon"), Near.VillainIcon, FString(TEXT("Villain.Skull")));
	TestTrue(TEXT("Name carries the class"), Near.Line.Contains(TEXT("[Delver]")));
	TestTrue(TEXT("Name carries the title"), Near.Line.Contains(TEXT("the Rude")));

	const FGFNameplateText Far = GFFormatNameplate(TEXT("Ada"), EGFPath::Scavenger, true, TEXT("the Rude"), false);
	TestTrue(TEXT("Class icon stays past 60 m"), Far.bShowPath);
	TestFalse(TEXT("Skull hides past 60 m"), Far.bShowVillain);
	TestTrue(TEXT("Villain mark hidden"), Far.VillainIcon.IsEmpty());
	TestFalse(TEXT("Title hidden with the skull"), Far.Line.Contains(TEXT("the Rude")));
	TestTrue(TEXT("Scavenger still named"), Far.Line.Contains(TEXT("[Scavenger]")));

	const FGFNameplateText Pending = GFFormatNameplate(TEXT("Bea"), EGFPath::UnnamedFourth, false, FString(), true);
	TestEqual(TEXT("Pending icon"), Pending.PathIcon, FString(TEXT("Path.PendingA")));
	TestTrue(TEXT("Pending name"), Pending.Line.Contains(TEXT("[Name pending]")));

	TestTrue(TEXT("Badge hidden while off"), GFFormatVillainBadge(false, 12.f).IsEmpty());
	TestEqual(TEXT("Badge"), GFFormatVillainBadge(true, 0.f), FString(TEXT("VILLAIN")));
	TestEqual(TEXT("Lock countdown rounds up"), GFFormatVillainBadge(true, 29.1f), FString(TEXT("VILLAIN 30s")));

	const TArray<FString> Lines = GFFormatPathCards(FGFPathRules::Cards());
	TestEqual(TEXT("Header plus two lines a card"), Lines.Num(), 11);
	TestEqual(TEXT("Pick prompt"), Lines[0], FString(TEXT("Pick a path")));
	TestEqual(TEXT("First card"), Lines[1], FString(TEXT("1  Delver")));
	TestEqual(TEXT("Third card"), Lines[5], FString(TEXT("3  Scavenger")));
	TestTrue(TEXT("Fourth card is unnamed"), Lines[7].Contains(TEXT("Name pending")));
	TestTrue(TEXT("Fifth card is unnamed"), Lines[9].Contains(TEXT("Name pending")));
	TestTrue(TEXT("Scavenger card talks about glints"), Lines[6].Contains(TEXT("glint")));
	TestFalse(TEXT("Scavenger card does not talk about speed"), Lines[6].Contains(TEXT("speed")));
	return true;
}

#endif
