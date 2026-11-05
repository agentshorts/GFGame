#include "GFAccelHopRoad.h"
#include "GFAccelHop.h"
#include "GFAccelHopTile.h"
#include "GFBeatClockSubsystem.h"
#include "GFBuiltInSongs.h"
#include "GFHopMusicSubsystem.h"
#include "GFLiveAudio.h"
#include "GFLog.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Algo/Sort.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"

AGFAccelHopRoad::AGFAccelHopRoad()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	// v2 (#342): the lit target is the server's pick, so the road replicates. It is placed with the map on every
	// machine (stable name), and always relevant because the road is longer than the default cull distance.
	bReplicates = true;
	bAlwaysRelevant = true;

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

bool AGFAccelHopRoad::ContainsPoint(const FVector& Point) const
{
	return Bounds && BoxContains(Bounds->GetComponentTransform(), Bounds->GetUnscaledBoxExtent(), Point);
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
	// A dedicated server still ticks (v2): it has no music, but it picks the lit tile on the beat.
	RefreshTiles();
	UpdateCollection(0.f);
}

void AGFAccelHopRoad::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRoadMusic();
	if (AGFAccelHopTile* Shown = ShownLit.Get())
	{
		Shown->SetLitVisual(false, 0.f, LitTuning);
	}
	ShownLit = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AGFAccelHopRoad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFAccelHopRoad, LitState);
}

float AGFAccelHopRoad::AlongOf(const FVector& World) const
{
	FTransform NoScale = Bounds->GetComponentTransform();
	NoScale.SetScale3D(FVector::OneVector);
	return static_cast<float>(NoScale.InverseTransformPosition(World).X);
}

void AGFAccelHopRoad::RefreshTiles()
{
	for (AGFAccelHopTile* Old : Tiles)
	{
		if (Old && Old->GetRoad() == this)
		{
			Old->SetRoad(nullptr, INDEX_NONE);
		}
	}
	Tiles.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FTransform BoxTransform = Bounds->GetComponentTransform();
	const FVector Extent = Bounds->GetUnscaledBoxExtent();
	TArray<AGFAccelHopTile*> Found;
	for (TActorIterator<AGFAccelHopTile> It(World); It; ++It)
	{
		AGFAccelHopTile* Tile = *It;
		if (Tile && !Tile->IsActorBeingDestroyed() && BoxContains(BoxTransform, Extent, Tile->GetActorLocation()))
		{
			Found.Add(Tile);
		}
	}
	FTransform NoScale = BoxTransform;
	NoScale.SetScale3D(FVector::OneVector);
	Algo::Sort(Found, [&NoScale](const AGFAccelHopTile* A, const AGFAccelHopTile* B)
	{
		const FVector LA = NoScale.InverseTransformPosition(A->GetActorLocation());
		const FVector LB = NoScale.InverseTransformPosition(B->GetActorLocation());
		// Rounded to a millimetre so float noise between machines cannot reorder two tiles.
		const int64 XA = FMath::RoundToInt64(LA.X * 10.0);
		const int64 XB = FMath::RoundToInt64(LB.X * 10.0);
		if (XA != XB)
		{
			return XA < XB;
		}
		const int64 YA = FMath::RoundToInt64(LA.Y * 10.0);
		const int64 YB = FMath::RoundToInt64(LB.Y * 10.0);
		if (YA != YB)
		{
			return YA < YB;
		}
		return A->GetFName().LexicalLess(B->GetFName());
	});
	Tiles.Append(Found);
	for (int32 Index = 0; Index < Tiles.Num(); ++Index)
	{
		Tiles[Index]->SetRoad(this, Index);
	}
	ShowLit(0.f);
}

TArray<FGFHopTileSlot> AGFAccelHopRoad::BuildSlots() const
{
	TArray<FGFHopTileSlot> Slots;
	Slots.Reserve(Tiles.Num());
	for (const AGFAccelHopTile* Tile : Tiles)
	{
		FGFHopTileSlot Slot;
		if (Tile)
		{
			Slot.Along = AlongOf(Tile->GetActorLocation());
			Slot.Kind = Tile->GetKind();
		}
		Slots.Add(Slot);
	}
	return Slots;
}

