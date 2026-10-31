#include "Modules/ModuleManager.h"
#include "GFWindowCloseGuard.h"
#include "Misc/CoreDelegates.h"

class FGFGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// The game window and its scene viewport exist once the engine has initialised.
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([]() { FGFWindowCloseGuard::Install(); });
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	}

private:
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FGFGameModule, GFGame, "GFGame");
