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

	bool Near(float A, float B)
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
	TestTrue(TEXT("Preferred width"), Near(Row[0].W, GFHUD::HotbarSlotW));
	TestTrue(TEXT("Preferred height"), Near(Row[0].H, GFHUD::HotbarSlotH));
	TestTrue(TEXT("Shared row"), Near(Row[0].Y, Row[1].Y) && Near(Row[1].Y, Row[2].Y));
	TestTrue(TEXT("Gap"), Near(Row[1].X, Row[0].X + Row[0].W + GFHUD::HotbarGap));
	TestTrue(TEXT("No overlap"), Row[0].X + Row[0].W <= Row[1].X + 0.01f && Row[1].X + Row[1].W <= Row[2].X + 0.01f);
	const float Left = Row[0].X;
	const float RightGap = 400.f - (Row[2].X + Row[2].W);
	TestTrue(TEXT("Centred"), Near(Left, RightGap));
	TestTrue(TEXT("Above the bottom edge"), Row[0].Y + Row[0].H <= 300.f);
	TestTrue(TEXT("Sits on the bottom margin"), Near(Row[0].Y, 300.f - GFHUD::HotbarBottomMargin - GFHUD::HotbarSlotH));

	const TArray<FGFHUDSlotRect> None = GFLayoutHotbar(2, INDEX_NONE, 800.f, 600.f);
	TestEqual(TEXT("Two wide slots"), None.Num(), 2);
	TestFalse(TEXT("No selection"), None[0].bSelected || None[1].bSelected);

	const TArray<FGFHUDSlotRect> One = GFLayoutHotbar(1, 0, 200.f, 100.f);
	TestEqual(TEXT("One slot"), One.Num(), 1);
	TestTrue(TEXT("Only slot selected"), One[0].bSelected);
	TestTrue(TEXT("Single slot centred"), Near(One[0].X + One[0].W * 0.5f, 100.f));

	const TArray<FGFHUDSlotRect> Many = GFLayoutHotbar(20, 3, 400.f, 300.f);
	if (!TestEqual(TEXT("Twenty slots"), Many.Num(), 20))
	{
		return false;
	}
	TestTrue(TEXT("Third selected"), Many[3].bSelected);
	TestFalse(TEXT("First of many unselected"), Many[0].bSelected);
	TestTrue(TEXT("Shrunk to the side margin"), Many[0].X >= GFHUD::HotbarSideMargin - 0.05f);
	TestTrue(TEXT("Right side margin"), Many.Last().X + Many.Last().W <= 400.f - GFHUD::HotbarSideMargin + 0.05f);
	TestTrue(TEXT("Still one row"), Near(Many[0].Y, Many.Last().Y));
	TestTrue(TEXT("Narrower than the preferred slot"), Many[0].W < GFHUD::HotbarSlotW);
	TestTrue(TEXT("Packed left to right"), Many[1].X >= Many[0].X + Many[0].W - 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHUDHintAndColorTest, "GFGame.HUD.HintAndColor", HUDFlags)
bool FGFHUDHintAndColorTest::RunTest(const FString&)
{
	TestEqual(TEXT("Hint waits 30s"), GFHUD::HintAutoHideSeconds, 30.f);
	TestEqual(TEXT("Hint copy"), FString(GFHUD::ControlsHint()), FString(TEXT("WASD move, Space jump, LMB dig, RMB place, wheel block, V view, Enter chat")));
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
	if (AWorldSettings* Settings = World->GetWorldSettings())
	{
		Settings->DefaultGameMode = AGFGameMode::StaticClass();
	}
	World->InitializeActorsForPlay(FURL());
	if (!World->GetAuthGameMode())
	{
		World->SetGameMode(FURL());
	}
	if (!World->HasBegunPlay())
	{
		World->BeginPlay();
	}

	AGFGameMode* Mode = World->GetAuthGameMode<AGFGameMode>();
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

#endif
