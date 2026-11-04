// The bag (Tab or I): the modal, the keys, the pick-and-drop cursor, the hotbar move rules, the tab view over
// DT_Items, the layout and the tooltip. Pure rules, no canvas and no world. Server moves are
// GFGame.Net.Hotbar.Moves; the save round-trip is GFGame.Save.Hotbar.RoundTrip.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.HUD.Inventory;GFGame.HUD.Hotbar;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "GFDefaultInputKeys.h"
#include "GFHotbarRules.h"
#include "GFHUD.h"
#include "GFHUDFormat.h"
#include "GFInventoryScreen.h"
#include "GFInventoryView.h"
#include "GFItemTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFInventoryTestDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FGFHudModalRules::FState Step(const FGFHudModalRules::FState& From, EGFHudModalEvent Event)
	{
		return FGFHudModalRules::Transition(From.Modal, From.Restore, Event);
	}

	FGFHudModalRules::FState At(EGFHudModal Modal, EGFHudModal Restore = EGFHudModal::Game)
	{
		FGFHudModalRules::FState State;
		State.Modal = Modal;
		State.Restore = Restore;
		return State;
	}

	TArray<FName> Bar(std::initializer_list<const TCHAR*> Ids)
	{
		TArray<FName> Out;
		for (const TCHAR* Id : Ids)
		{
			Out.Add(Id ? FName(Id) : FName());
		}
		return Out;
	}

	bool IsMove(const TOptional<FGFHotbarMove>& Got, const FGFHotbarMove& Want)
	{
		return Got.IsSet() && Got.GetValue() == Want;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFInventoryModalTest, "GFGame.HUD.Inventory.Modal", GFInventoryTestDetail::Flags)
bool FGFInventoryModalTest::RunTest(const FString&)
{
	using namespace GFInventoryTestDetail;
	const FGFHudModalRules::FState Open = Step(At(EGFHudModal::Game), EGFHudModalEvent::ToggleInventory);
	TestTrue(TEXT("Tab opens the bag from the game"), Open.Modal == EGFHudModal::Inventory);
	TestTrue(TEXT("The bag holds the pawn"), FGFHudModalRules::HoldsPawn(Open.Modal, Open.Restore));
	TestTrue(TEXT("The bag frees the mouse"), FGFHudModalRules::FreesMouse(Open.Modal));
	TestTrue(TEXT("The tracker and banners stay up"), FGFHudModalRules::ShowsFieldHud(Open.Modal, Open.Restore));
	TestFalse(TEXT("The bag is not pause"), FGFHudModalRules::IsPause(Open.Modal));
	TestTrue(TEXT("Tab again closes it"), Step(Open, EGFHudModalEvent::ToggleInventory).Modal == EGFHudModal::Game);
	TestTrue(TEXT("Escape closes it, not into pause"), Step(Open, EGFHudModalEvent::Escape).Modal == EGFHudModal::Game);
	TestTrue(TEXT("Close closes it"), Step(Open, EGFHudModalEvent::CloseInventory).Modal == EGFHudModal::Game);
	TestTrue(TEXT("Open is idempotent"), Step(Open, EGFHudModalEvent::OpenInventory).Modal == EGFHudModal::Inventory);

	TestTrue(TEXT("Tab over pause does nothing"), Step(At(EGFHudModal::Pause), EGFHudModalEvent::ToggleInventory).Modal == EGFHudModal::Pause);
	TestTrue(TEXT("Tab over the elder does nothing"), Step(At(EGFHudModal::Elder), EGFHudModalEvent::ToggleInventory).Modal == EGFHudModal::Elder);
	TestTrue(TEXT("Tab over aim settings does nothing"), Step(At(EGFHudModal::Aim), EGFHudModalEvent::ToggleInventory).Modal == EGFHudModal::Aim);
	TestTrue(TEXT("Tab over chat does nothing"), Step(At(EGFHudModal::Chat), EGFHudModalEvent::ToggleInventory).Modal == EGFHudModal::Chat);

	const FGFHudModalRules::FState Chat = Step(Open, EGFHudModalEvent::OpenChat);
	TestTrue(TEXT("Chat opens over the bag"), Chat.Modal == EGFHudModal::Chat && Chat.Restore == EGFHudModal::Inventory);
	TestTrue(TEXT("Chat over the bag still holds the pawn"), FGFHudModalRules::HoldsPawn(Chat.Modal, Chat.Restore));
	TestTrue(TEXT("Closing chat goes back to the bag"), Step(Chat, EGFHudModalEvent::CloseChat).Modal == EGFHudModal::Inventory);
	const FGFHudModalRules::FState ClosedUnder = Step(Chat, EGFHudModalEvent::CloseInventory);
	TestTrue(TEXT("Closing the bag under chat leaves chat up over the game"), ClosedUnder.Modal == EGFHudModal::Chat && ClosedUnder.Restore == EGFHudModal::Game);

	TestTrue(TEXT("A late elder reply does not replace the bag"), Step(Open, EGFHudModalEvent::OpenElder).Modal == EGFHudModal::Inventory);
	TestTrue(TEXT("The hub prompt replaces the bag"), Step(Open, EGFHudModalEvent::OpenConfirm).Modal == EGFHudModal::PauseConfirm);
	TestTrue(TEXT("Teardown closes it"), Step(Open, EGFHudModalEvent::Reset).Modal == EGFHudModal::Game);
	TestTrue(TEXT("Aim settings cannot open over the bag"), Step(Open, EGFHudModalEvent::OpenAim).Modal == EGFHudModal::Inventory);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFInventoryKeysTest, "GFGame.HUD.Inventory.Keys", GFInventoryTestDetail::Flags)
