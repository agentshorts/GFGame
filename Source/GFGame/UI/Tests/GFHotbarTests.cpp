// Hotbar: slot select (wheel and number keys), the default fill, counts, labels, the key table and the band layout.
// Pure rules. No canvas and no world. The in-world select is GFGame.Net.Hotbar.Select.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.HUD.Hotbar;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFDefaultInputKeys.h"
#include "GFHotbarRules.h"
#include "GFHUDFormat.h"
#include "GFPathRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFHotbarTestDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool HasKey(const TArray<FGFMappedKey>& Keys, FName Action, FKey Key)
	{
		for (const FGFMappedKey& Row : Keys)
		{
			if (Row.Action == Action && Row.Key == Key)
			{
				return true;
			}
		}
		return false;
	}

	/** Block rows in DT_Blocks order, with the item each spends. */
	TArray<FGFHotbarBlock> SampleBlocks()
	{
		return {
			{ TEXT("Grass"), TEXT("Dirt") },
			{ TEXT("Dirt"), TEXT("Dirt") },
			{ TEXT("Stone"), TEXT("Stone") },
			{ TEXT("WoodPlank"), TEXT("WoodPlank") },
			{ TEXT("Log"), TEXT("WoodPlank") },
			{ TEXT("Glass"), TEXT("Glass") },
			{ TEXT("Ice"), TEXT("Stone") },
			{ TEXT("Fabric"), TEXT("Fabric") },
			{ TEXT("ClayBed"), TEXT("Clay") },
			{ TEXT("OreMetal"), TEXT("OreMetal") },
			{ TEXT("CoalnutSeam"), TEXT("Coalnut") },
			{ TEXT("CrystalVein"), TEXT("Crystal") },
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHotbarSlotSelectTest, "GFGame.HUD.Hotbar.SlotSelect", GFHotbarTestDetail::Flags)
bool FGFHotbarSlotSelectTest::RunTest(const FString&)
{
	TestEqual(TEXT("Nine slots"), FGFHotbarRules::SlotCount, 9);
	TestEqual(TEXT("Fists keeps the aim id"), FGFHotbarRules::FistsId(), FName(TEXT("Fists")));

	TestEqual(TEXT("Wrap 9 is slot 0"), FGFHotbarRules::Wrap(9), 0);
	TestEqual(TEXT("Wrap -1 is slot 8"), FGFHotbarRules::Wrap(-1), 8);
	TestEqual(TEXT("Wrap -10 is slot 8"), FGFHotbarRules::Wrap(-10), 8);

	TestEqual(TEXT("Wheel up from 0"), FGFHotbarRules::Cycle(0, 1), 1);
	TestEqual(TEXT("Wheel down from 0 wraps to 8"), FGFHotbarRules::Cycle(0, -1), 8);
	TestEqual(TEXT("Wheel up from 8 wraps to 0"), FGFHotbarRules::Cycle(8, 1), 0);
	TestEqual(TEXT("A big wheel delta is still one step"), FGFHotbarRules::Cycle(3, 5), 4);
	TestEqual(TEXT("No wheel keeps the slot"), FGFHotbarRules::Cycle(4, 0), 4);
	TestEqual(TEXT("A bad current slot starts at 0"), FGFHotbarRules::Cycle(42, 1), 0);
	TSet<int32> Visited;
	int32 Walk = 0;
	for (int32 Step = 0; Step < FGFHotbarRules::SlotCount; ++Step)
	{
		Walk = FGFHotbarRules::Cycle(Walk, 1);
		Visited.Add(Walk);
	}
	TestEqual(TEXT("Nine wheel steps visit all nine slots"), Visited.Num(), FGFHotbarRules::SlotCount);

	// Number keys: 1 to 5 choose a class until it is chosen, then every key is a slot.
	TestTrue(TEXT("Five class keys"), FGFPathRules::SlotCount() == 5);
	for (int32 Key = 0; Key < 9; ++Key)
	{
		const EGFNumberKeyUse Pending = FGFHotbarRules::RouteNumberKey(Key, true);
		const EGFNumberKeyUse Chosen = FGFHotbarRules::RouteNumberKey(Key, false);
		TestTrue(*FString::Printf(TEXT("Key %d picks a class while pending only if it is 1 to 5"), Key + 1),
			Pending == (Key < 5 ? EGFNumberKeyUse::Path : EGFNumberKeyUse::Hotbar));
		TestTrue(*FString::Printf(TEXT("Key %d selects a slot once the class is chosen"), Key + 1), Chosen == EGFNumberKeyUse::Hotbar);
	}
	TestTrue(TEXT("Key 10 is ignored"), FGFHotbarRules::RouteNumberKey(9, false) == EGFNumberKeyUse::Ignore);
	TestTrue(TEXT("Negative key is ignored"), FGFHotbarRules::RouteNumberKey(-1, true) == EGFNumberKeyUse::Ignore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHotbarDefaultFillTest, "GFGame.HUD.Hotbar.DefaultFill", GFHotbarTestDetail::Flags)
bool FGFHotbarDefaultFillTest::RunTest(const FString&)
{
	using namespace GFHotbarTestDetail;
	const TArray<FGFHotbarBlock> Blocks = SampleBlocks();

	const TMap<FName, int64> Owned = { { TEXT("Dirt"), 12 }, { TEXT("Stone"), 3 }, { TEXT("Crystal"), 1 } };
	const TArray<FName> Few = FGFHotbarRules::DefaultSlots(Blocks, [&Owned](FName Item) -> int64
	{
		const int64* N = Owned.Find(Item);
		return N ? *N : 0;
	}, false);
	TestEqual(TEXT("Always nine entries"), Few.Num(), 9);
	TestEqual(TEXT("Dirt block wins over Grass for the Dirt item"), Few[0], FName(TEXT("Dirt")));
	TestEqual(TEXT("Stone block wins over Ice"), Few[1], FName(TEXT("Stone")));
	TestEqual(TEXT("Crystal vein for the crystal"), Few[2], FName(TEXT("CrystalVein")));
	TestTrue(TEXT("Unowned slots are empty"), Few[3].IsNone() && Few[7].IsNone());
	TestEqual(TEXT("Fists in the last slot"), Few[8], FGFHotbarRules::FistsId());

	const TArray<FName> None = FGFHotbarRules::DefaultSlots(Blocks, [](FName) -> int64 { return 0; }, false);
	TestEqual(TEXT("Nothing owned: Fists only"), None[8], FGFHotbarRules::FistsId());
	TestTrue(TEXT("Nothing owned: slot 1 empty"), None[0].IsNone());

	const TArray<FName> Free = FGFHotbarRules::DefaultSlots(Blocks, [](FName) -> int64 { return 0; }, true);
	TestEqual(TEXT("Free build fills all nine slots"), Free.FilterByPredicate([](FName Id) { return !Id.IsNone(); }).Num(), 9);
	TestEqual(TEXT("Free build keeps table order"), Free[0], FName(TEXT("Dirt")));
	TestEqual(TEXT("Free build second is Stone"), Free[1], FName(TEXT("Stone")));
	TestEqual(TEXT("Free build third is WoodPlank, not Log"), Free[2], FName(TEXT("WoodPlank")));
	TestFalse(TEXT("The ninth distinct item does not push Fists out"), Free.Contains(FName(TEXT("CrystalVein"))));
	TestEqual(TEXT("Free build still ends in Fists"), Free[8], FGFHotbarRules::FistsId());

	const TArray<FName> Empty = FGFHotbarRules::DefaultSlots({}, [](FName) -> int64 { return 5; }, false);
	TestEqual(TEXT("No blocks: nine entries"), Empty.Num(), 9);
	TestEqual(TEXT("No blocks: Fists"), Empty[8], FGFHotbarRules::FistsId());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHotbarLabelsTest, "GFGame.HUD.Hotbar.Labels", GFHotbarTestDetail::Flags)
bool FGFHotbarLabelsTest::RunTest(const FString&)
{
	TestEqual(TEXT("No count is blank"), FGFHotbarRules::FormatCount(-1), FString());
	TestEqual(TEXT("Spent shows 0"), FGFHotbarRules::FormatCount(0), FString(TEXT("0")));
	TestEqual(TEXT("One"), FGFHotbarRules::FormatCount(1), FString(TEXT("1")));
	TestEqual(TEXT("999"), FGFHotbarRules::FormatCount(999), FString(TEXT("999")));
	TestEqual(TEXT("Cap"), FGFHotbarRules::FormatCount(1000), FString(TEXT("999+")));
	TestEqual(TEXT("Huge"), FGFHotbarRules::FormatCount(5000000000LL), FString(TEXT("999+")));

	TestEqual(TEXT("Two capitals"), FGFHotbarRules::ShortLabel(TEXT("WoodPlank")), FString(TEXT("WP")));
	TestEqual(TEXT("One capital"), FGFHotbarRules::ShortLabel(TEXT("Dirt")), FString(TEXT("Di")));
	TestEqual(TEXT("Skin"), FGFHotbarRules::ShortLabel(TEXT("Skin_Raptor")), FString(TEXT("SR")));
	TestEqual(TEXT("None"), FGFHotbarRules::ShortLabel(NAME_None), FString());

	TestEqual(TEXT("Icon path convention"), FGFHotbarRules::ConventionIconPath(TEXT("Glowgrub")),
		FString(TEXT("/Game/Art/Items/Textures/T_Icon_Glowgrub.T_Icon_Glowgrub")));
	TestEqual(TEXT("No icon path for None"), FGFHotbarRules::ConventionIconPath(NAME_None), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHotbarKeysTest, "GFGame.HUD.Hotbar.Keys", GFHotbarTestDetail::Flags)
bool FGFHotbarKeysTest::RunTest(const FString&)
{
	using namespace GFHotbarTestDetail;
	TArray<FGFMappedKey> Character;
	TArray<FGFMappedKey> Hud;
	FGFDefaultInputKeys::CharacterKeys(Character);
	FGFDefaultInputKeys::HudKeys(Hud, EKeys::Home);
	TArray<FGFMappedKey> All = Character;
	All.Append(Hud);

	const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 Index = 0; Index < 9; ++Index)
	{
		const FName Action(*FString::Printf(TEXT("Slot%d"), Index + 1));
		TestTrue(*FString::Printf(TEXT("%s is on its number key"), *Action.ToString()), HasKey(Hud, Action, Numbers[Index]));
		TestTrue(*FString::Printf(TEXT("Number key %d does nothing else"), Index + 1), FGFDefaultInputKeys::KeyIsOnly(All, Numbers[Index], Action));
	}
	TestTrue(TEXT("The wheel is still the hotbar"), HasKey(Character, TEXT("Hotbar"), EKeys::MouseWheelAxis));

	// The chip spells the ToggleElement keys out. If the key table changes, change the hint too.
	TestTrue(TEXT("Element swap is R"), HasKey(Character, TEXT("ToggleElement"), EKeys::R));
	TestTrue(TEXT("Element swap is D-pad Right"), HasKey(Character, TEXT("ToggleElement"), EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("Element key hint"), FString(GFHUD::ElementKeyHint()), FString(TEXT("R / D-pad Right")));
	TestTrue(TEXT("Controls hint names the number keys"), FString(GFHUD::ControlsHint()).Contains(TEXT("1-9 hotbar")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHotbarBandTest, "GFGame.HUD.Hotbar.ElementChip", GFHotbarTestDetail::Flags)
bool FGFHotbarBandTest::RunTest(const FString&)
{
	const FGFElementChip Rime = GFFormatElementChip(EGFMagicElement::Spark, EGFMagicElement::Frost);
	TestEqual(TEXT("Current element"), Rime.Label, FString(TEXT("Spark")));
	TestTrue(TEXT("Can swap with a faction"), Rime.bCanSwap);
	TestEqual(TEXT("Swap hint names the key and the target"), Rime.Hint, FString(TEXT("[R / D-pad Right] swap to Frost")));

	const FGFElementChip Back = GFFormatElementChip(EGFMagicElement::Magma, EGFMagicElement::Magma);
	TestEqual(TEXT("Faction element shown"), Back.Label, FString(TEXT("Magma")));
	TestEqual(TEXT("Swap back to Spark"), Back.Hint, FString(TEXT("[R / D-pad Right] swap to Spark")));

	const FGFElementChip Loner = GFFormatElementChip(EGFMagicElement::Spark, EGFMagicElement::Spark);
	TestFalse(TEXT("No faction: no swap"), Loner.bCanSwap);
	TestEqual(TEXT("No faction hint"), Loner.Hint, FString(TEXT("[R / D-pad Right] join a faction to swap")));
	TestTrue(TEXT("Each element has its own colour"), GFElementColor(EGFMagicElement::Frost) != GFElementColor(EGFMagicElement::Magma)
		&& GFElementColor(EGFMagicElement::Root) != GFElementColor(EGFMagicElement::Spark));

	const float Sizes[][2] = { { 1280.f, 720.f }, { 1920.f, 1080.f }, { 640.f, 360.f } };
	for (const auto& Size : Sizes)
	{
		const float W = Size[0];
		const float H = Size[1];
		const FString Tag = FString::Printf(TEXT("%.0fx%.0f"), W, H);
		const FGFHudRect Bounds = GFHudHotbarBounds(W, H);
		const FGFHotbarBandLayout Band = GFLayoutHotbarBand(W, H, 120.f, 300.f);
		const TArray<FGFHUDSlotRect> Row = GFLayoutHotbar(FGFHotbarRules::SlotCount, 0, W, H);
		TestFalse(*FString::Printf(TEXT("%s item chip placed"), *Tag), Band.Item.IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s element chip placed"), *Tag), Band.Element.IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s chips do not overlap"), *Tag), GFHudRectsOverlap(Band.Item, Band.Element));
		TestTrue(*FString::Printf(TEXT("%s chips inside the hotbar bounds"), *Tag), Band.Item.Y >= Bounds.Y - 0.05f && Band.Element.Bottom() <= Bounds.Bottom() + 0.05f);
		TestTrue(*FString::Printf(TEXT("%s chips inside the canvas"), *Tag), GFHudRectInside(Band.Item, W, H) && GFHudRectInside(Band.Element, W, H));
		TestTrue(*FString::Printf(TEXT("%s chips sit above the slots, with room for the selected lift"), *Tag), Band.Element.Bottom() + 7.f <= Row[0].Y + 0.05f);
		TestTrue(*FString::Printf(TEXT("%s pair is centred"), *Tag), FMath::IsNearlyEqual(Band.Item.X, W - Band.Element.Right(), 0.1f));
		TestTrue(*FString::Printf(TEXT("%s slots are square"), *Tag), FMath::IsNearlyEqual(Row[0].W, Row[0].H, 0.1f));
	}
	const FGFHotbarBandLayout Squeezed = GFLayoutHotbarBand(200.f, 300.f, 300.f, 300.f);
	TestTrue(TEXT("A narrow canvas squeezes the chips inside the margins"), Squeezed.Item.X >= GFHUD::HotbarSideMargin - 0.05f && Squeezed.Element.Right() <= 200.f - GFHUD::HotbarSideMargin + 0.05f);
	TestTrue(TEXT("No element: only the item chip"), GFLayoutHotbarBand(1280.f, 720.f, 100.f, 0.f).Element.IsEmpty());
	TestTrue(TEXT("Hint sits above the band"), GFHudHotbarTop(720.f) <= GFLayoutHotbarBand(1280.f, 720.f, 100.f, 100.f).Item.Y + 0.05f);
	return true;
}

#endif
