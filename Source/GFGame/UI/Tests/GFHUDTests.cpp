// Canvas HUD formatting, and the game mode's HUD class on a controller in a game world.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.HUD;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFGameMode.h"
#include "GFHUD.h"
#include "GFHUDFormat.h"
#include "GFPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HUDFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool HudNear(float A, float B)
	{
		return FMath::IsNearlyEqual(A, B, 0.05f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHUDZoneLabelTest, "GFGame.HUD.ZoneLabel", HUDFlags)
bool FGFHUDZoneLabelTest::RunTest(const FString&)
{
	TestEqual(TEXT("Safe faction"), GFFormatZoneLabel(EGFTerritoryZone::Faction, TEXT("Rime"), true, false, NAME_None), FString(TEXT("Rime \u00B7 safe")));
	TestEqual(TEXT("Unsafe faction"), GFFormatZoneLabel(EGFTerritoryZone::Faction, TEXT("Rime"), false, false, NAME_None), FString(TEXT("Rime \u00B7 unsafe")));
	TestEqual(TEXT("Faction ignores a hazard"), GFFormatZoneLabel(EGFTerritoryZone::Faction, TEXT("Verdant"), true, true, TEXT("LavaBridge")), FString(TEXT("Verdant \u00B7 safe")));
	TestEqual(TEXT("Nameless faction"), GFFormatZoneLabel(EGFTerritoryZone::Faction, NAME_None, false, false, NAME_None), FString(TEXT("Land \u00B7 unsafe")));
	TestEqual(TEXT("Wild hazard"), GFFormatZoneLabel(EGFTerritoryZone::Wild, NAME_None, false, true, TEXT("LavaBridge")), FString(TEXT("Wild \u00B7 LavaBridge")));
	TestEqual(TEXT("Wild clear"), GFFormatZoneLabel(EGFTerritoryZone::Wild, NAME_None, false, false, NAME_None), FString(TEXT("Wild")));
	TestEqual(TEXT("Wild hazard flag without a name"), GFFormatZoneLabel(EGFTerritoryZone::Wild, NAME_None, false, true, NAME_None), FString(TEXT("Wild")));
	TestEqual(TEXT("Neutral"), GFFormatZoneLabel(EGFTerritoryZone::Neutral, NAME_None, true, true, TEXT("LavaBridge")), FString(TEXT("Neutral")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHUDChatFadeTest, "GFGame.HUD.ChatFade", HUDFlags)
bool FGFHUDChatFadeTest::RunTest(const FString&)
{
	TestEqual(TEXT("Hold is 15s"), GFHUD::ChatHoldSeconds, 15.f);
	TestEqual(TEXT("Fade is 1.5s"), GFHUD::ChatFadeSeconds, 1.5f);
	TestEqual(TEXT("Fresh"), GFChatLineAlpha(0.f), 1.f);
	TestEqual(TEXT("Negative age stays up"), GFChatLineAlpha(-2.f), 1.f);
	TestEqual(TEXT("Still full at 15s"), GFChatLineAlpha(15.f), 1.f);
	TestTrue(TEXT("Half faded"), FMath::IsNearlyEqual(GFChatLineAlpha(15.f + 0.75f), 0.5f, 0.001f));
	TestTrue(TEXT("Fading"), GFChatLineAlpha(15.2f) < GFChatLineAlpha(15.f) && GFChatLineAlpha(15.2f) > GFChatLineAlpha(16.f));
	TestEqual(TEXT("Gone at the end of the fade"), GFChatLineAlpha(16.5f), 0.f);
	TestEqual(TEXT("Stays gone"), GFChatLineAlpha(40.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHUDHotbarLayoutTest, "GFGame.HUD.HotbarLayout", HUDFlags)
bool FGFHUDHotbarLayoutTest::RunTest(const FString&)
{
	TestEqual(TEXT("No slots"), GFLayoutHotbar(0, 0, 400.f, 300.f).Num(), 0);
	TestEqual(TEXT("Negative count"), GFLayoutHotbar(-3, 0, 400.f, 300.f).Num(), 0);
	TestEqual(TEXT("No canvas"), GFLayoutHotbar(2, 0, 0.f, 300.f).Num(), 0);

	const TArray<FGFHUDSlotRect> Row = GFLayoutHotbar(3, 1, 400.f, 300.f);
	if (!TestEqual(TEXT("Three slots"), Row.Num(), 3))
	{
		return false;
	}
	TestFalse(TEXT("First unselected"), Row[0].bSelected);
	TestTrue(TEXT("Middle selected"), Row[1].bSelected);
	TestFalse(TEXT("Last unselected"), Row[2].bSelected);
	TestTrue(TEXT("Preferred width"), HudNear(Row[0].W, GFHUD::HotbarSlotW));
	TestTrue(TEXT("Preferred height"), HudNear(Row[0].H, GFHUD::HotbarSlotH));
	TestTrue(TEXT("Shared row"), HudNear(Row[0].Y, Row[1].Y) && HudNear(Row[1].Y, Row[2].Y));
	TestTrue(TEXT("Gap"), HudNear(Row[1].X, Row[0].X + Row[0].W + GFHUD::HotbarGap));
	TestTrue(TEXT("No overlap"), Row[0].X + Row[0].W <= Row[1].X + 0.01f && Row[1].X + Row[1].W <= Row[2].X + 0.01f);
	const float Left = Row[0].X;
	const float RightGap = 400.f - (Row[2].X + Row[2].W);
	TestTrue(TEXT("Centred"), HudNear(Left, RightGap));
	TestTrue(TEXT("Above the bottom edge"), Row[0].Y + Row[0].H <= 300.f);
	TestTrue(TEXT("Sits on the bottom margin"), HudNear(Row[0].Y, 300.f - GFHUD::HotbarBottomMargin - GFHUD::HotbarSlotH));

	const TArray<FGFHUDSlotRect> None = GFLayoutHotbar(2, INDEX_NONE, 800.f, 600.f);
	TestEqual(TEXT("Two wide slots"), None.Num(), 2);
	TestFalse(TEXT("No selection"), None[0].bSelected || None[1].bSelected);

	const TArray<FGFHUDSlotRect> One = GFLayoutHotbar(1, 0, 200.f, 100.f);
	TestEqual(TEXT("One slot"), One.Num(), 1);
	TestTrue(TEXT("Only slot selected"), One[0].bSelected);
	TestTrue(TEXT("Single slot centred"), HudNear(One[0].X + One[0].W * 0.5f, 100.f));

	const TArray<FGFHUDSlotRect> Many = GFLayoutHotbar(20, 3, 400.f, 300.f);
	if (!TestEqual(TEXT("Twenty slots"), Many.Num(), 20))
	{
		return false;
	}
	TestTrue(TEXT("Third selected"), Many[3].bSelected);
	TestFalse(TEXT("First of many unselected"), Many[0].bSelected);
	TestTrue(TEXT("Shrunk to the side margin"), Many[0].X >= GFHUD::HotbarSideMargin - 0.05f);
	TestTrue(TEXT("Right side margin"), Many.Last().X + Many.Last().W <= 400.f - GFHUD::HotbarSideMargin + 0.05f);
	TestTrue(TEXT("Still one row"), HudNear(Many[0].Y, Many.Last().Y));
	TestTrue(TEXT("Narrower than the preferred slot"), Many[0].W < GFHUD::HotbarSlotW);
	TestTrue(TEXT("Packed left to right"), Many[1].X >= Many[0].X + Many[0].W - 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHUDHintAndColorTest, "GFGame.HUD.HintAndColor", HUDFlags)
bool FGFHUDHintAndColorTest::RunTest(const FString&)
{
	TestEqual(TEXT("Hint waits 30s"), GFHUD::HintAutoHideSeconds, 30.f);
	TestEqual(TEXT("Hint copy"), FString(GFHUD::ControlsHint()), FString(TEXT("WASD move, Shift sprint, Ctrl dash, Q swing, E yank, Space jump, LMB dig, RMB place or aim, wheel or 1-9 hotbar, Tab/I bag, Alt+wheel zoom, MMB shoulder, V view, Enter chat, Esc menu, Home return")));
	TestTrue(TEXT("Shown at the start"), GFHintVisible(0.f, false, false));
	TestTrue(TEXT("Shown just before 30s"), GFHintVisible(29.9f, false, false));
	TestFalse(TEXT("Hidden at 30s"), GFHintVisible(30.f, false, true));
	TestTrue(TEXT("H shows it after the timer"), GFHintVisible(45.f, true, true));
	TestFalse(TEXT("H hides it early"), GFHintVisible(1.f, true, false));

	TestEqual(TEXT("Ten name colours"), GFHUD::PlayerColorCount, 10);
	TestTrue(TEXT("First two colours differ"), GFPlayerColor(0) != GFPlayerColor(1));
	TestTrue(TEXT("Tenth wraps to the first"), GFPlayerColor(10) == GFPlayerColor(0));
	TestTrue(TEXT("Eleventh wraps to the second"), GFPlayerColor(11) == GFPlayerColor(1));
	TestTrue(TEXT("No player state is white"), GFPlayerColor(INDEX_NONE) == FLinearColor::White);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHUDGameWorldTest, "GFGame.HUD.ControllerClass", HUDFlags)
bool FGFHUDGameWorldTest::RunTest(const FString&)
{
	const AGFGameMode* Defaults = GetDefault<AGFGameMode>();
	if (!TestTrue(TEXT("Game mode HUD class"), Defaults && Defaults->HUDClass == AGFHUD::StaticClass()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFHUDWorld"));
	if (!TestNotNull(TEXT("World created"), World))
	{
		return false;
	}
	struct FGuard
	{
		UWorld* World = nullptr;
		~FGuard()
		{
			if (!World)
			{
				return;
			}
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->DestroyWorld(false);
		}
	} Guard;
	Guard.World = World;

	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	// A bare world has no game instance, so UWorld::SetGameMode would dereference null. Spawn the mode directly.
	FActorSpawnParameters ModeParams;
	ModeParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFGameMode* Mode = World->SpawnActor<AGFGameMode>(AGFGameMode::StaticClass(), FTransform::Identity, ModeParams);
	if (!TestNotNull(TEXT("Hub game mode"), Mode))
	{
		return false;
	}
	TestTrue(TEXT("Live HUD class"), Mode->HUDClass == AGFHUD::StaticClass());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerController* Controller = World->SpawnActor<AGFPlayerController>(AGFPlayerController::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Controller"), Controller))
	{
		return false;
	}
	// A bare world has no net driver, so the Client RPC would not run. This is the body that RPC runs on a local player.
	Controller->ClientSetHUD_Implementation(Mode->HUDClass);
	const AHUD* HUD = Controller->GetHUD();
	TestTrue(TEXT("Controller HUD is AGFHUD"), HUD && HUD->IsA(AGFHUD::StaticClass()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHUDMiningReadoutTest, "GFGame.HUD.MiningReadout", HUDFlags)
bool FGFHUDMiningReadoutTest::RunTest(const FString&)
{
	const FGFMiningHudView Magma = GFFormatMiningHud(EGFMagicElement::Magma, 37.5f, 1, false, 0.4f);
	TestTrue(TEXT("Element binds"), Magma.Element == EGFMagicElement::Magma);
	TestEqual(TEXT("Element label"), Magma.ElementLabel, FString(TEXT("Magma")));
	TestTrue(TEXT("Element colour"), Magma.Color == GFElementColor(EGFMagicElement::Magma));
	TestEqual(TEXT("Crystal pool binds"), Magma.CrystalPool, 37.5f);
	TestEqual(TEXT("Crystal label"), Magma.CrystalLabel, FString(TEXT("Crystal 37.5")));
	TestEqual(TEXT("Claw charges bind"), Magma.ClawCharges, 1);
	TestEqual(TEXT("Claw label"), Magma.ClawLabel, FString(TEXT("Claw 1")));
	TestFalse(TEXT("Inactive target hides the bar"), Magma.bIntegrityVisible);
	TestEqual(TEXT("Hidden bar drops the fraction"), Magma.IntegrityFraction, 0.f);

	const FGFMiningHudView Full = GFFormatMiningHud(EGFMagicElement::Spark, 100.f, 2, true, 1.4f);
	TestEqual(TEXT("Spark label"), Full.ElementLabel, FString(TEXT("Spark")));
	TestEqual(TEXT("Full pool label"), Full.CrystalLabel, FString(TEXT("Crystal 100")));
	TestEqual(TEXT("Two charges"), Full.ClawCharges, 2);
	TestEqual(TEXT("Two charges label"), Full.ClawLabel, FString(TEXT("Claw 2")));
	TestTrue(TEXT("Active target shows the bar"), Full.bIntegrityVisible);
	TestEqual(TEXT("Fraction above 1 clamps"), Full.IntegrityFraction, 1.f);

	const FGFMiningHudView Spent = GFFormatMiningHud(EGFMagicElement::Root, 0.f, 0, true, -0.25f);
	TestEqual(TEXT("Root label"), Spent.ElementLabel, FString(TEXT("Root")));
	TestEqual(TEXT("Empty pool"), Spent.CrystalPool, 0.f);
	TestEqual(TEXT("Empty pool label"), Spent.CrystalLabel, FString(TEXT("Crystal 0")));
	TestEqual(TEXT("No charges"), Spent.ClawCharges, 0);
	TestEqual(TEXT("No charges label"), Spent.ClawLabel, FString(TEXT("Claw 0")));
	TestTrue(TEXT("A spent target still shows the bar"), Spent.bIntegrityVisible);
	TestEqual(TEXT("Fraction below 0 clamps"), Spent.IntegrityFraction, 0.f);

	const FGFMiningHudView Frost = GFFormatMiningHud(EGFMagicElement::Frost, 12.f, 2, true, 0.5f);
	TestEqual(TEXT("Frost label"), Frost.ElementLabel, FString(TEXT("Frost")));
	TestTrue(TEXT("Frost colour"), Frost.Color == GFElementColor(EGFMagicElement::Frost));
	TestEqual(TEXT("Mid pool label"), Frost.CrystalLabel, FString(TEXT("Crystal 12")));
	TestEqual(TEXT("Half fraction stays"), Frost.IntegrityFraction, 0.5f);

	const FGFMiningHudLayout Hidden = GFLayoutMiningHud(1280.f, 720.f, false);
	TestFalse(TEXT("Readout is placed while the bar is hidden"), Hidden.Readout.IsEmpty());
	TestTrue(TEXT("Hidden bar has no rect"), Hidden.Integrity.IsEmpty());
	const FGFMiningHudLayout NoCanvas = GFLayoutMiningHud(0.f, 720.f, true);
	TestTrue(TEXT("No canvas drops the readout"), NoCanvas.Readout.IsEmpty());
	TestTrue(TEXT("No canvas drops the bar"), NoCanvas.Integrity.IsEmpty());

	const float Sizes[][2] = { { 1280.f, 720.f }, { 1920.f, 1080.f }, { 640.f, 360.f } };
	for (const auto& Size : Sizes)
	{
		const float W = Size[0];
		const float H = Size[1];
		const FString Tag = FString::Printf(TEXT("%.0fx%.0f"), W, H);
		const FGFMiningHudLayout Layout = GFLayoutMiningHud(W, H, true);
		const FGFHudRect Stamina = GFHudStaminaBounds(W, H);
		const FGFHudRect Cross = GFHudCrosshairBounds(W, H);
		const FGFHudRect Hotbar = GFHudHotbarBounds(W, H);
		const FGFHudRect Interact = GFLayoutFieldHud(W, H, 0, false, true).Interact;
		TestFalse(*FString::Printf(TEXT("%s readout placed"), *Tag), Layout.Readout.IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s bar placed"), *Tag), Layout.Integrity.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s readout inside"), *Tag), GFHudRectInside(Layout.Readout, W, H));
		TestTrue(*FString::Printf(TEXT("%s bar inside"), *Tag), GFHudRectInside(Layout.Integrity, W, H));
		TestTrue(*FString::Printf(TEXT("%s readout is beside the stamina arc"), *Tag), Layout.Readout.X >= Stamina.Right() + GFHUD::MiningReadoutGap - 0.05f);
		TestTrue(*FString::Printf(TEXT("%s bar is under the crosshair"), *Tag), Layout.Integrity.Y >= Cross.Bottom() + GFHUD::IntegrityBarGap - 0.05f);
		TestTrue(*FString::Printf(TEXT("%s bar stays above the interact prompt"), *Tag), Layout.Integrity.Bottom() <= H * 0.5f + GFHUD::InteractBelowCenter - 0.05f);
		TestTrue(*FString::Printf(TEXT("%s bar is centred"), *Tag), HudNear(Layout.Integrity.X + Layout.Integrity.W * 0.5f, W * 0.5f));
		TestFalse(*FString::Printf(TEXT("%s readout misses the stamina arc"), *Tag), GFHudRectsOverlap(Layout.Readout, Stamina));
		TestFalse(*FString::Printf(TEXT("%s readout misses the crosshair"), *Tag), GFHudRectsOverlap(Layout.Readout, Cross));
		TestFalse(*FString::Printf(TEXT("%s readout misses the bar"), *Tag), GFHudRectsOverlap(Layout.Readout, Layout.Integrity));
		TestFalse(*FString::Printf(TEXT("%s readout misses the hotbar"), *Tag), GFHudRectsOverlap(Layout.Readout, Hotbar));
		TestFalse(*FString::Printf(TEXT("%s readout misses the prompt"), *Tag), GFHudRectsOverlap(Layout.Readout, Interact));
		TestFalse(*FString::Printf(TEXT("%s bar misses the crosshair"), *Tag), GFHudRectsOverlap(Layout.Integrity, Cross));
		TestFalse(*FString::Printf(TEXT("%s bar misses the stamina arc"), *Tag), GFHudRectsOverlap(Layout.Integrity, Stamina));
		TestFalse(*FString::Printf(TEXT("%s bar misses the prompt"), *Tag), GFHudRectsOverlap(Layout.Integrity, Interact));
		TestFalse(*FString::Printf(TEXT("%s bar misses the hotbar"), *Tag), GFHudRectsOverlap(Layout.Integrity, Hotbar));
	}
	return true;
}

#endif