AGFAccelHopTile* AGFAccelHopRoad::GetLitTile() const
{
	return Tiles.IsValidIndex(LitState.Index) ? Tiles[LitState.Index].Get() : nullptr;
}

bool AGFAccelHopRoad::IsTileLit(const AGFAccelHopTile* Tile) const
{
	return Tile && Tile == GetLitTile();
}

bool AGFAccelHopRoad::ServerIsLitLanding(const AGFAccelHopTile* Tile, double Now) const
{
	if (!Tile || !HasAuthority() || Tile->GetRoad() != this)
	{
		return false;
	}
	if (IsTileLit(Tile))
	{
		return true;
	}
	return Tile->GetRoadIndex() == PreviousLitIndex && Now <= PreviousLitUntil;
}

void AGFAccelHopRoad::ServerConsumeLit(const AGFAccelHopTile* Tile, double Now)
{
	if (!Tile || !HasAuthority() || Tile->GetRoad() != this)
	{
		return;
	}
	if (IsTileLit(Tile))
	{
		ServerSetLit(INDEX_NONE, Now);
	}
	// Paid once: a second player (or a bounce) on the same tile inside the grace window gets the normal gain.
	if (Tile->GetRoadIndex() == PreviousLitIndex)
	{
		PreviousLitUntil = -1.0e9;
	}
}

void AGFAccelHopRoad::ServerSetLit(int32 Index, double Now)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 Next = Tiles.IsValidIndex(Index) ? Index : INDEX_NONE;
	if (Next == LitState.Index)
	{
		return;
	}
	if (LitState.Index != INDEX_NONE)
	{
		PreviousLitIndex = LitState.Index;
		PreviousLitUntil = Now + static_cast<double>(FMath::Max(0.f, LitTuning.ServerGraceSeconds));
	}
	LitState.Index = Next;
	++LitState.Serial;
	ForceNetUpdate();
	ShowLit(0.f); // the server's own view (listen host); clients follow in OnRep_LitState
}

void AGFAccelHopRoad::ServerPickFrom(float AnchorAlong, float Direction, double Now)
{
	if (!HasAuthority())
	{
		return;
	}
	const TArray<FGFHopTileSlot> Slots = BuildSlots();
	if (FGFAccelHopLitRules::KeepLit(Slots, LitState.Index, AnchorAlong, Direction, LitTuning))
	{
		return;
	}
	ServerSetLit(FGFAccelHopLitRules::PickLitIndex(Slots, AnchorAlong, Direction, LitState.Index, LitState.Serial, LitTuning), Now);
}

void AGFAccelHopRoad::OnRep_LitState()
{
	ShowLit(0.f);
}

void AGFAccelHopRoad::ShowLit(float Pulse)
{
	AGFAccelHopTile* Want = GetLitTile();
	AGFAccelHopTile* Shown = ShownLit.Get();
	if (Shown && Shown != Want)
	{
		Shown->SetLitVisual(false, 0.f, LitTuning);
	}
	if (Want)
	{
		Want->SetLitVisual(true, Pulse, LitTuning);
	}
	ShownLit = Want;
}

float AGFAccelHopRoad::GetLitPickBeatSeconds()
{
	if (GridBeatSeconds <= 0.f)
	{
		// The road song's committed tempo (Beat Hop's fallback song by default), never a local track's (issue 343).
		const FName Song = FGFBeatHopMusic::PickSong(EGFBeatHopMode::Songs, SongId, false, false);
		const FGFBeatmap* Map = FGFBuiltInSongs::Find(Song, SongDifficulty);
		const double Seconds = Map ? Map->SecondsPerBeat() : 0.5;
		GridBeatSeconds = FGFAccelHopRules::ClampBeatSeconds(static_cast<float>(Seconds > 0.0 ? Seconds : 0.5));
	}
	return GridBeatSeconds;
}

