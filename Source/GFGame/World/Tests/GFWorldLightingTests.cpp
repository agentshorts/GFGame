// Runtime sun and sky light must be Movable, and the fixed exposure (bias 0)
// must sit inside the Lumen cached-lighting window. No map, no bake.

#include "Misc/AutomationTest.h"
#include "GFEnvironmentDirector.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFWorldLightingTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FEnvWorld
	{
		UWorld* World = nullptr;

		FEnvWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFEnvLightWorld"));
			if (!World)
			{
				return;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FEnvWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	struct FLogCapture : FOutputDevice
	{
		FString Text;

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			Text += V;
			Text += LINE_TERMINATOR;
		}
	};

	struct FLogScope
	{
		FLogCapture Capture;

		FLogScope()
		{
			if (GLog)
			{
				GLog->AddOutputDevice(&Capture);
			}
		}

		~FLogScope()
		{
			if (GLog)
			{
				GLog->RemoveOutputDevice(&Capture);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLumenExposureWindowTest, "GFGame.World.LumenExposureWindow", GFWorldLightingTests::Flags)
bool FGFLumenExposureWindowTest::RunTest(const FString&)
{
	// Bias +11 was the white screen, and the viewport printed it as Exposure -11.
	// Default offset 4 only covers [-8, 12], so -11 was clipped. Bias 0 is Exposure 0.
	TestTrue(TEXT("Bias 11 prints as Exposure -11"), FMath::IsNearlyEqual(GFFixedExposureEV(11.f), -11.f));
	TestFalse(TEXT("Exposure -11 is outside the engine default window"), GFLumenCachedLightingCovers(4.f, GFFixedExposureEV(11.f)));
	TestTrue(TEXT("Default window is [-8, 12]"),
		FMath::IsNearlyEqual(GFLumenCachedLightingMinEV(4.f), -8.f) && FMath::IsNearlyEqual(GFLumenCachedLightingMaxEV(4.f), 12.f));

	TestTrue(TEXT("Bias 0 prints as Exposure 0"), FMath::IsNearlyEqual(GFFixedExposureEV(0.f), 0.f));
	TestTrue(TEXT("Exposure 0 is inside offset 0"), GFLumenCachedLightingCovers(0.f, GFFixedExposureEV(0.f)));
	TestTrue(TEXT("Offset 0 window is [-12, 8]"),
		FMath::IsNearlyEqual(GFLumenCachedLightingMinEV(0.f), -12.f) && FMath::IsNearlyEqual(GFLumenCachedLightingMaxEV(0.f), 8.f));

	const FString EngineIni = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini"));
	FString Ini;
	if (!TestTrue(TEXT("DefaultEngine.ini readable"), FFileHelper::LoadFileToString(Ini, *EngineIni)))
	{
		return false;
	}
	TestTrue(TEXT("Static lighting is off"), Ini.Contains(TEXT("r.AllowStaticLighting=False")));
	TestTrue(TEXT("Cached lighting pre-exposure is 0"), Ini.Contains(TEXT("r.EyeAdaptation.CachedLightingPreExposure=0")));
	TestTrue(TEXT("Auto exposure stays off"), Ini.Contains(TEXT("r.DefaultFeature.AutoExposure=False")));

	const AGFEnvironmentDirector* Defaults = GetDefault<AGFEnvironmentDirector>();
	if (TestNotNull(TEXT("Director defaults"), Defaults))
	{
		const float ExposureEV = GFFixedExposureEV(Defaults->ExposureBias);
		TestTrue(TEXT("Shipped bias is still about 0"), FMath::IsNearlyZero(Defaults->ExposureBias, 0.05f));
		TestTrue(TEXT("Shipped exposure sits in the offset-0 window"), GFLumenCachedLightingCovers(0.f, ExposureEV));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRuntimeLightsAreMovableTest, "GFGame.World.RuntimeLightsAreMovable", GFWorldLightingTests::Flags)
bool FGFRuntimeLightsAreMovableTest::RunTest(const FString&)
{
	using namespace GFWorldLightingTests;

	FEnvWorld Env;
	if (!TestNotNull(TEXT("World created"), Env.World))
	{
		return false;
	}

	FLogScope Log;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFEnvironmentDirector* Director = Env.World->SpawnActor<AGFEnvironmentDirector>(AGFEnvironmentDirector::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Director"), Director))
	{
		return false;
	}

	TestTrue(TEXT("Director root is movable"), Director->GetRootComponent() && Director->GetRootComponent()->Mobility == EComponentMobility::Movable);
	TestTrue(TEXT("Shipped exposure bias stays about 0"), FMath::IsNearlyZero(Director->ExposureBias, 0.05f));

	int32 Lights = 0;
	UDirectionalLightComponent* Sun = nullptr;
	USkyLightComponent* Sky = nullptr;
	for (TActorIterator<AActor> It(Env.World); It; ++It)
	{
		TArray<ULightComponent*> Found;
		It->GetComponents(Found);
		for (ULightComponent* Light : Found)
		{
			++Lights;
			TestEqual(TEXT("Runtime light is movable"), Light->Mobility, EComponentMobility::Movable);
			if (!Sun)
			{
				Sun = Cast<UDirectionalLightComponent>(Light);
			}
			if (!Sky)
			{
				Sky = Cast<USkyLightComponent>(Light);
			}
		}
	}

	TestTrue(TEXT("Spawned a sun"), Sun != nullptr);
	TestTrue(TEXT("Spawned a sky light"), Sky != nullptr);
	TestEqual(TEXT("Sun and sky light, nothing else"), Lights, 2);
	if (Sun)
	{
		TestEqual(TEXT("Sun mobility"), Sun->Mobility, EComponentMobility::Movable);
		TestTrue(TEXT("Sun is the atmosphere light"), Sun->bAtmosphereSunLight != 0);
	}

	UPostProcessComponent* Exposure = Director->FindComponentByClass<UPostProcessComponent>();
	if (TestNotNull(TEXT("Exposure post process"), Exposure))
	{
		TestTrue(TEXT("Bias override is on"), Exposure->Settings.bOverride_AutoExposureBias);
		TestTrue(TEXT("Applied bias matches the director"), FMath::IsNearlyEqual(Exposure->Settings.AutoExposureBias, Director->ExposureBias));
	}

	const float ExposureEV = GFFixedExposureEV(Director->ExposureBias);
	if (IConsoleVariable* Offset = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptation.CachedLightingPreExposure")))
	{
		TestTrue(TEXT("Pre-exposure cvar matches the fixed exposure"), FMath::IsNearlyEqual(Offset->GetFloat(), ExposureEV));
		TestTrue(TEXT("That exposure is inside the window"), GFLumenCachedLightingCovers(Offset->GetFloat(), ExposureEV));
	}

	TestTrue(TEXT("Log says the sun is movable"), Log.Capture.Text.Contains(TEXT("sun=yes")));
	TestTrue(TEXT("Log says the sky light is movable"), Log.Capture.Text.Contains(TEXT("skylight=yes")));
	TestTrue(TEXT("Log prints Exposure 0"), Log.Capture.Text.Contains(TEXT("exposure=0.0")));
	TestTrue(TEXT("Log prints the offset-0 window"), Log.Capture.Text.Contains(TEXT("window=[-12.0, 8.0]")));

	Director->EnsureEnvironment();
	int32 LightsAfter = 0;
	for (TActorIterator<AActor> It(Env.World); It; ++It)
	{
		TArray<ULightComponent*> Found;
		It->GetComponents(Found);
		LightsAfter += Found.Num();
	}
	TestEqual(TEXT("Second pass adds no lights"), LightsAfter, 2);

	return true;
}

#endif
