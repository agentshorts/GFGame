#include "Modules/ModuleManager.h"
#include "GFBeatHopCourse.h"
#include "GFBeatHopMantle.h"
#include "GFBeatHopMapGates.h"
#include "GFBeatHopMaps.h"
#include "GFBeatHopNinja.h"
#include "GFMovementTuning.h"
#include "GFStairMovement.h"
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
		GFInstallDinoStairHook();
		GFBeatHopMaps::SetLayoutHook([](AGFBeatHopCourse* Course)
		{
			GFBeatHopMapGates::Refresh(Course);
		});
		// The game window and its scene viewport exist once the engine has initialised.
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([]() { FGFWindowCloseGuard::Install(); });
	}

	virtual void ShutdownModule() override
	{
		GFBeatHopNinja::SetProfileProvider(nullptr);
		GFBeatHopNinja::SetMantleProbe(nullptr);
		GFBeatHopMaps::SetLayoutHook(nullptr);
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	}

private:
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FGFGameModule, GFGame, "GFGame");