int64 AGFAccelHopRoad::GetLitPickBeatIndex(double Now)
{
	// Always the server's own steady grid, also on a listen host or a solo player. Each player may hear a different
	// track at a different BPM (issue 343), so the shared pick must not ride the clock this machine hears: that
	// clock only sets this player's beat feel, pulses and HUD timing.
	return static_cast<int64>(FMath::FloorToDouble(Now / static_cast<double>(GetLitPickBeatSeconds())));
}

bool AGFAccelHopRoad::FindAnchor(float& OutAlong, float& OutDirection)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FTransform BoxTransform = Bounds->GetComponentTransform();
	const FVector Extent = Bounds->GetUnscaledBoxExtent();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn || !BoxContains(BoxTransform, Extent, Pawn->GetActorLocation()))
		{
			continue;
		}
		OutAlong = AlongOf(Pawn->GetActorLocation());
		FTransform NoScale = BoxTransform;
		NoScale.SetScale3D(FVector::OneVector);
		const float AlongSpeed = static_cast<float>(NoScale.InverseTransformVector(Pawn->GetVelocity()).X);
		if (FMath::Abs(AlongSpeed) > 50.f)
		{
			LastDirection = AlongSpeed < 0.f ? -1.f : 1.f; // standing still keeps the last heading
		}
		OutDirection = LastDirection;
		return true;
	}
	return false;
}

void AGFAccelHopRoad::ServerTickPick(double Now)
{
	const int64 BeatIndex = GetLitPickBeatIndex(Now);
	if (BeatIndex == LastPickBeat)
	{
		return;
	}
	LastPickBeat = BeatIndex;
	float Along = 0.f;
	float Direction = 1.f;
	if (!FindAnchor(Along, Direction))
	{
		ServerSetLit(INDEX_NONE, Now); // nobody on the road: no target
		return;
	}
	ServerPickFrom(Along, Direction, Now);
}

AGFAccelHopRoad* AGFAccelHopRoad::FindLocalRoad(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFAccelHopRoad> It(World); It; ++It)
	{
		if (It->IsLocalPlayerOnRoad())
		{
			return *It;
		}
	}
	return nullptr;
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
	if (HasAuthority() && bAutoPick)
	{
		ServerTickPick(Now);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return; // no local player, no music, nothing to draw
	}
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

	if (bOwnsClock && IsPlayingLocalTrack() && GetLocalTrackPosition() >= LocalDurationSec)
	{
		// A local track ended: stopping notes it as played to the end, so the next start moves on through the playlist
		// (Repeat One plays it again).
		StopRoadMusic();
		if (bLocalInside)
		{
			StartRoadMusic();
		}
	}
	else if (bOwnsClock && Beat && Beat->GetBeatSource() == EGFBeatSource::Song && SongSeconds > 0.f && Beat->GetSongTimeSec() >= SongSeconds)
	{
		// The song ended: start it again from beat 0 so the clock and the audio stay together.
		StopRoadMusic();
		if (bLocalInside)
		{
			StartRoadMusic();
		}
	}

	const bool bBeat = Beat && Beat->IsRunning();
	UpdateCollection(bBeat ? FGFAccelHopRules::BeatPulse(Beat->GetCurrentBeat()) : 0.f);
	// The lit tile's own sharper pulse on top of the road-wide one, on the beat this machine hears.
	ShowLit(bBeat ? FGFAccelHopLitRules::LitPulse(Beat->GetCurrentBeat(), LitTuning.LitPulseBeats) : 0.f);
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
	if (StartLocalTrack(Beat, GetMusicDirector().ChooseOnEnter()))
	{
		return;
	}
	// No local track (empty or missing folder, everything excluded, or the file would not load): the committed song.
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

FGFHopMusicDirector& AGFAccelHopRoad::GetMusicDirector()
{
	if (FGFHopMusicDirector* Shared = UGFHopMusicSubsystem::FindDirector(this))
	{
		return *Shared;
	}
	if (!OwnDirector)
	{
		OwnDirector = MakeUnique<FGFHopMusicDirector>();
	}
	return *OwnDirector;
}

