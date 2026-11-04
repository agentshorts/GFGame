#include "Modules/ModuleManager.h"
#include "GFMineable.h"

class FGFCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGFMineable::StartKitAudit();
	}

	virtual void ShutdownModule() override
	{
		FGFMineable::StopKitAudit();
	}
};

IMPLEMENT_MODULE(FGFCoreModule, GFCore);
