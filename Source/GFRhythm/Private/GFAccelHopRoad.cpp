#include "GFAccelHopRoad.h"
#include "GFAccelHop.h"
#include "GFBeatClockSubsystem.h"
#include "GFBuiltInSongs.h"
#include "GFLog.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Sound/SoundBase.h"

AGFAccelHopRoad::AGFAccelHopRoad()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	bReplicates = false;

	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	RootComponent = Bounds;
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetCanEverAffectNavigation(false);
	Bounds->SetBoxExtent(FVector(1000.f, 575.f, 400.f));
	Bounds->SetHiddenInGame(true);

	Outline = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Outline"));
	Outline->SetupAttachment(Bounds);
	Outline->bUnbound = true;
	Outline->Priority = 1.f;
}

void AGFAccelHopRoad::SetRoadExtent(FVector HalfExtent)
{
	Bounds->SetBoxExtent(HalfExtent.ComponentMax(FVector(50.f)));
}

bool AGFAccelHopRoad::BoxContains(const FTransform& BoxTransform, const FVector& HalfExtent, const FVector& Point)
{
	FTransform NoScale = BoxTransform;
	NoScale.SetScale3D(FVector::OneVector);
	const FVector Local = NoScale.InverseTransformPosition(Point);
	return FMath::Abs(Local.X) <= HalfExtent.X && FMath::Abs(Local.Y) <= HalfExtent.Y && FMath::Abs(Local.Z) <= HalfExtent.Z;
}

EGFBeatHopMode AGFAccelHopRoad::PickMode(bool bWantLive, int32 NumPlayers, bool bLiveStarted)
{
	return bWantLive && bLiveStarted && FGFBeatHopSessionRules::IsModeAllowed(EGFBeatHopMode::Live, NumPlayers) ? EGFBeatHopMode::Live : EGFBeatHopMode::Songs;
}

void AGFAccelHopRoad::BeginPlay()
{
	Super::BeginPlay();
	if (FGFAccelHopRules::AssetExists(FGFAccelHopRules::BeatCollectionPath))
	{
		BeatCollection = LoadObject<UMaterialParameterCollection>(nullptr, FGFAccelHopRules::BeatCollectionPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	UMaterialInterface* Ink = FGFAccelHopRules::AssetExists(FGFAccelHopRules::OutlineMaterialPath)
		? LoadObject<UMaterialInterface>(nullptr, FGFAccelHopRules::OutlineMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	if (Ink)
	{
		Outline->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, Ink));
	}
	else
	{
		UE_LOG(LogGF, Log, TEXT("Acceleration Hop: %s is not imported yet; tiles draw without the ink outline"), FGFAccelHopRules::OutlineMaterialPath);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false); // no local player, no music
	}
	UpdateCollection(0.f);
}

void AGFAccelHopRoad::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRoadMusic();
	Super::EndPlay(EndPlayReason);
}

UGFBeatClockSubsystem* AGFAccelHopRoad::Clock() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
}

bool AGFAccelHopRoad::FindLocalPawnLocation(FVector& Out) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController() && PC->GetPawn())
		{
			Out = PC->GetPawn()->GetActorLocation();
			return true;
		}
	}
	return false;
}

int32 AGFAccelHopRoad::PlayerCount() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? FMath::Max(1, GS->PlayerArray.Num()) : 1;
}

void AGFAccelHopRoad::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	FVector PawnLoc;
	bLocalInside = FindLocalPawnLocation(PawnLoc) && BoxContains(Bounds->GetComponentTransform(), Bounds->GetUnscaledBoxExtent(), PawnLoc);
	if (bLocalInside)
	{
		LastInsideTime = Now;
	}
	else
	{
		bStartFailed = false;
	}

	UGFBeatClockSubsystem* Beat = Clock();
	if (bLocalInside && !bStartFailed && Beat && !Beat->IsRunning())
	{
		StartRoadMusic();
	}
	else if (!bLocalInside && bOwnsClock && Now - LastInsideTime > LeaveGraceSeconds)
	{
		StopRoadMusic();
	}

	if (bOwnsClock && Beat && Beat->GetBeatSource() == EGFBeatSource::Song && SongSeconds > 0.f && Beat->GetSongTimeSec() >= SongSeconds)
	{
		// The song ended: start it again from beat 0 so the clock and the audio stay together.
		StopRoadMusic();
		if (bLocalInside)
		{
			StartRoadMusic();
		}
	}

	UpdateCollection(Beat && Beat->IsRunning() ? FGFAccelHopRules::BeatPulse(Beat->GetCurrentBeat()) : 0.f);
}

