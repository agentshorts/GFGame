#include "Modules/ModuleManager.h"
#include "GFBeatHopMantle.h"
#include "GFBeatHopNinja.h"
#include "GFMovementTuning.h"
#include "GFWindowCloseGuard.h"
#include "Misc/CoreDelegates.h"

class FGFGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		GFBeatHopNinja::SetProfileProvider([]()
		{
			const UGFMovementTuning* Tuning = GetDefault<UGFMovementTuning>();
			return Tuning ? Tuning->BeatHop : FGFBeatHopTraversalProfile();
		});
		GFBeatHopNinja::SetMantleProbe([]()
		{
			return GFBeatHopMantle::IsImplemented();
		});
		// The game window and its scene viewport exist once the engine has initialised.
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([]() { FGFWindowCloseGuard::Install(); });
	}

	virtual void ShutdownModule() override
	{
		GFBeatHopNinja::SetProfileProvider(nullptr);
		GFBeatHopNinja::SetMantleProbe(nullptr);
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	}

private:
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FGFGameModule, GFGame, "GFGame");
