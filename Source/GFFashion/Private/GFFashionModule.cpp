#include "GFActivityWin.h"
#include "GFWardrobe.h"
#include "Modules/ModuleManager.h"

class FGFFashionModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		ActivityHandle = FGFActivityWin::OnActivity().AddLambda([](UWorld* World, const FString& PlayerKey, FName Activity, FName Context)
		{
			FGFWardrobe::HandleActivity(World, PlayerKey, Activity, Context);
		});
		CharacterHandle = FGFActivityWin::OnNewCharacter().AddLambda([](UWorld* World, const FString& PlayerKey)
		{
			FGFWardrobe::HandleNewCharacter(World, PlayerKey);
		});
	}

	virtual void ShutdownModule() override
	{
		if (ActivityHandle.IsValid())
		{
			FGFActivityWin::OnActivity().Remove(ActivityHandle);
			ActivityHandle.Reset();
		}
		if (CharacterHandle.IsValid())
		{
			FGFActivityWin::OnNewCharacter().Remove(CharacterHandle);
			CharacterHandle.Reset();
		}
	}

private:
	FDelegateHandle ActivityHandle;
	FDelegateHandle CharacterHandle;
};

IMPLEMENT_MODULE(FGFFashionModule, GFFashion);