void AGFAccelHopRoad::StartRoadMusic()
{
	UGFBeatClockSubsystem* Beat = Clock();
	if (!Beat || Beat->IsRunning())
	{
		return; // another system (a Beat Hop run) already owns the clock: hop to its beat instead
	}
	const int32 Players = PlayerCount();
	const bool bLive = bListenToMyMusic && FGFBeatHopSessionRules::IsModeAllowed(EGFBeatHopMode::Live, Players) && Beat->StartLive();
	if (PickMode(bListenToMyMusic, Players, bLive) == EGFBeatHopMode::Live)
	{
		bOwnsClock = true;
		SongSeconds = 0.f;
		return;
	}
	const FName Song = FGFBeatHopMusic::PickSong(EGFBeatHopMode::Songs, SongId, false, false);
	if (!Beat->StartSong(Song, SongDifficulty))
	{
		UE_LOG(LogGF, Warning, TEXT("Acceleration Hop: song %s has no built-in chart; the road stays silent"), *Song.ToString());
		bStartFailed = true;
		return;
	}
	bOwnsClock = true;
	SongSeconds = 0.f;
	const FString Path = FGFBeatHopMusic::SoundPathFor(Song);
	USoundBase* Sound = Path.IsEmpty() ? nullptr : LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn);
	if (!Sound)
	{
		// Metronome-only song or missing audio: the clock still runs, so hops still judge on its beat.
		UE_LOG(LogGF, Log, TEXT("Acceleration Hop: no audio for %s; hopping to the beat map alone"), *Song.ToString());
		return;
	}
	SongSeconds = Sound->GetDuration();
	// Null without an audio device (-nosound): the clock keeps the beat anyway.
	Music = UGameplayStatics::CreateSound2D(this, Sound, MusicVolume, 1.f, 0.f, nullptr, false, false);
	if (Music)
	{
		Music->Play(0.f);
	}
}

void AGFAccelHopRoad::StopRoadMusic()
{
	if (Music)
	{
		Music->Stop();
		Music = nullptr;
	}
	if (bOwnsClock)
	{
		if (UGFBeatClockSubsystem* Beat = Clock())
		{
			Beat->StopClock();
		}
		bOwnsClock = false;
	}
	SongSeconds = 0.f;
}

void AGFAccelHopRoad::UpdateCollection(float Pulse)
{
	if (!BeatCollection)
	{
		return;
	}
	UKismetMaterialLibrary::SetScalarParameterValue(this, BeatCollection, TEXT("BeatPulse"), Pulse);
	// SunDir mirrors the first directional light (the cel bands follow the sun); straight down when there is none.
	UWorld* World = GetWorld();
	if (!SunLight.IsValid() && World && World->GetTimeSeconds() >= NextSunSearch)
	{
		// The environment director may spawn the sun after BeginPlay, so look again now and then.
		NextSunSearch = World->GetTimeSeconds() + 2.0;
		for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsRegistered())
			{
				SunLight = *It;
				break;
			}
		}
	}
	const FVector Sun = SunLight.IsValid() ? -SunLight->GetForwardVector() : FVector(0.f, 0.f, 1.f);
	UKismetMaterialLibrary::SetVectorParameterValue(this, BeatCollection, TEXT("SunDir"), FLinearColor(Sun.X, Sun.Y, Sun.Z, 0.f));
}
