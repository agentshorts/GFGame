#pragma once

// Guards the game window close path in standalone and packaged games.
// UGameEngine::OnGameWindowClosed resets SceneViewport and is not re-entrant. A held Alt-F4 queues two
// window closes, and the second can run inside the first one, so the outer call then hits
// SceneViewport.ToSharedRef() on a null pointer (IsValid() assert in SharedPointer.h). The guard replaces
// the window's close delegate with one that forwards to the engine once and drops any repeat.

#include "CoreMinimal.h"

class SWindow;

struct GFGAME_API FGFWindowCloseGuard
{
	/** Swaps the game window's OnWindowClosed for the guarded one. No-op in the editor or without a game window. */
	static bool Install();

	/** Delegate target: forwards to UGameEngine::OnGameWindowClosed at most once per close. */
	static void OnGameWindowClosed(const TSharedRef<SWindow>& Window);

	/**
	 * The decision on its own. HasViewport says whether the engine still owns a scene viewport, Forward runs the
	 * engine close. Returns true when Forward ran. A call made while Forward is running is dropped.
	 */
	static bool HandleClose(TFunctionRef<bool()> HasViewport, TFunctionRef<void()> Forward);

	/** How many closes were dropped since start-up (repeat or no viewport). For logs and tests. */
	static int32 DroppedCloses();

private:
	static bool bClosing;
	static int32 Dropped;
};
