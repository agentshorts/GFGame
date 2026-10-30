#include "GFEnvironmentDirector.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GFLog.h"
#include "HAL/IConsoleManager.h"

namespace
{
	template <typename ComponentClass>
	bool WorldHas(UWorld* World)
	{
		if (!World)
		{
			return false;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->FindComponentByClass<ComponentClass>())
			{
				return true;
			}
		}
		return false;
	}

	template <typename ComponentClass>
	ComponentClass* AddEnvComponent(AActor* Owner)
	{
		ComponentClass* Comp = NewObject<ComponentClass>(Owner);
		// Movable before attach. A static child on this actor would keep the baker interested.
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetupAttachment(Owner->GetRootComponent());
		Owner->AddInstanceComponent(Comp);
		return Comp;
	}

	void MatchCachedLightingPreExposure(float ExposureBias)
	{
		// Slide the cached-lighting window onto the exposure we actually use.
		// The bias itself stays whatever DefaultGame.ini says (0 on staging).
		const float ExposureEV = GFNormalizedExposureEV(GFFixedExposureEV(ExposureBias));
		if (IConsoleVariable* Offset = IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptation.CachedLightingPreExposure")))
		{
			Offset->Set(ExposureEV, ECVF_SetByCode);
		}
	}

	const TCHAR* MobilityWord(const USceneComponent* Component)
	{
		if (!Component)
		{
			return TEXT("absent");
		}
		return Component->Mobility == EComponentMobility::Movable ? TEXT("yes") : TEXT("no");
	}
}

AGFEnvironmentDirector::AGFEnvironmentDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// The sun and sky light are Movable. Their parent has to be Movable too, or the engine refuses the change.
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	// Config defaults live on the CDO. Instances keep the ini values copied from it.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		SunIntensity = 10.f;
		ExposureBias = 0.f;
	}
}

void AGFEnvironmentDirector::BeginPlay()
{
	Super::BeginPlay();
	EnsureEnvironment();
}

void AGFEnvironmentDirector::EnsureEnvironment()
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}

	bool bAddedSun = false;
	bool bAddedSky = false;
	bool bAddedSkyLight = false;
	bool bAddedFog = false;
	UDirectionalLightComponent* SpawnedSun = nullptr;
	USkyLightComponent* SpawnedSky = nullptr;

	if (!WorldHas<UDirectionalLightComponent>(World))
	{
		const FRotator SunRotation(-48.f, 25.f, 0.f);
		const FTransform SunTransform(SunRotation);
		// Deferred so the light's first registration is Movable. ADirectionalLight is Stationary
		// out of the box, and a stationary sun plus static ground is the "needs to be rebuilt" prompt.
		ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(
			ADirectionalLight::StaticClass(),
			SunTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (UDirectionalLightComponent* Light = Sun ? Cast<UDirectionalLightComponent>(Sun->GetLightComponent()) : nullptr)
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetAtmosphereSunLight(true);
			Light->SetIntensity(SunIntensity);
		}
		if (Sun)
		{
			Sun->FinishSpawning(SunTransform);
		}
		if (UDirectionalLightComponent* Light = Sun ? Cast<UDirectionalLightComponent>(Sun->GetLightComponent()) : nullptr)
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetAtmosphereSunLight(true);
			Light->SetIntensity(SunIntensity);
			SpawnedSun = Light;
			bAddedSun = true;

			// Only when we own the sun. A hand-lit map keeps its own exposure.
			// Auto exposure is off, so the bias is fixed. 0 reads right at 10 lux in the packaged hub.
			UPostProcessComponent* Exposure = AddEnvComponent<UPostProcessComponent>(this);
			Exposure->bUnbound = true;
			Exposure->Priority = 1.f;
			Exposure->BlendWeight = 1.f;
			Exposure->Settings.bOverride_AutoExposureBias = true;
			Exposure->Settings.AutoExposureBias = ExposureBias;
			Exposure->RegisterComponent();
			MatchCachedLightingPreExposure(ExposureBias);
		}
	}

	if (!WorldHas<USkyAtmosphereComponent>(World))
	{
		USkyAtmosphereComponent* Atmosphere = AddEnvComponent<USkyAtmosphereComponent>(this);
		Atmosphere->RegisterComponent();
		bAddedSky = true;
	}

	if (!WorldHas<USkyLightComponent>(World))
	{
		USkyLightComponent* SkyLight = AddEnvComponent<USkyLightComponent>(this);
		SkyLight->SetMobility(EComponentMobility::Movable);
		SkyLight->SourceType = SLS_CapturedScene;
		SkyLight->bRealTimeCapture = true;
		SkyLight->RegisterComponent();
		SpawnedSky = SkyLight;
		if (World->Scene)
		{
			SkyLight->RecaptureSky();
		}
		bAddedSkyLight = true;
	}

	if (!WorldHas<UExponentialHeightFogComponent>(World))
	{
		UExponentialHeightFogComponent* Fog = AddEnvComponent<UExponentialHeightFogComponent>(this);
		// Light haze. The default 0.02 hides the other camps. Volumetric fog stays off (not cheap).
		Fog->FogDensity = 0.005f;
		Fog->bEnableVolumetricFog = false;
		Fog->RegisterComponent();
		bAddedFog = true;
	}

	// No volumetric cloud: the default layer is a tall raymarch, which is not cheap beside the voxel plot.

	if (bAddedSun || bAddedSky || bAddedSkyLight || bAddedFog)
	{
		UE_LOG(LogGF, Log, TEXT("World: environment director filled missing sky on this machine (sun=%d atmosphere=%d skylight=%d fog=%d). Lights stay local."),
			bAddedSun ? 1 : 0, bAddedSky ? 1 : 0, bAddedSkyLight ? 1 : 0, bAddedFog ? 1 : 0);
	}

	if (bAddedSun || bAddedSkyLight)
	{
		const float ExposureEV = GFNormalizedExposureEV(GFFixedExposureEV(ExposureBias));
		UE_LOG(LogGF, Log, TEXT("World: runtime lights are movable (sun=%s skylight=%s exposure=%.1f window=[%.1f, %.1f])."),
			MobilityWord(SpawnedSun), MobilityWord(SpawnedSky), ExposureEV,
			GFLumenCachedLightingMinEV(ExposureEV), GFLumenCachedLightingMaxEV(ExposureEV));
	}
}
