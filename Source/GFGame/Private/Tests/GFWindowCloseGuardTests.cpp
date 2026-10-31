#include "GFWindowCloseGuard.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWindowCloseGuardTest, "GFGame.Window.CloseGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGFWindowCloseGuardTest::RunTest(const FString& Parameters)
{
	// One close with a viewport runs the engine close once.
	{
		int32 Calls = 0;
		const bool bRan = FGFWindowCloseGuard::HandleClose([]() { return true; }, [&Calls]() { ++Calls; });
		TestTrue(TEXT("A single close is forwarded"), bRan);
		TestEqual(TEXT("The engine close runs once"), Calls, 1);
	}

	// A held Alt-F4: the second close arrives while the first is still running. Only the first reaches the engine.
	{
		bool bHasViewport = true;
		int32 Calls = 0;
		bool bInnerRan = true;
		const int32 DroppedBefore = FGFWindowCloseGuard::DroppedCloses();
		const bool bOuterRan = FGFWindowCloseGuard::HandleClose(
			[&bHasViewport]() { return bHasViewport; },
			[&]()
			{
				++Calls;
				bInnerRan = FGFWindowCloseGuard::HandleClose([&bHasViewport]() { return bHasViewport; }, [&Calls]() { ++Calls; });
				bHasViewport = false; // the engine resets SceneViewport at the end of its close
			});
		TestTrue(TEXT("The outer close is forwarded"), bOuterRan);
		TestFalse(TEXT("The nested close is dropped"), bInnerRan);
		TestEqual(TEXT("The engine close runs once for two requests"), Calls, 1);
		TestEqual(TEXT("One close was dropped"), FGFWindowCloseGuard::DroppedCloses(), DroppedBefore + 1);

		// A late close after the viewport is gone is dropped instead of asserting.
		const bool bLateRan = FGFWindowCloseGuard::HandleClose([&bHasViewport]() { return bHasViewport; }, [&Calls]() { ++Calls; });
		TestFalse(TEXT("A close with no viewport is dropped"), bLateRan);
		TestEqual(TEXT("Still one engine close"), Calls, 1);
	}

	// The guard is released after a close, so a later window close works again.
	{
		int32 Calls = 0;
		TestTrue(TEXT("The guard resets after a close"), FGFWindowCloseGuard::HandleClose([]() { return true; }, [&Calls]() { ++Calls; }));
		TestEqual(TEXT("And forwards"), Calls, 1);
	}

	// In the editor there is no UGameEngine, so Install does nothing.
	TestFalse(TEXT("Install is a no-op without a game window"), FGFWindowCloseGuard::Install());
	return true;
}

#endif