float AGFAccelHopRoad::GetLocalTrackPosition() const
{
	return IsPlayingLocalTrack() ? LocalStartSec + static_cast<float>(FGFAudioClock::Now() - LocalStartedAt) : 0.f;
}

bool AGFAccelHopRoad::StartLocalTrack(UGFBeatClockSubsystem* Beat, const FGFHopChoice& Choice)
{
	FGFHopMusicDirector& Director = GetMusicDirector();
	const FGFHopTrack* Track = Choice.IsLocal() ? Director.FindTrack(Choice.Key) : nullptr;
	if (!Beat || !Track)
	{
		return false;
	}
	TArray<uint8> Bytes;
	FGFWavPcm Pcm;
	FString Error;
	if (!FFileHelper::LoadFileToArray(Bytes, *Track->Path))
	{
		Error = TEXT("could not be read");
	}
	else if (!FGFHopMusicLibrary::DecodeWav(Bytes, Pcm, Error))
	{
		Error = FString::Printf(TEXT("did not decode (%s)"), *Error);
	}
	if (!Error.IsEmpty())
	{
		UE_LOG(LogGF, Warning, TEXT("Acceleration Hop music: %s %s; playing the built-in song"), *Track->Key, *Error);
		return false;
	}
	Bytes.Empty();
	const double Duration = Pcm.DurationSec();
	const double StartAt = Choice.StartSec > 0.f && Choice.StartSec < Duration - 0.25 ? Choice.StartSec : 0.0;

	// The clock runs on the track's own tempo, so hops, the tile pulse and the boost length all follow it.
	FGFBeatmap Map;
	Map.SongId = FName(*FString::Printf(TEXT("LocalHop_%s"), *FPaths::GetBaseFilename(Track->Key)));
	Map.Title = Track->Title;
	Map.Difficulty = SongDifficulty;
	Map.Bpm = Track->Bpm;
	Map.OffsetMs = Track->OffsetMs;
	if (!Beat->StartBeatmapAt(Map, StartAt))
	{
		return false;
	}
	bOwnsClock = true;
	SongSeconds = 0.f;
	LocalKey = Track->Key;
	LocalStartSec = static_cast<float>(StartAt);
	LocalStartedAt = FGFAudioClock::Now();
	LocalDurationSec = static_cast<float>(Duration);
	Director.NoteStarted(LocalKey, LocalStartSec);
	UE_LOG(LogGF, Log, TEXT("Acceleration Hop music: %s at %.1f BPM from %.1f s%s"), *Track->Key, Track->Bpm, StartAt, Choice.bResumed ? TEXT(" (resumed)") : TEXT(""));

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(Pcm.SampleRate);
	Wave->NumChannels = Pcm.Channels;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->SoundGroup = SOUNDGROUP_Music;
	Wave->bLooping = false;
	const int64 FirstSample = FMath::Clamp<int64>(static_cast<int64>(StartAt * Pcm.SampleRate), 0, Pcm.NumFrames()) * Pcm.Channels;
	const int64 QueueBytes = (Pcm.Samples.Num() - FirstSample) * static_cast<int64>(sizeof(int16));
	if (QueueBytes > 0 && QueueBytes <= MAX_int32)
	{
		Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.Samples.GetData() + FirstSample), static_cast<int32>(QueueBytes));
	}
	// Null without an audio device (-nosound): the clock keeps the beat anyway.
	Music = UGameplayStatics::CreateSound2D(this, Wave, MusicVolume, 1.f, 0.f, nullptr, false, false);
	if (Music)
	{
		Music->Play(0.f);
	}
	return true;
}

void AGFAccelHopRoad::StopRoadMusic()
{
	if (IsPlayingLocalTrack())
	{
		// Remember where the player left it (Resume), clamped to the end when the track ran out.
		const FString Key = LocalKey;
		const float Position = FMath::Min(GetLocalTrackPosition(), LocalDurationSec);
		LocalKey.Reset();
		LocalStartSec = 0.f;
		LocalDurationSec = 0.f;
		GetMusicDirector().NoteStopped(Key, Position);
	}
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
