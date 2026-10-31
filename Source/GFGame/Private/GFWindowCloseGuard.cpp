#include "GFWindowCloseGuard.h"
#include "GFLog.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Misc/ScopeExit.h"
#include "Widgets/SWindow.h"

bool FGFWindowCloseGuard::bClosing = false;
int32 FGFWindowCloseGuard::Dropped = 0;

bool FGFWindowCloseGuard::Install()
{
	UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
	const TSharedPtr<SWindow> Window = GameEngine ? GameEngine->GameViewportWindow.Pin() : nullptr;
	if (!Window.IsValid())
	{
		return false;
	}
	Window->SetOnWindowClosed(FOnWindowClosed::CreateStatic(&FGFWindowCloseGuard::OnGameWindowClosed));
	UE_LOG(LogGF, Log, TEXT("Window: close guard installed"));
	return true;
}

bool FGFWindowCloseGuard::HandleClose(TFunctionRef<bool()> HasViewport, TFunctionRef<void()> Forward)
{
	if (bClosing || !HasViewport())
	{
		++Dropped;
		return false;
	}
	bClosing = true;
	ON_SCOPE_EXIT
	{
		bClosing = false;
	};
	Forward();
	return true;
}

int32 FGFWindowCloseGuard::DroppedCloses()
{
	return Dropped;
}

void FGFWindowCloseGuard::OnGameWindowClosed(const TSharedRef<SWindow>& Window)
{
	UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
	if (!GameEngine)
	{
		return;
	}
	const bool bForwarded = HandleClose(
		[GameEngine]() { return GameEngine->SceneViewport.IsValid(); },
		[GameEngine, &Window]() { GameEngine->OnGameWindowClosed(Window); });
	if (!bForwarded)
	{
		UE_LOG(LogGF, Log, TEXT("Window: ignored a repeated close request (the game is already shutting down)"));
	}
}
