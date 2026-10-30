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
	ComponentClass* AddComponent(AActor* Owner)
	{
		ComponentClass* Comp = NewObject<ComponentClass>(Owner);
		Comp->SetupAttachment(Owner->GetRootComponent());
		Owner->AddInstanceComponent(Comp);
		return Comp;
	}
}

AGFEnvironmentDirector::AGFEnvironmentDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Config defaults live on the CDO. Instances keep the ini values copied from it.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		SunIntensity = 10.f;
		ExposureBias = 11.f;
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

	if (!WorldHas<UDirectionalLightComponent>(World))
	{
		const FRotator SunRotation(-48.f, 25.f, 0.f);
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(SunRotation), Params);
		if (UDirectionalLightComponent* Light = Sun ? Cast<UDirectionalLightComponent>(Sun->GetLightComponent()) : nullptr)
		{
			Light->SetAtmosphereSunLight(true);
			Light->SetIntensity(SunIntensity);
			bAddedSun = true;

			// Only when we own the sun. A hand-lit map keeps its own exposure.
			// Auto exposure is off, so 10 lux needs this bias or the empty map stays black.
			UPostProcessComponent* Exposure = AddComponent<UPostProcessComponent>(this);
			Exposure->bUnbound = true;
			Exposure->Priority = 1.f;
			Exposure->BlendWeight = 1.f;
			Exposure->Settings.bOverride_AutoExposureBias = true;
			Exposure->Settings.AutoExposureBias = ExposureBias;
			Exposure->RegisterComponent();
		}
	}

	if (!WorldHas<USkyAtmosphereComponent>(World))
	{
		USkyAtmosphereComponent* Atmosphere = AddComponent<USkyAtmosphereComponent>(this);
		Atmosphere->RegisterComponent();
		bAddedSky = true;
	}

	if (!WorldHas<USkyLightComponent>(World))
	{
		USkyLightComponent* SkyLight = AddComponent<USkyLightComponent>(this);
		SkyLight->SetMobility(EComponentMobility::Movable);
		SkyLight->SourceType = SLS_CapturedScene;
		SkyLight->bRealTimeCapture = true;
		SkyLight->RegisterComponent();
		if (World->Scene)
		{
			SkyLight->RecaptureSky();
		}
		bAddedSkyLight = true;
	}

	if (!WorldHas<UExponentialHeightFogComponent>(World))
	{
		UExponentialHeightFogComponent* Fog = AddComponent<UExponentialHeightFogComponent>(this);
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
}