bool FGFInventoryKeysTest::RunTest(const FString&)
{
	TArray<FGFMappedKey> Hud;
	FGFDefaultInputKeys::HudKeys(Hud, EKeys::Home);
	TestTrue(TEXT("Tab opens the bag and nothing else"), FGFDefaultInputKeys::KeyIsOnly(Hud, EKeys::Tab, TEXT("Inventory")));
	TestTrue(TEXT("I opens the bag and nothing else"), FGFDefaultInputKeys::KeyIsOnly(Hud, EKeys::I, TEXT("Inventory")));
	TArray<FGFMappedKey> Pawn;
	FGFDefaultInputKeys::CharacterKeys(Pawn);
	for (const FGFMappedKey& Row : Pawn)
	{
		TestFalse(FString::Printf(TEXT("%s is not on Tab or I"), *Row.Action.ToString()), Row.Key == EKeys::Tab || Row.Key == EKeys::I);
	}
	TestTrue(TEXT("The controls hint names both bag keys"), FString(GFHUD::ControlsHint()).Contains(TEXT("Tab/I bag")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHotbarMoveRulesTest, "GFGame.HUD.Hotbar.Moves", GFInventoryTestDetail::Flags)
bool FGFHotbarMoveRulesTest::RunTest(const FString&)
{
	using namespace GFInventoryTestDetail;
	const TArray<FName> Start = Bar({ TEXT("Dirt"), TEXT("Stone"), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, TEXT("Fists") });

	TArray<FName> Slots = Start;
	TestTrue(TEXT("Assign into an empty slot"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Assign(2, TEXT("Amber"))) == EGFHotbarMoveResult::Ok);
	TestEqual(TEXT("Amber is in slot 3"), Slots[2], FName(TEXT("Amber")));

	TestTrue(TEXT("Assign an id already on the bar"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Assign(0, TEXT("Amber"))) == EGFHotbarMoveResult::Ok);
	TestEqual(TEXT("It moved to slot 1"), Slots[0], FName(TEXT("Amber")));
	TestEqual(TEXT("What was there took its old place"), Slots[2], FName(TEXT("Dirt")));
	int32 Ambers = 0;
	for (const FName Id : Slots)
	{
		Ambers += Id == TEXT("Amber") ? 1 : 0;
	}
	TestEqual(TEXT("One slot per id"), Ambers, 1);

	TestTrue(TEXT("Same id on the same slot is no change"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Assign(0, TEXT("Amber"))) == EGFHotbarMoveResult::NoChange);
	TestTrue(TEXT("Swap"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Swap(0, 8)) == EGFHotbarMoveResult::Ok);
	TestTrue(TEXT("Fists moved to slot 1"), Slots[0] == TEXT("Fists") && Slots[8] == TEXT("Amber"));
	TestTrue(TEXT("Swapping two empties is no change"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Swap(4, 5)) == EGFHotbarMoveResult::NoChange);
	TestTrue(TEXT("Clear"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Clear(1)) == EGFHotbarMoveResult::Ok && Slots[1].IsNone());
	TestTrue(TEXT("Clearing an empty slot is no change"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Clear(1)) == EGFHotbarMoveResult::NoChange);
	TestTrue(TEXT("Assign None clears"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Assign(2, NAME_None)) == EGFHotbarMoveResult::Ok && Slots[2].IsNone());

	const TArray<FName> Before = Slots;
	TestTrue(TEXT("Slot 10 is refused"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Assign(9, TEXT("Dirt"))) == EGFHotbarMoveResult::BadSlot);
	TestTrue(TEXT("A negative slot is refused"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Clear(-1)) == EGFHotbarMoveResult::BadSlot);
	TestTrue(TEXT("A swap partner off the bar is refused"), FGFHotbarRules::Apply(Slots, FGFHotbarMove::Swap(0, 40)) == EGFHotbarMoveResult::BadSlot);
	TestTrue(TEXT("Refused moves change nothing"), Slots == Before);

	TArray<FName> Short = Bar({ TEXT("Dirt") });
	FGFHotbarRules::Apply(Short, FGFHotbarMove::Assign(8, TEXT("Stone")));
	TestEqual(TEXT("Short saves pad to nine"), Short.Num(), FGFHotbarRules::SlotCount);

	FGFHotbarLayout Layout;
	TestTrue(TEXT("Unarranged follows the default"), FGFHotbarRules::Resolve(Layout, Start) == Start);
	Layout.bArranged = true;
	Layout.Slots = Bar({ TEXT("Amber") });
	const TArray<FName> Resolved = FGFHotbarRules::Resolve(Layout, Start);
	TestTrue(TEXT("Arranged wins and pads"), Resolved.Num() == FGFHotbarRules::SlotCount && Resolved[0] == TEXT("Amber") && Resolved[8].IsNone());
	Layout.Slots.Init(TEXT("Dirt"), 20);
	TestEqual(TEXT("A long save is cut to nine"), FGFHotbarRules::Resolve(Layout, Start).Num(), FGFHotbarRules::SlotCount);

	TestTrue(TEXT("None is always fine"), FGFHotbarRules::CheckId(NAME_None, false, false, 0, false) == EGFHotbarMoveResult::Ok);
	TestTrue(TEXT("Fists are always fine"), FGFHotbarRules::CheckId(FGFHotbarRules::FistsId(), false, false, 0, false) == EGFHotbarMoveResult::Ok);
	TestTrue(TEXT("Unknown ids are refused"), FGFHotbarRules::CheckId(TEXT("Nope"), false, false, 5, true) == EGFHotbarMoveResult::UnknownItem);
	TestTrue(TEXT("Owned items are fine"), FGFHotbarRules::CheckId(TEXT("Amber"), true, false, 1, false) == EGFHotbarMoveResult::Ok);
	TestTrue(TEXT("Unowned items are refused"), FGFHotbarRules::CheckId(TEXT("Amber"), true, false, 0, false) == EGFHotbarMoveResult::NotOwned);
	TestTrue(TEXT("Free build does not hand out items"), FGFHotbarRules::CheckId(TEXT("Amber"), true, false, 0, true) == EGFHotbarMoveResult::NotOwned);
	TestTrue(TEXT("Free build lets any block through"), FGFHotbarRules::CheckId(TEXT("Grass"), true, true, 0, true) == EGFHotbarMoveResult::Ok);

	// #330: running out keeps the slot. It greys out at 0 and fills again when the item comes back.
	TestTrue(TEXT("An item at 0 is spent"), FGFHotbarRules::IsSpent(TEXT("Amber"), 0));
	TestFalse(TEXT("An item with a count is not"), FGFHotbarRules::IsSpent(TEXT("Amber"), 3));
	TestFalse(TEXT("No count (free build) is not"), FGFHotbarRules::IsSpent(TEXT("Dirt"), -1));
	TestFalse(TEXT("An empty slot is not"), FGFHotbarRules::IsSpent(NAME_None, 0));
	TestFalse(TEXT("Fists are never spent"), FGFHotbarRules::IsSpent(FGFHotbarRules::FistsId(), 0));
	FGFHotbarLayout Sticky;
	Sticky.bArranged = true;
	Sticky.Slots = Bar({ TEXT("Grass"), TEXT("Dirt"), nullptr, TEXT("Amber") });
	const TArray<FName> Kept = FGFHotbarRules::Resolve(Sticky, Start);
	TestTrue(TEXT("The arranged bar still shows the spent items where they were"), Kept[0] == TEXT("Grass") && Kept[1] == TEXT("Dirt") && Kept[3] == TEXT("Amber"));
	TestTrue(TEXT("A spent slot can still be moved"), FGFHotbarRules::Apply(Sticky.Slots, FGFHotbarMove::Swap(3, 5)) == EGFHotbarMoveResult::Ok && Sticky.Slots[5] == TEXT("Amber"));
	TestTrue(TEXT("Or cleared by hand"), FGFHotbarRules::Apply(Sticky.Slots, FGFHotbarMove::Clear(5)) == EGFHotbarMoveResult::Ok && Sticky.Slots[5].IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFInventoryCursorTest, "GFGame.HUD.Inventory.Cursor", GFInventoryTestDetail::Flags)
bool FGFInventoryCursorTest::RunTest(const FString&)
{
	using namespace GFInventoryTestDetail;
	const FGFInvTarget Amber = FGFInvTarget::Grid(TEXT("Amber"));
	const FGFInvTarget Slot3 = FGFInvTarget::BarSlot(2, NAME_None);
	const FGFInvTarget Slot1Dirt = FGFInvTarget::BarSlot(0, TEXT("Dirt"));
	const FGFInvTarget Slot5Stone = FGFInvTarget::BarSlot(4, TEXT("Stone"));

	{
		FGFInventoryCursor Cursor;
		TestFalse(TEXT("Press picks, no move yet"), Cursor.Press(Amber).IsSet());
		TestTrue(TEXT("Holding Amber"), Cursor.IsHolding() && Cursor.GetHeld() == TEXT("Amber"));
		TestTrue(TEXT("Drag grid to slot assigns"), IsMove(Cursor.Release(Slot3), FGFHotbarMove::Assign(2, TEXT("Amber"))));
		TestFalse(TEXT("Dropped"), Cursor.IsHolding());
	}
	{
		FGFInventoryCursor Cursor;
		Cursor.Press(Amber);
		TestFalse(TEXT("Release on the same cell is a click"), Cursor.Release(Amber).IsSet());
		TestTrue(TEXT("Still holding after the click"), Cursor.IsHolding() && !Cursor.IsButtonDown());
		TestTrue(TEXT("Second click on a slot assigns"), IsMove(Cursor.Press(Slot3), FGFHotbarMove::Assign(2, TEXT("Amber"))));
		TestFalse(TEXT("The second click's release does nothing"), Cursor.Release(Amber).IsSet());
		TestFalse(TEXT("Not holding"), Cursor.IsHolding());
	}
	{
		FGFInventoryCursor Cursor;
		Cursor.Press(Slot1Dirt);
		TestTrue(TEXT("Drag slot to slot swaps"), IsMove(Cursor.Release(Slot5Stone), FGFHotbarMove::Swap(0, 4)));
	}
	{
		FGFInventoryCursor Cursor;
		Cursor.Press(Slot1Dirt);
		TestTrue(TEXT("Drag a slot off the bar clears it"), IsMove(Cursor.Release(FGFInvTarget::Nothing()), FGFHotbarMove::Clear(0)));
	}
	{
		FGFInventoryCursor Cursor;
		Cursor.Press(Slot1Dirt);
		TestTrue(TEXT("Drag a slot onto the grid clears it"), IsMove(Cursor.Release(Amber), FGFHotbarMove::Clear(0)));
	}
	{
		FGFInventoryCursor Cursor;
		Cursor.Press(Slot1Dirt);
		Cursor.Release(Slot1Dirt);
		TestTrue(TEXT("Click a slot holds it"), Cursor.IsHolding() && Cursor.GetFromSlot() == 0);
		TestFalse(TEXT("Clicking it again puts it back"), Cursor.Press(Slot1Dirt).IsSet());
		TestFalse(TEXT("Back where it was"), Cursor.IsHolding());
	}
	{
		FGFInventoryCursor Cursor;
		Cursor.Press(Amber);
		TestFalse(TEXT("A grid item dropped on nothing does nothing"), Cursor.Release(FGFInvTarget::Nothing()).IsSet());
		TestFalse(TEXT("And is let go"), Cursor.IsHolding());
	}
	{
		FGFInventoryCursor Cursor;
		TestFalse(TEXT("An empty slot picks nothing"), Cursor.Press(Slot3).IsSet());
		TestFalse(TEXT("Not holding"), Cursor.IsHolding());
		TestFalse(TEXT("Its release does nothing"), Cursor.Release(Slot5Stone).IsSet());
	}
	{
		FGFInventoryCursor Cursor;
		TestTrue(TEXT("Number key over a grid item assigns it"), IsMove(Cursor.NumberKey(6, Amber), FGFHotbarMove::Assign(6, TEXT("Amber"))));
		TestTrue(TEXT("Number key over a slot swaps"), IsMove(Cursor.NumberKey(6, Slot1Dirt), FGFHotbarMove::Swap(0, 6)));
		TestFalse(TEXT("Number key over its own slot does nothing"), Cursor.NumberKey(0, Slot1Dirt).IsSet());
		TestFalse(TEXT("Number key over nothing does nothing"), Cursor.NumberKey(3, FGFInvTarget::Nothing()).IsSet());
		TestFalse(TEXT("Key 10 does nothing"), Cursor.NumberKey(9, Amber).IsSet());
		Cursor.Press(Amber);
		Cursor.Release(Amber);
		TestTrue(TEXT("Number key while holding drops there"), IsMove(Cursor.NumberKey(1, FGFInvTarget::Nothing()), FGFHotbarMove::Assign(1, TEXT("Amber"))));
		TestFalse(TEXT("Let go"), Cursor.IsHolding());
	}
	{
		FGFInventoryCursor Cursor;
		Cursor.Press(Amber);
		Cursor.Cancel();
		TestFalse(TEXT("Cancel lets go"), Cursor.IsHolding() || Cursor.IsButtonDown());
		TestFalse(TEXT("A stray release after cancel does nothing"), Cursor.Release(Slot3).IsSet());
	}
	{
		// #304 pass 2: the bag closed between the drop press and its release. The swallow must not outlive Cancel.
		FGFInventoryCursor Cursor;
		Cursor.Press(Amber);
		Cursor.Release(Amber);
		TestTrue(TEXT("Drop press assigns"), IsMove(Cursor.Press(Slot3), FGFHotbarMove::Assign(2, TEXT("Amber"))));
		TestTrue(TEXT("Waiting to swallow its release"), Cursor.GetPhase() == EGFInvCursorPhase::Dropping);
		Cursor.Cancel();
		TestTrue(TEXT("Cancel goes all the way to idle"), Cursor.GetPhase() == EGFInvCursorPhase::Idle);
		Cursor.Press(Slot1Dirt);
		TestTrue(TEXT("The next drag after reopening works"), IsMove(Cursor.Release(Slot5Stone), FGFHotbarMove::Swap(0, 4)));
		TestTrue(TEXT("And ends idle"), Cursor.GetPhase() == EGFInvCursorPhase::Idle);
	}
	{
		// A release that reached no box: the HUD now routes it as Nothing, which takes a bar item off the bar.
		FGFInventoryCursor Cursor;
		Cursor.Press(Slot1Dirt);
		TestTrue(TEXT("Dragging"), Cursor.GetPhase() == EGFInvCursorPhase::Dragging && Cursor.IsButtonDown());
		TestTrue(TEXT("Stray release off the panel clears the slot"), IsMove(Cursor.Release(FGFInvTarget::Nothing()), FGFHotbarMove::Clear(0)));
		TestFalse(TEXT("Not stuck down"), Cursor.IsButtonDown());
	}
	{
		// If a release is lost anyway, the next press starts clean instead of swallowing a later release.
		FGFInventoryCursor Cursor;
		Cursor.Press(Amber);
		Cursor.Release(Amber);
		Cursor.Press(Slot3);
		TestFalse(TEXT("Lost drop release, new press picks up"), Cursor.Press(Slot1Dirt).IsSet());
		TestTrue(TEXT("Picked up the slot"), Cursor.GetPhase() == EGFInvCursorPhase::Dragging && Cursor.GetFromSlot() == 0);
		TestTrue(TEXT("Its release is not swallowed"), IsMove(Cursor.Release(Slot5Stone), FGFHotbarMove::Swap(0, 4)));
	}
	{
		// A number key mid-drag drops, and the button's later release is swallowed exactly once.
		FGFInventoryCursor Cursor;
		Cursor.Press(Amber);
		TestTrue(TEXT("Number key mid-drag assigns"), IsMove(Cursor.NumberKey(4, FGFInvTarget::Nothing()), FGFHotbarMove::Assign(4, TEXT("Amber"))));
		TestTrue(TEXT("Waiting for the button"), Cursor.GetPhase() == EGFInvCursorPhase::Dropping);
		TestFalse(TEXT("Its release does nothing"), Cursor.Release(Slot3).IsSet());
		TestTrue(TEXT("Idle after"), Cursor.GetPhase() == EGFInvCursorPhase::Idle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFInventoryViewTest, "GFGame.HUD.Inventory.View", GFInventoryTestDetail::Flags)
bool FGFInventoryViewTest::RunTest(const FString&)
{
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	FGFInventory Bag;
	Bag.Add(TEXT("Amber"), 2);
	Bag.Add(TEXT("Dirt"), 7);
	Bag.Add(TEXT("Stone"), 3);
	Bag.Add(TEXT("GlowDust"), 4);
	Bag.Add(TEXT("Skin_Raptor"), 1);
	Bag.Add(TEXT("FernBraid"), 1);
	Bag.Add(TEXT("Berry"), 5);
	Bag.Add(TEXT("NotARealRow"), 1);
	const TArray<FGFHotbarBlock> Blocks = {
		{ TEXT("Grass"), TEXT("Dirt") },
		{ TEXT("Dirt"), TEXT("Dirt") },
		{ TEXT("Stone"), TEXT("Stone") },
		{ TEXT("Glass"), TEXT("Glass") },
	};
	const TArray<FGFInventoryEntry> All = FGFInventoryView::Build(Bag, Blocks, Items, false);
	auto Ids = [](const TArray<FGFInventoryEntry>& Entries)
	{
		TArray<FName> Out;
		for (const FGFInventoryEntry& Entry : Entries)
		{
			Out.Add(Entry.Id);
		}
		return Out;
	};
	const TArray<FName> BlockTab = Ids(FGFInventoryView::Filter(All, EGFInventoryTab::Blocks));
	TestTrue(TEXT("Blocks you can spend, in table order, Grass included"), BlockTab == TArray<FName>{ TEXT("Grass"), TEXT("Dirt"), TEXT("Stone") });
	TestTrue(TEXT("Crystals are the Glow rows in table order"), Ids(FGFInventoryView::Filter(All, EGFInventoryTab::Crystals)) == TArray<FName>{ TEXT("GlowDust"), TEXT("Amber") });
	TestTrue(TEXT("Materials"), Ids(FGFInventoryView::Filter(All, EGFInventoryTab::Materials)) == TArray<FName>{ TEXT("Stone") });
	const TArray<FName> Skins = Ids(FGFInventoryView::Filter(All, EGFInventoryTab::Skins));
	TestTrue(TEXT("Skins hold dino skins and outfits"), Skins.Contains(TEXT("Skin_Raptor")) && Skins.Contains(TEXT("FernBraid")) && Skins.Num() == 2);
	const TArray<FName> Other = Ids(FGFInventoryView::Filter(All, EGFInventoryTab::Other));
	TestTrue(TEXT("Treats and unknown ids are Other, unknown last"), Other == TArray<FName>{ TEXT("Berry"), TEXT("NotARealRow") });
	int32 Dirts = 0;
	for (const FName Id : Ids(All))
	{
		Dirts += Id == TEXT("Dirt") ? 1 : 0;
	}
	TestEqual(TEXT("The Dirt item is listed once, as a block"), Dirts, 1);
	for (const FGFInventoryEntry& Entry : All)
	{
		if (Entry.Id == TEXT("Grass"))
		{
			TestEqual(TEXT("Grass counts the Dirt it spends"), Entry.Count, int64(7));
			TestEqual(TEXT("Grass spends Dirt"), Entry.ItemId, FName(TEXT("Dirt")));
			TestTrue(TEXT("Grass is placeable"), Entry.bPlaceable);
		}
	}

	const TArray<FGFInventoryEntry> Free = FGFInventoryView::Build(FGFInventory(), Blocks, Items, true);
	TestEqual(TEXT("Free build lists every block"), FGFInventoryView::Filter(Free, EGFInventoryTab::Blocks).Num(), 4);
	TestEqual(TEXT("Free build blocks have no count"), Free.Num() > 0 ? Free[0].Count : int64(0), int64(-1));
	TestEqual(TEXT("Free build hands out no items"), Free.Num(), 4);

	TestEqual(TEXT("Zero entries is one page"), FGFInventoryView::PageCount(0, 48), 1);
	TestEqual(TEXT("49 entries is two pages"), FGFInventoryView::PageCount(49, 48), 2);
	TestEqual(TEXT("Page clamps"), FGFInventoryView::ClampPage(5, 49, 48), 1);
	TestEqual(TEXT("Negative page clamps"), FGFInventoryView::ClampPage(-1, 49, 48), 0);
	TestEqual(TEXT("Page label"), GFFormatInventoryPage(1, 2), FString(TEXT("Page 2 of 2")));
	TestTrue(TEXT("One page has no label"), GFFormatInventoryPage(0, 1).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFInventoryLayoutTest, "GFGame.HUD.Inventory.Layout", GFInventoryTestDetail::Flags)
bool FGFInventoryLayoutTest::RunTest(const FString&)
{
	struct FSize { float W; float H; };
	for (const FSize Size : { FSize{ 1280.f, 720.f }, FSize{ 1920.f, 1080.f } })
	{
		const FString At = FString::Printf(TEXT(" at %.0fx%.0f"), Size.W, Size.H);
		const FGFInventoryLayout L = GFLayoutInventory(Size.W, Size.H);
		const FGFFieldHudLayout Field = GFLayoutFieldHud(Size.W, Size.H, 8, true, true);
		TestTrue(TEXT("Panel inside the canvas") + At, GFHudRectInside(L.Panel, Size.W, Size.H));
		TestFalse(TEXT("Panel clear of the hotbar") + At, GFHudRectsOverlap(L.Panel, GFHudHotbarBounds(Size.W, Size.H)));
		TestFalse(TEXT("Panel clear of the tracker") + At, GFHudRectsOverlap(L.Panel, Field.Tracker));
		TestFalse(TEXT("Panel clear of the spotted banner") + At, GFHudRectsOverlap(L.Panel, Field.Spotted));
		TestFalse(TEXT("Panel clear of the interact prompt") + At, GFHudRectsOverlap(L.Panel, Field.Interact));
		TestFalse(TEXT("Panel clear of the crosshair") + At, GFHudRectsOverlap(L.Panel, GFHudCrosshairBounds(Size.W, Size.H)));
		TestTrue(TEXT("At least six columns") + At, L.Columns >= 6);
		TestTrue(TEXT("At least five rows") + At, L.Rows >= 5);
		TestEqual(TEXT("One cell per column and row") + At, L.Cells.Num(), L.Columns * L.Rows);
		TestEqual(TEXT("A tab per tab") + At, L.Tabs.Num(), FGFInventoryView::TabCount);
		auto InPanel = [&L](const FGFHudRect& R)
		{
			return R.X >= L.Panel.X && R.Y >= L.Panel.Y && R.Right() <= L.Panel.Right() && R.Bottom() <= L.Panel.Bottom();
		};
		bool bCellsIn = true;
		bool bCellsApart = true;
		for (int32 A = 0; A < L.Cells.Num(); ++A)
		{
			bCellsIn &= InPanel(L.Cells[A]);
			for (int32 B = A + 1; B < L.Cells.Num(); ++B)
			{
				bCellsApart &= !GFHudRectsOverlap(L.Cells[A], L.Cells[B]);
			}
		}
		TestTrue(TEXT("Cells inside the panel") + At, bCellsIn);
		TestTrue(TEXT("Cells do not overlap") + At, bCellsApart);
		bool bTabsIn = true;
		for (const FGFHudRect& Tab : L.Tabs)
		{
			bTabsIn &= InPanel(Tab);
			for (const FGFHudRect& Cell : L.Cells)
			{
				bTabsIn &= !GFHudRectsOverlap(Tab, Cell);
			}
		}
		TestTrue(TEXT("Tabs inside the panel, above the grid") + At, bTabsIn);
		TestTrue(TEXT("Buttons inside the panel") + At, InPanel(L.Close) && InPanel(L.Prev) && InPanel(L.Next));
		TestFalse(TEXT("Prev and Next apart") + At, GFHudRectsOverlap(L.Prev, L.Next));
		TestEqual(TEXT("Hit test finds the first cell") + At, GFHudHitTest(L.Cells, L.Cells[0].X + 1.f, L.Cells[0].Y + 1.f), 0);
		TestEqual(TEXT("Hit test misses the gap") + At, GFHudHitTest(L.Cells, L.Cells[0].Right() + 1.f, L.Cells[0].Y + 1.f), INDEX_NONE);
	}
	const FGFInventoryLayout Small = GFLayoutInventory(640.f, 360.f);
	TestTrue(TEXT("Small canvas: still inside"), GFHudRectInside(Small.Panel, 640.f, 360.f));
	TestTrue(TEXT("Small canvas: at least one row of three"), Small.Rows >= 1 && Small.Columns >= GFHUD::InvMinColumns);

	for (const FVector2D Mouse : { FVector2D(5.f, 5.f), FVector2D(1275.f, 5.f), FVector2D(5.f, 715.f), FVector2D(1275.f, 715.f) })
	{
		const FGFHudRect Tip = GFPlaceTooltip(Mouse.X, Mouse.Y, 6, 1280.f, 720.f);
		TestTrue(FString::Printf(TEXT("Tooltip inside near %.0f,%.0f"), Mouse.X, Mouse.Y), GFHudRectInside(Tip, 1280.f, 720.f));
	}
	const FGFHudRect Tip = GFPlaceTooltip(200.f, 200.f, 6, 1280.f, 720.f);
	TestFalse(TEXT("Tooltip does not sit under the cursor"), GFHudContains(Tip, 200.f, 200.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFInventoryTooltipTest, "GFGame.HUD.Inventory.Tooltip", GFInventoryTestDetail::Flags)
bool FGFInventoryTooltipTest::RunTest(const FString&)
{
	FGFItemRow Row;
	Row.Category = EGFItemCategory::Material;
	Row.MaterialCategory = EGFMaterialCategory::Glow;
	Row.Rarity = EGFRarity::Uncommon;
	Row.Tier = 3;
	Row.Value = 8;
	FGFInventoryEntry Amber;
	Amber.Id = TEXT("Amber");
	Amber.ItemId = TEXT("Amber");
	Amber.Count = 2;
	Amber.Tab = EGFInventoryTab::Crystals;
	const TArray<FString> Lines = GFFormatInventoryTooltip(Amber, &Row, TEXT("Amber"));
	TestTrue(TEXT("Name first"), Lines.Num() > 0 && Lines[0] == TEXT("Amber"));
	TestTrue(TEXT("Tab, rarity, tier"), Lines.Contains(TEXT("Crystals, Uncommon, tier 3")));
	TestTrue(TEXT("Count"), Lines.Contains(TEXT("You have 2")));
	TestTrue(TEXT("Value"), Lines.Contains(TEXT("Value 8")));
	TestTrue(TEXT("How-to last"), Lines.Last().Contains(TEXT("1-9")));

	FGFInventoryEntry Grass;
	Grass.Id = TEXT("Grass");
	Grass.ItemId = TEXT("Dirt");
	Grass.Count = -1;
	Grass.Tab = EGFInventoryTab::Blocks;
	Grass.bPlaceable = true;
	const TArray<FString> Block = GFFormatInventoryTooltip(Grass, nullptr, FString());
	TestTrue(TEXT("Blank name falls back to the id"), Block[0] == TEXT("Grass"));
	TestTrue(TEXT("No row: just the tab"), Block.Contains(TEXT("Blocks")));
	TestTrue(TEXT("Free build"), Block.Contains(TEXT("Free build: no cost")));
	TestTrue(TEXT("Says it places"), Block.Contains(TEXT("Places a block (RMB)")));
	return true;
}

#endif
