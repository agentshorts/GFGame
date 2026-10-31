#include "Modules/ModuleManager.h"
#include "GFMagicTags.h"

class FGFMagicModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGFMagicTags::Anchor();
	}
};

IMPLEMENT_MODULE(FGFMagicModule, GFMagic);
