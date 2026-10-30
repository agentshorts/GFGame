#include "GFZoneAmbienceDirector.h"

#include "GFZoneCatalog.h"
#include "GFZoneSubsystem.h"
#include "GFZoneTypes.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

AGFZoneAmbienceDirector::AGFZoneAmbienceDirector()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AGFZoneAmbienceDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer || Volume)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Volume = GetWorld()->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Params);
	if (!Volume)
	{
		return;
	}
	Volume->SetReplicates(false);
	Volume->bUnbound = true;
	Volume->bEnabled = true;
	Volume->Priority = 1.f;
	Volume->BlendWeight = 1.f;
	Apply(Current);
}

void AGFZoneAmbienceDirector::Apply(const FBlend& Value) const
{
	if (!Volume)
	{
		return;
	}
	FPostProcessSettings& Settings = Volume->Settings;
	Settings.bOverride_ColorSaturation = true;
	Settings.ColorSaturation.X = Value.Saturation;
	Settings.ColorSaturation.Y = Value.Saturation;
	Settings.ColorSaturation.Z = Value.Saturation;
	Settings.ColorSaturation.W = 1.f;
	Settings.bOverride_ColorGain = true;
	Settings.ColorGain.X = Value.Tint.R;
	Settings.ColorGain.Y = Value.Tint.G;
	Settings.ColorGain.Z = Value.Tint.B;
	Settings.ColorGain.W = 1.f;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = Value.Vignette;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = Value.Exposure;
}

void AGFZoneAmbienceDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Volume || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UGFZoneSubsystem* Zones = World ? World->GetSubsystem<UGFZoneSubsystem>() : nullptr;
	if (!Pawn || !Zones)
	{
		return;
	}
	const FGFZoneHit Hit = Zones->ResolveAt(Pawn->GetActorLocation());
	const FGFZoneAmbienceRow* Row = FGFZoneCatalog::Get().FindAmbience(Hit.AmbienceId);
	if (!Row)
	{
		return;
	}
	if (!bHasTarget || TargetId != Row->Id)
	{
		From = Current;
		TargetId = Row->Id;
		TargetBlendSeconds = Row->BlendTime();
		BlendElapsed = 0.f;
		bHasTarget = true;
	}
	BlendElapsed += DeltaSeconds;
	const float Alpha = TargetBlendSeconds <= 0.f ? 1.f : FMath::Clamp(BlendElapsed / TargetBlendSeconds, 0.f, 1.f);
	Current.Saturation = FMath::Lerp(From.Saturation, Row->Saturation, Alpha);
	Current.Tint = FMath::Lerp(From.Tint, Row->ContrastTint, Alpha);
	Current.Vignette = FMath::Lerp(From.Vignette, Row->Vignette, Alpha);
	Current.Exposure = FMath::Lerp(From.Exposure, Row->ClampedExposure(), Alpha);
	Apply(Current);
}
