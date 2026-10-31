#include "GFBeatHopCourse.h"
#include "GFBuiltInSongs.h"
#include "GFBeatClockSubsystem.h"
#include "GFBeatHopGateSubsystem.h"
#include "GFRhythmSettings.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFAnimMap.h"
#include "GFSimpleAnimComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "GFMiniGameCatalog.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Misc/SecureHash.h"
#include "GFLog.h"

namespace GFBeatHopCourse
{
	static const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static const FName ColorParam = TEXT("Color");
	static constexpr double LiveSpeedCms = 600.0;
	static constexpr double EndPaddingSeconds = 2.0;
	static constexpr float RetrySeconds = 1.f;

	// Art (batch beat_hop_v1). Bricks and kit pieces are bottom-centred: their top is Size.Z above the origin.
	static const TCHAR* ArtRoot = TEXT("/Game/Art/BeatHop/");
	static const FName EmissiveStrengthParam = TEXT("EmissiveStrength");
	static const FName ScreenFrameParam = TEXT("Frame");
	static const FName EmissiveColorParam = TEXT("EmissiveColor");
	static constexpr double ArtBrickHeightCm = 31.25;
	static constexpr double ArtBrickLengthCm = 100.0;
	static constexpr double ArtHoldLengthCm = 300.0;
	static constexpr double CatchTileCm = 400.0;
	static constexpr double CatchCollisionTopCm = 12.5;
	static constexpr double RailPieceCm = 100.0;
	static constexpr double PillarEveryCm = 800.0;
	static constexpr double PillarHeightCm = 400.0;
	static constexpr double StartPadCm = 700.0;
	static constexpr double LiveKitLengthCm = 600.0 * 600.0; // ten minutes of live run at 600 cm/s
	static constexpr float Gravity = 980.f;
	static constexpr float CrumbleSeconds = 1.5f;
	static constexpr float BurstSeconds = 0.6f;
	static constexpr int32 MaxPops = 32;
	static constexpr int32 JudgementRing = 24;
	static constexpr double TeleportForgiveSeconds = 0.5;
	static const FLinearColor Coral(1.f, 0.254f, 0.13f);
	static const FLinearColor Mint(0.392f, 0.791f, 0.624f);
	static const FLinearColor MissGrey(0.08f, 0.08f, 0.09f);

	// Hop clips (#207): DT_AnimMap keys.
	static const FName AnimTakeoff = TEXT("Player.Bhop_Takeoff");
	static const FName AnimAir = TEXT("Player.Bhop_Air");
	static const FName AnimStrafeL = TEXT("Player.Air_Strafe_L");
	static const FName AnimStrafeR = TEXT("Player.Air_Strafe_R");
	static const FName AnimLand = TEXT("Player.Bhop_Land");
	static const FName AnimLandPerfect = TEXT("Player.Land_Perfect");
	static const FName AnimStumble = TEXT("Player.Stumble");
	static const FName AnimFall = TEXT("Player.Fall");
	static const FName AnimLandCatch = TEXT("Player.Land_Catch");
	static const FName AnimRunFast = TEXT("Player.Run_Fast");
	static const FName AnimVictory = TEXT("Player.Victory");
	static constexpr float StrafeSpeedCms = 120.f;
	static constexpr float FallBelowTopCm = 60.f;
	static constexpr float MoveSpeedCms = 20.f;

	// Session lobby (#212), course-relative. The lobby floor runs from LobbyFromX to LobbyEndX, where the start pad
	// takes over; it never moves, so picking a song (which rebuilds the track kit) never pulls the floor away.
	static constexpr double LobbyFromX = -2200.0;
	static constexpr double LobbyEndX = -200.0;
	static constexpr double LobbySpawnX = -2050.0;
	static constexpr double SongRowX = -1700.0;
	static constexpr double DifficultyRowX = -1250.0;
	static constexpr double CourseRowX = -800.0;
	static constexpr double HireRowX = -450.0;
	static constexpr double TileSpacingY = 160.0;
	static constexpr double LostMarginCm = 1500.0;
	static const FLinearColor Sunflower(0.913f, 0.578f, 0.054f);
	static const FLinearColor Lavender(0.456f, 0.266f, 0.745f);
	static const FLinearColor TileOff(0.03f, 0.03f, 0.035f);

	static FString ArtPath(const TCHAR* Folder, const FString& Name)
	{
		return FString::Printf(TEXT("%s%s/%s.%s"), ArtRoot, Folder, *Name, *Name);
	}

	static UStaticMesh* LoadArtMesh(const FString& Path)
	{
		return Path.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	static float AnimRate(FName Key, float Speed)
	{
		return FGFAnimMap::PlayRateFor(Key, Speed);
	}
}

const TCHAR* FGFBeatHopVfx::JudgementTexturePath = TEXT("/Game/Art/BeatHop/VFX/Textures/T_VFX_Judgement.T_VFX_Judgement");
const TCHAR* FGFBeatHopVfx::SparkleTexturePath = TEXT("/Game/Art/BeatHop/VFX/Textures/T_VFX_Sparkle.T_VFX_Sparkle");

bool FGFBeatHopVfx::WordRect(EGFJudgement Judgement, FIntRect& Out)
{
	// T_VFX_Judgement_layout.json (x, y, w, h).
	switch (Judgement)
	{
	case EGFJudgement::Perfect: Out = FIntRect(FIntPoint(1, 2), FIntPoint(1 + 41, 2 + 7)); return true;
	case EGFJudgement::Good: Out = FIntRect(FIntPoint(1, 12), FIntPoint(1 + 23, 12 + 7)); return true;
	case EGFJudgement::Miss: Out = FIntRect(FIntPoint(34, 12), FIntPoint(34 + 23, 12 + 7)); return true;
	default: return false;
	}
}

FIntRect FGFBeatHopVfx::ComboRect() { return FIntRect(FIntPoint(1, 22), FIntPoint(1 + 29, 22 + 7)); }
FIntRect FGFBeatHopVfx::TimesRect() { return FIntRect(FIntPoint(34, 22), FIntPoint(34 + 5, 22 + 7)); }
FIntRect FGFBeatHopVfx::PlusRect() { return FIntRect(FIntPoint(42, 22), FIntPoint(42 + 5, 22 + 7)); }

FIntRect FGFBeatHopVfx::DigitRect(int32 Digit)
{
	Digit = FMath::Clamp(Digit, 0, 9);
	const int32 X = 1 + 12 * (Digit % 5);
	const int32 Y = Digit < 5 ? 34 : 49;
	return FIntRect(FIntPoint(X, Y), FIntPoint(X + 5, Y + 7));
}

FLinearColor FGFBeatHopVfx::Tint(EGFJudgement Judgement)
{
	switch (Judgement)
	{
	case EGFJudgement::Perfect: return FLinearColor(FColor(0xF5, 0xC8, 0x42)); // sunflower
	case EGFJudgement::Good: return FLinearColor(FColor(0xA8, 0xE6, 0xCF)); // mint
	case EGFJudgement::Miss: return FLinearColor(FColor(0xC7, 0x7B, 0x5B)); // clay_2 (GF_Palette.gpl)
	default: return FLinearColor::White;
	}
}

FLinearColor FGFBeatHopVfx::ComboTint()
{
	return FLinearColor(FColor(0xB4, 0x8D, 0xE0)); // lavender
}

int32 FGFBeatHopVfx::SparkleFrame(double AgeSeconds)
{
	if (AgeSeconds < 0.0)
	{
		return INDEX_NONE;
	}
	const int32 Frame = FMath::FloorToInt32(AgeSeconds * SparkleFps);
	return Frame < SparkleFrames ? Frame : INDEX_NONE;
}

AGFBeatHopCourse::AGFBeatHopCourse()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CatchFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CatchFloor"));
	CatchFloor->SetupAttachment(Root);
	CatchFloor->SetHiddenInGame(true);
	CatchFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Brick palette (#201, linear): coral, sunflower, mint, sky, lavender.
	LaneColors = {
		FLinearColor(1.000f, 0.254f, 0.130f),
		FLinearColor(0.913f, 0.578f, 0.054f),
		FLinearColor(0.392f, 0.791f, 0.624f),
		FLinearColor(0.209f, 0.578f, 0.888f),
		FLinearColor(0.456f, 0.266f, 0.745f),
	};
	RescueSalt = FGuid::NewGuid();
	SelectedCourseId = FGFBeatHopCourses::Default().Id;
}

void AGFBeatHopCourse::BeginPlay()
{
	Super::BeginPlay();
	if (bSession && LobbyComponents.Num() == 0)
	{
		BuildLobby();
	}
	if (KitComponents.Num() == 0)
	{
		BuildTrackKit(); // the start pad and gate stand ready before the first run
	}
}

void AGFBeatHopCourse::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFBeatHopCourse, RunSongId);
	DOREPLIFETIME(AGFBeatHopCourse, RunCourseId);
	DOREPLIFETIME(AGFBeatHopCourse, RunDifficulty);
	DOREPLIFETIME(AGFBeatHopCourse, RunMode);
	DOREPLIFETIME(AGFBeatHopCourse, bRunCozy);
	DOREPLIFETIME(AGFBeatHopCourse, RunStartServerTime);
	DOREPLIFETIME(AGFBeatHopCourse, bRunning);
	DOREPLIFETIME(AGFBeatHopCourse, RunCalibrationSeconds);
	DOREPLIFETIME(AGFBeatHopCourse, Judgements);
	DOREPLIFETIME(AGFBeatHopCourse, ScoreReps);
	DOREPLIFETIME(AGFBeatHopCourse, HitBricks);
	DOREPLIFETIME(AGFBeatHopCourse, AnimReps);
	DOREPLIFETIME(AGFBeatHopCourse, MoveReps);
	DOREPLIFETIME(AGFBeatHopCourse, Placements);
	DOREPLIFETIME(AGFBeatHopCourse, bSession);
	DOREPLIFETIME(AGFBeatHopCourse, SelectedSong);
	DOREPLIFETIME(AGFBeatHopCourse, SelectedDifficulty);
	DOREPLIFETIME(AGFBeatHopCourse, SelectedCourseId);
	DOREPLIFETIME(AGFBeatHopCourse, Results);
	DOREPLIFETIME(AGFBeatHopCourse, ResultsEndServerTime);
	DOREPLIFETIME(AGFBeatHopCourse, RevengeSpot);
	DOREPLIFETIME(AGFBeatHopCourse, bRevengeShown);
	DOREPLIFETIME(AGFBeatHopCourse, RescueSpot);
	DOREPLIFETIME(AGFBeatHopCourse, bRescueShown);
}

bool AGFBeatHopCourse::StartRun(FName SongId, EGFBeatDifficulty Difficulty, EGFBeatHopMode Mode, bool bCozy, FString& OutReason)
{
	int32 NumPlayers = 1;
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GS = World->GetGameState())
		{
			NumPlayers = FMath::Max(1, GS->PlayerArray.Num());
		}
	}
	return StartRunForPlayers(SongId, Difficulty, Mode, bCozy, NumPlayers, OutReason);
}

bool AGFBeatHopCourse::StartRunForPlayers(FName SongId, EGFBeatDifficulty Difficulty, EGFBeatHopMode Mode, bool bCozy, int32 NumPlayers, FString& OutReason)
{
	OutReason.Reset();
	FallbackReason.Reset();
	if (!HasAuthority())
	{
		OutReason = TEXT("Only the host can start a run.");
		return false;
	}
	if (!FGFBeatHopSessionRules::CanStart(Mode, NumPlayers, SongId, Difficulty, &OutReason))
	{
		return false;
	}

	Runs.Reset();
	WasFalling.Reset();
	EndReason.Reset();
	bCoursePassed = false;
	bLastPassed = false;
	RunCourseId = SelectedCourseId.IsNone() || !FGFBeatHopCourses::Find(SelectedCourseId)
		? FGFBeatHopCourses::Default().Id
		: SelectedCourseId;
	RunId = FGuid::NewGuid(); // identifies the run; every grant's TxId is derived from it
	Judgements.Reset();
	ScoreReps.Reset();
	HitBricks.Reset();
	AnimReps.Reset();
	AppliedJudgementSeqs.Reset();
	RemoteScores.Reset();
	NextJudgementSeq = 0;
	for (FGFBeatHopMoveRep& Row : MoveReps)
	{
		Row.HoldBrick = INDEX_NONE;
		Row.HoldUntilRunTime = -1.0;
	}
	RunMode = Mode;
	RunDifficulty = Difficulty;
	bRunCozy = bCozy;
	RunSongId = Mode == EGFBeatHopMode::Songs ? SongId : NAME_None;
	Windows = FGFJudgeWindows::For(Mode, Difficulty);
	RunStartServerTime = GetServerWorldTime();

	if (Mode == EGFBeatHopMode::Songs)
	{
		const FGFBeatmap* Map = FGFBuiltInSongs::Find(SongId, Difficulty);
		check(Map); // CanStart checked it
		BuildFromBeatmap(*Map);
	}
	else
	{
		ClearBricks();
		Beatmap = FGFBeatmap();
		Beatmap.Difficulty = Difficulty;
		NumLanes = Difficulty == EGFBeatDifficulty::Hard ? 5 : 4;
		Live = FGFLiveChartGenerator();
		Live.Lanes = NumLanes;
		Live.Difficulty = Difficulty;
		LiveStartClock = 0.0;
		EndTime = TNumericLimits<double>::Max();
		if (UGFBeatClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr)
		{
			if (Clock->GetBeatSource() != EGFBeatSource::Live && !Clock->StartLive())
			{
				if (bFallbackWhenNoMusic)
				{
					// No music source: the fallback song has its own audio and beat map.
					const FName Song = FGFBeatHopMusic::PickSong(EGFBeatHopMode::Live, NAME_None, false, false);
					if (!StartRunForPlayers(Song, Difficulty, EGFBeatHopMode::Songs, bCozy, NumPlayers, OutReason))
					{
						return false;
					}
					FallbackReason = TEXT("No output device to listen to, so Beat Hop plays its own song.");
					UE_LOG(LogGF, Log, TEXT("Beat Hop: %s"), *FallbackReason);
					return true;
				}
				OutReason = TEXT("Couldn't listen to an output device. Check that something can play sound, then try again.");
				return false;
			}
			LiveStartClock = Clock->GetCorrectedNow();
			BuildTrackKit();
		}
		else
		{
			OutReason = TEXT("No beat clock in this world.");
			return false;
		}
	}
	RefreshHazards(nullptr);
	// Snapshot the host's songs offset. Every client subtracts this same value, not its own setting.
	RunCalibrationSeconds = RunMode == EGFBeatHopMode::Songs
		? static_cast<double>(GetDefault<UGFRhythmSettings>()->SongsOffsetMs) / 1000.0
		: 0.0;
	bRunning = true;
	return true;
}

void AGFBeatHopCourse::OnRep_RunSpec()
{
	// Clients rebuild the same course from the replicated song and difficulty.
	if (!HasAuthority() && !RunSongId.IsNone())
	{
		if (const FGFBeatmap* Map = FGFBuiltInSongs::Find(RunSongId, RunDifficulty))
		{
			Windows = FGFJudgeWindows::For(EGFBeatHopMode::Songs, RunDifficulty);
			BuildFromBeatmap(*Map);
			// Judgements or hit bricks may have arrived before the meshes existed.
			ApplyIncomingHitBricks();
			ApplyIncomingJudgements();
			UpdateScreen(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
		}
	}
}

void AGFBeatHopCourse::ClearBricks()
{
	for (FGFBeatHopBrick& B : Bricks)
	{
		if (B.Mesh)
		{
			B.Mesh->DestroyComponent();
		}
	}
	Bricks.Reset();
	BrickBase = 0;
	LastAddedLane = INDEX_NONE;
	for (FGFBeatHopDebris& D : Debris)
	{
		if (D.Mesh)
		{
			D.Mesh->DestroyComponent();
		}
	}
	Debris.Reset();
	Pops.Reset();
}

void AGFBeatHopCourse::BuildFromBeatmap(const FGFBeatmap& Map)
{
	ClearBricks();
	Beatmap = Map;
	NumLanes = Map.Lanes;
	EndTime = 0.0;
	for (int32 i = 0; i < Map.Notes.Num(); ++i)
	{
		const FGFBeatNote& N = Map.Notes[i];
		const double T = Map.BeatToSeconds(N.Beat);
		const double Len = N.Type == EGFBeatNoteType::Hold ? N.LenBeats * Map.SecondsPerBeat() : 0.0;
		EndTime = FMath::Max(EndTime, T + Len);
		if (!N.HasBrick())
		{
			continue; // gaps are just air
		}
		AddBrick(T, Len, N.Lane, Map.Lanes, N.Type, i);
	}
	EndTime += GFBeatHopCourse::EndPaddingSeconds;
	BuildTrackKit();
}

int32 AGFBeatHopCourse::AddBrick(double Time, double LenSeconds, int32 Lane, int32 Lanes, EGFBeatNoteType Type, int32 SourceId)
{
	if (!CubeMesh)
	{
		CubeMesh = LoadObject<UStaticMesh>(nullptr, GFBeatHopCourse::CubePath);
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, GFBeatHopCourse::MaterialPath);
	}

	const double Speed = RunMode == EGFBeatHopMode::Live ? GFBeatHopCourse::LiveSpeedCms : static_cast<double>(Beatmap.RefSpeedCms);
	const double StartTime = RunMode == EGFBeatHopMode::Live ? LiveStartClock : 0.0;
	const double Length = BrickSizeCm.X + LenSeconds * Speed;
	double X = (Time - StartTime) * Speed + LenSeconds * Speed * 0.5;
	double Y = (Lane - (Lanes - 1) * 0.5) * LaneWidthCm;
	// Songs follow the selected course. Fern is the chart itself. Live bricks stay on the chart (#114).
	if (RunMode == EGFBeatHopMode::Songs)
	{
		const FGFBeatHopCourseDef* Layout = FGFBeatHopCourses::Find(RunCourseId);
		if (!Layout)
		{
			Layout = &FGFBeatHopCourses::Default();
		}
		const FVector2D At = FGFBeatHopCourses::Place(*Layout, Bricks.Num(), X, Y, LaneWidthCm);
		X = At.X;
		Y = At.Y;
	}

	FGFBeatHopBrick B;
	B.Time = Time;
	B.LenSeconds = LenSeconds;
	B.Lane = Lane;
	B.Type = Type;
	B.SourceId = SourceId;

	// Art brick (#201): SM_Block_Beat_L1-L5 by lane for taps, the Hold/Pad/Switch meshes otherwise. Bottom-centred,
	// 100 x 100 x 31.25 (Hold 300 long), so the top sits at Z 0. The engine cube is the fallback.
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Mesh->SetupAttachment(Root);
	UStaticMesh* Art = GFBeatHopCourse::LoadArtMesh(BrickMeshPath(Type, Lane));
	const float PadScale = Type == EGFBeatNoteType::Pad ? 1.3f : 1.f;
	if (Art)
	{
		Mesh->SetStaticMesh(Art);
		const double ArtLength = Type == EGFBeatNoteType::Hold ? GFBeatHopCourse::ArtHoldLengthCm : GFBeatHopCourse::ArtBrickLengthCm;
		// Switch arrows point +Y (toward higher lanes); a switch to a lower lane mirrors in Y.
		const double SignY = (Type == EGFBeatNoteType::Switch && LastAddedLane != INDEX_NONE && Lane < LastAddedLane) ? -1.0 : 1.0;
		Mesh->SetRelativeLocation(FVector(X, Y, -BrickSizeCm.Z));
		Mesh->SetRelativeScale3D(FVector(Length / ArtLength, SignY * BrickSizeCm.Y * PadScale / GFBeatHopCourse::ArtBrickLengthCm, BrickSizeCm.Z / GFBeatHopCourse::ArtBrickHeightCm));
		B.TopOffsetZ = BrickSizeCm.Z;
		if (UMaterialInterface* ArtMat = Mesh->GetMaterial(0))
		{
			B.Material = UMaterialInstanceDynamic::Create(ArtMat, this);
			Mesh->SetMaterial(0, B.Material);
		}
	}
	else
	{
		if (CubeMesh)
		{
			Mesh->SetStaticMesh(CubeMesh);
		}
		// Engine cube is 100 cm, centred on its origin.
		Mesh->SetRelativeLocation(FVector(X, Y, -BrickSizeCm.Z * 0.5));
		Mesh->SetRelativeScale3D(FVector(Length / 100.0, BrickSizeCm.Y * PadScale / 100.0, BrickSizeCm.Z / 100.0));
		B.TopOffsetZ = BrickSizeCm.Z * 0.5;
		if (BaseMaterial)
		{
			B.Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			Mesh->SetMaterial(0, B.Material);
		}
	}
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->RegisterComponent();
	B.Mesh = Mesh;
	LastAddedLane = Lane;
	return BrickBase + Bricks.Add(B);
}

FLinearColor AGFBeatHopCourse::LaneColor(int32 Lane) const
{
	return LaneColors.Num() > 0 ? LaneColors[FMath::Abs(Lane) % LaneColors.Num()] : FLinearColor::White;
}

double AGFBeatHopCourse::GetServerWorldTime() const
{
	if (DebugServerTime >= 0.0)
	{
		return DebugServerTime;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return GS->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

double AGFBeatHopCourse::GetRunTime() const
{
	if (DebugRunTime >= 0.0)
	{
		return DebugRunTime;
	}
	if (RunMode == EGFBeatHopMode::Live)
	{
		const UGFBeatClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
		return Clock ? Clock->GetCorrectedNow() : 0.0;
	}
	return GetServerWorldTime() - RunStartServerTime - RunCalibrationSeconds;
}

double AGFBeatHopCourse::PeriodSeconds() const
{
	if (RunMode == EGFBeatHopMode::Live)
	{
		const UGFBeatClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
		const double P = Clock ? Clock->GetTracker().GetBeatPeriod() : 0.0;
		return P > 0.0 ? P : 0.5;
	}
	return Beatmap.SecondsPerBeat();
}

double AGFBeatHopCourse::WindowSeconds() const
{
	return Windows.GoodMs / 1000.0;
}

EGFBrickLight AGFBeatHopCourse::GetBrickLight(int32 BrickIndex) const
{
	if (!IsValidBrick(BrickIndex))
	{
		return EGFBrickLight::Off;
	}
	const FGFBeatHopBrick& B = GetBrick(BrickIndex);
	const double P = PeriodSeconds();
	const double Now = GetRunTime();
	return FGFBrickLighting::LightAt(Now / P, B.Time / P, B.LenSeconds / P, WindowSeconds() / P, B.bHitByAnyone, B.bMissedByEveryone);
}

int32 AGFBeatHopCourse::FindBrickByComponent(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return INDEX_NONE;
	}
	const int32 Kept = Bricks.IndexOfByPredicate([Component](const FGFBeatHopBrick& B) { return B.Mesh == Component; });
	return Kept == INDEX_NONE ? INDEX_NONE : BrickBase + Kept;
}

FVector AGFBeatHopCourse::GetBrickTop(int32 BrickIndex) const
{
	if (!IsValidBrick(BrickIndex) || !GetBrick(BrickIndex).Mesh)
	{
		return GetActorLocation();
	}
	const FGFBeatHopBrick& B = GetBrick(BrickIndex);
	return B.Mesh->GetComponentLocation() + FVector(0, 0, B.TopOffsetZ);
}

FString AGFBeatHopCourse::BrickMeshPath(EGFBeatNoteType Type, int32 Lane)
{
	switch (Type)
	{
	case EGFBeatNoteType::Tap: return GFBeatHopCourse::ArtPath(TEXT("Bricks"), FString::Printf(TEXT("SM_Block_Beat_L%d"), FMath::Abs(Lane) % 5 + 1));
	case EGFBeatNoteType::Hold: return GFBeatHopCourse::ArtPath(TEXT("Bricks"), TEXT("SM_Block_Beat_Hold"));
	case EGFBeatNoteType::Pad: return GFBeatHopCourse::ArtPath(TEXT("Bricks"), TEXT("SM_Block_Beat_Pad"));
	case EGFBeatNoteType::Switch: return GFBeatHopCourse::ArtPath(TEXT("Bricks"), TEXT("SM_Block_Beat_Switch"));
	default: return FString();
	}
}

float AGFBeatHopCourse::BeatPulseStrength(double BeatPhase)
{
	const double Phase = BeatPhase - FMath::FloorToDouble(BeatPhase);
	return static_cast<float>(1.0 + 3.0 * FMath::Exp(-6.0 * Phase));
}

const FGFRhythmScore* AGFBeatHopCourse::GetScore(const FString& PlayerKey) const
{
	const FPlayerRun* Run = Runs.Find(PlayerKey);
	if (Run)
	{
		return &Run->Judge.GetScore();
	}
	return RemoteScores.Find(PlayerKey);
}

void AGFBeatHopCourse::GetScoreboard(TArray<FGFBeatHopScoreRep>& Out) const
{
	Out.Reset();
	TSet<FString> Seen;
	for (const TPair<FString, FPlayerRun>& Pair : Runs)
	{
		const FGFRhythmScore& S = Pair.Value.Judge.GetScore();
		FGFBeatHopScoreRep Row;
		Row.PlayerKey = Pair.Key;
		Row.Score = S.Score;
		Row.Combo = S.Combo;
		Row.BestCombo = S.BestCombo;
		Row.Perfects = S.Perfects;
		Row.Goods = S.Goods;
		Row.Misses = S.Misses;
		Out.Add(Row);
		Seen.Add(Pair.Key);
	}
	for (const FGFBeatHopScoreRep& Row : ScoreReps)
	{
		if (!Seen.Contains(Row.PlayerKey))
		{
			Out.Add(Row);
		}
	}
}

bool AGFBeatHopCourse::IsBrickHitByAnyone(int32 Index) const
{
	return IsValidBrick(Index) && GetBrick(Index).bHitByAnyone;
}

const FGFInventory* AGFBeatHopCourse::GetEarnedRewards(const FString& PlayerKey) const
{
	const FPlayerRun* Run = Runs.Find(PlayerKey);
	return Run ? &Run->Earned : nullptr;
}

FGuid AGFBeatHopCourse::MakeGrantTxId(const FGuid& InRunId, const FString& PlayerKey, const FString& What)
{
	// Deterministic: the same run, player and reward always give the same TxId, so the ledger pays it at most once.
	const FString Text = FString::Printf(TEXT("GF.BeatHop|%s|%s|%s"), *InRunId.ToString(EGuidFormats::Digits), *PlayerKey, *What);
	const FTCHARToUTF8 Utf8(*Text);
	FMD5 Md5;
	Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	uint8 Digest[16];
	Md5.Final(Digest);
	uint32 W[4];
	FMemory::Memcpy(W, Digest, sizeof(W));
	return FGuid(W[0], W[1], W[2], W[3] | 1u); // never the all-zero (invalid) guid
}

int32 AGFBeatHopCourse::CurrentPlayerCount() const
{
	if (DebugPlayerCount >= 0)
	{
		return DebugPlayerCount;
	}
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	return GS ? GS->PlayerArray.Num() : 1;
}

bool AGFBeatHopCourse::TryMint(const FGFBeatHopPendingGrant& G)
{
	EGFTxResult R = EGFTxResult::UnknownAccount;
	if (LedgerOverride && AccountsOverride)
	{
		R = LedgerOverride->Mint(*AccountsOverride, G.TxId, G.PlayerKey, G.Stacks, G.Reason);
	}
	else if (AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr; GS && HasAuthority())
	{
		R = GS->ServerMint(G.TxId, G.PlayerKey, G.Stacks, G.Reason);
	}
	if (R == EGFTxResult::Applied || R == EGFTxResult::AlreadyApplied)
	{
		return true;
	}
	if (R == EGFTxResult::InvalidStacks)
	{
		UE_LOG(LogGF, Error, TEXT("Beat Hop: reward for %s has invalid stacks; dropped"), *G.PlayerKey);
		return true; // retrying can't fix it
	}
	return false; // UnknownAccount: the player's account isn't readable yet (or no ledger in this world)
}

int32 AGFBeatHopCourse::RetryPendingGrants()
{
	if (!HasAuthority())
	{
		return 0;
	}
	int32 Done = 0;
	for (int32 i = PendingGrants.Num() - 1; i >= 0; --i)
	{
		if (TryMint(PendingGrants[i]))
		{
			PendingGrants.RemoveAt(i);
			++Done;
		}
	}
	return Done;
}

AGFBeatHopCourse::FPlayerRun& AGFBeatHopCourse::RunFor(const FString& PlayerKey)
{
	FPlayerRun* Run = Runs.Find(PlayerKey);
	if (!Run)
	{
		Run = &Runs.Add(PlayerKey);
		Run->Judge = FGFRhythmJudge(Windows);
		PublishScore(PlayerKey, Run->Judge.GetScore());
	}
	return *Run;
}

void AGFBeatHopCourse::Grant(const FString& PlayerKey, FPlayerRun& Run, const TArray<FGFItemStack>& Stacks, FName Reason, const FString& What)
{
	if (Stacks.Num() == 0 || !HasAuthority())
	{
		return;
	}
	for (const FGFItemStack& S : Stacks)
	{
		Run.Earned.Add(S.ItemId, S.Count);
	}
	FGFBeatHopPendingGrant G;
	G.TxId = MakeGrantTxId(RunId, PlayerKey, What);
	G.PlayerKey = PlayerKey;
	G.Stacks = Stacks;
	G.Reason = Reason;
	if (!TryMint(G))
	{
		PendingGrants.Add(MoveTemp(G)); // same TxId on every retry
	}
}

EGFJudgement AGFBeatHopCourse::HandleLanding(const FString& PlayerKey, int32 BrickIndex, double Now)
{
	if (!HasAuthority() || !bRunning || !IsValidBrick(BrickIndex) || PlayerKey.IsEmpty())
	{
		return EGFJudgement::None;
	}
	FPlayerRun& Run = RunFor(PlayerKey);
	FGFBeatHopBrick& B = Bricks[BrickIndex - BrickBase];
	if (Run.Judged.Contains(BrickIndex))
	{
		return EGFJudgement::None; // already judged for this player (standing on it, or re-landing)
	}

	// Holds: landing anywhere inside the held span is on time.
	double NoteTime = B.Time;
	if (B.LenSeconds > 0.0 && Now > B.Time)
	{
		NoteTime = FMath::Min(Now, B.Time + B.LenSeconds);
	}
	if (Now < NoteTime - FGFBrickLighting::DimLeadBeats * PeriodSeconds())
	{
		return EGFJudgement::None; // not even glowing yet: a stray hop, nothing judged
	}
	const EGFJudgement J = Run.Judge.JudgeLanding(Now, B.Lane, NoteTime, B.Lane);
	Run.Judged.Add(BrickIndex);
	const int32 Combo = Run.Judge.GetScore().Combo;
	RecordJudgement(PlayerKey, BrickIndex, J, Now - NoteTime, Combo);
	AddPop(PlayerKey, GetBrickTop(BrickIndex), J, Combo, LaneColor(B.Lane));
	PublishScore(PlayerKey, Run.Judge.GetScore());
	if (J == EGFJudgement::Perfect)
	{
		Burst(GetBrickTop(BrickIndex), LaneColor(B.Lane));
	}

	int32 Earned = 0;
		if (J == EGFJudgement::Perfect || J == EGFJudgement::Good)
	{
		B.bHitByAnyone = true;
		PublishHit(BrickIndex);
		const TArray<FGFItemStack> Stacks = FGFBeatHopRewards::ForLanding(J, Rewards, bRunCozy);
		for (const FGFItemStack& S : Stacks)
		{
			Earned += S.Count;
		}
		Grant(PlayerKey, Run, Stacks, TEXT("BeatHop.Landing"), FString::Printf(TEXT("Landing.%d"), BrickIndex));
	}
	OnBrickLanding.Broadcast(PlayerKey, BrickIndex, J, Earned);
	return J;
}

void AGFBeatHopCourse::SweepMisses(double Now)
{
	// A brick is missed for a player once its window has fully passed without them landing on it.
	const double Window = WindowSeconds();
	for (TPair<FString, FPlayerRun>& Pair : Runs)
	{
		FPlayerRun& Run = Pair.Value;
		// Bricks already pruned (live mode) were long past: unjudged ones are misses.
		while (Run.MissSweep < BrickBase)
		{
			if (!Run.Judged.Contains(Run.MissSweep))
			{
				Run.Judged.Add(Run.MissSweep);
				Run.Judge.AddMiss();
			}
			++Run.MissSweep;
		}
		if (Run.MissSweep > 0)
		{
			PublishScore(Pair.Key, Run.Judge.GetScore());
		}
		while (Run.MissSweep < NumBricks())
		{
			const FGFBeatHopBrick& B = GetBrick(Run.MissSweep);
			if (Now <= B.Time + B.LenSeconds + Window)
			{
				break;
			}
			if (!Run.Judged.Contains(Run.MissSweep))
			{
				Run.Judged.Add(Run.MissSweep);
				Run.Judge.AddMiss();
				if (Now < TNumericLimits<double>::Max() / 8.0) // not the end-of-run sweep
				{
					const double Offset = Now - (B.Time + B.LenSeconds);
					RecordJudgement(Pair.Key, Run.MissSweep, EGFJudgement::Miss, Offset, 0);
					AddPop(Pair.Key, GetBrickTop(Run.MissSweep), EGFJudgement::Miss, 0, LaneColor(B.Lane));
				}
				PublishScore(Pair.Key, Run.Judge.GetScore());
			}
			++Run.MissSweep;
		}
	}
	// Visual: a brick nobody hit goes "missed" once its window is gone.
	for (FGFBeatHopBrick& B : Bricks)
	{
		if (!B.bHitByAnyone && !B.bMissedByEveryone && Now > B.Time + B.LenSeconds + Window)
		{
			B.bMissedByEveryone = true;
		}
	}
}

void AGFBeatHopCourse::UpdateLights(double Now)
{
	const double P = PeriodSeconds();
	const double W = WindowSeconds() / P;
	for (FGFBeatHopBrick& B : Bricks)
	{
		if (B.bMissedByEveryone && !B.bCrumbled)
		{
			Crumble(B);
		}
		const float Glow = FGFBrickLighting::GlowAt(Now / P, B.Time / P, B.LenSeconds / P, W, B.bHitByAnyone, B.bMissedByEveryone);
		if (FMath::IsNearlyEqual(Glow, B.ShownGlow, 0.01f) || !B.Material)
		{
			continue;
		}
		B.ShownGlow = Glow;
		FLinearColor C = LaneColor(B.Lane);
		if (B.bMissedByEveryone)
		{
			C = GFBeatHopCourse::MissGrey;
		}
		else if (B.bHitByAnyone)
		{
			C = FLinearColor::LerpUsingHSV(C, FLinearColor::White, 0.6f);
		}
		else
		{
			C = C * (0.15f + 0.85f * Glow) * (1.f + 2.f * FMath::Max(0.f, Glow - 0.9f) * 10.f);
		}
		C.A = 1.f;
		B.Material->SetVectorParameterValue(GFBeatHopCourse::ColorParam, C);
	}
}

void AGFBeatHopCourse::UpdateLive(double Now)
{
	UGFBeatClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
	if (!Clock)
	{
		return;
	}
	const TArray<FGFLiveBrick> Created = Live.Update(Now, Clock->GetTracker(), Clock->GetTickOnsets());
	for (const FGFLiveBrick& LB : Created)
	{
		AddBrick(LB.Time, 0.0, LB.Lane, NumLanes, LB.Type, LB.Id);
	}
	// Unfrozen bricks follow tempo corrections (their judged time; the greybox keeps their spot).
	for (const FGFLiveBrick& LB : Live.GetBricks())
	{
		if (LB.bFrozen)
		{
			continue;
		}
		for (int32 i = Bricks.Num() - 1; i >= 0; --i)
		{
			if (Bricks[i].SourceId == LB.Id)
			{
				Bricks[i].Time = LB.Time;
				break;
			}
		}
	}
	Live.PruneBefore(Now - 2.0);
}

void AGFBeatHopCourse::PruneLiveBricks(double Now)
{
	// Live runs are endless: drop bricks well behind the player so meshes, materials and the light pass stay bounded.
	const double Window = WindowSeconds();
	int32 Drop = 0;
	while (Drop < Bricks.Num())
	{
		const FGFBeatHopBrick& B = Bricks[Drop];
		const int32 Index = BrickBase + Drop;
		if (Now <= B.Time + B.LenSeconds + Window + LiveBrickKeepSeconds)
		{
			break;
		}
		bool bSwept = true;
		for (const TPair<FString, FPlayerRun>& Pair : Runs)
		{
			bSwept &= Pair.Value.MissSweep > Index;
		}
		if (!bSwept)
		{
			break;
		}
		++Drop;
	}
	if (Drop == 0)
	{
		return;
	}
	for (int32 i = 0; i < Drop; ++i)
	{
		if (Bricks[i].Mesh)
		{
			Bricks[i].Mesh->DestroyComponent();
		}
	}
	Bricks.RemoveAt(0, Drop);
	BrickBase += Drop;
	for (TPair<FString, FPlayerRun>& Pair : Runs)
	{
		for (auto It = Pair.Value.Judged.CreateIterator(); It; ++It)
		{
			if (*It < BrickBase)
			{
				It.RemoveCurrent();
			}
		}
	}
}

void AGFBeatHopCourse::PollLandings(double Now)
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS)
	{
		return;
	}
	for (APlayerState* PS : GS->PlayerArray)
	{
		ACharacter* Char = PS ? Cast<ACharacter>(PS->GetPawn()) : nullptr;
		UCharacterMovementComponent* Move = Char ? Char->GetCharacterMovement() : nullptr;
		if (!Move)
		{
			continue;
		}
		const bool bFalling = Move->IsFalling();
		bool& bWas = WasFalling.FindOrAdd(Char);
		if (bWas && !bFalling && Move->CurrentFloor.IsWalkableFloor())
		{
			const int32 Index = FindBrickByComponent(Move->CurrentFloor.HitResult.GetComponent());
			if (Index != INDEX_NONE)
			{
				// Rewards and scores are keyed by the ledger account; a player without a key yet (still joining)
				// isn't judged at all rather than being scored under a display name.
				const AGFPlayerState* GFPS = Cast<AGFPlayerState>(PS);
				const EGFJudgement J = GFPS && !GFPS->PlayerKey.IsEmpty() ? HandleLanding(GFPS->PlayerKey, Index, Now) : EGFJudgement::None;
				LastLanding.Add(Char, J);
				if ((J == EGFJudgement::Perfect || J == EGFJudgement::Good) && GetBrick(Index).Type == EGFBeatNoteType::Pad)
				{
					Char->LaunchCharacter(FVector(0, 0, PadLaunchZ), false, true);
				}
			}
		}
		bWas = bFalling;
	}
}

void AGFBeatHopCourse::UpdateRun()
{
	const double Now = GetRunTime();
	if (bRunning && HasAuthority() && RunMode == EGFBeatHopMode::Live)
	{
		// Live mode is solo (the music is on one machine), and needs the audio device.
		const UGFBeatClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
		if (CurrentPlayerCount() > 1)
		{
			EndRun(TEXT("Play to my music is solo only: someone joined, so the run ended. Songs mode works together."));
		}
		else if (!Clock || Clock->GetBeatSource() != EGFBeatSource::Live)
		{
			EndRun(TEXT("Live audio stopped: no output device to listen to."));
		}
		else if (bFallbackWhenNoMusic && LiveSilenceFallbackSeconds > 0.f && NumBricks() == 0 && Now - LiveStartClock >= LiveSilenceFallbackSeconds)
		{
			// Nothing to follow yet: no audio at all (loopback sends nothing while nothing plays) or only silence.
			const FGFBeatTracker& Tracker = Clock->GetTracker();
			const bool bNoAudio = Tracker.GetAnalysedUntil() <= 0.0;
			if (!Tracker.IsLocked() && !FGFBeatHopMusic::PickSong(EGFBeatHopMode::Live, NAME_None, !bNoAudio, bNoAudio || Tracker.IsSilent()).IsNone())
			{
				SwitchToFallbackSong(bNoAudio
					? TEXT("No sound is coming from the output device, so Beat Hop plays its own song.")
					: TEXT("Only silence is playing, so Beat Hop plays its own song."));
				return; // the run clock changed; judge from the next tick
			}
		}
	}
	if (bRunning && HasAuthority())
	{
		if (RunMode == EGFBeatHopMode::Live)
		{
			UpdateLive(Now);
		}
		PollLandings(Now);
		SweepMisses(Now);
		if (RunMode == EGFBeatHopMode::Live)
		{
			PruneLiveBricks(Now);
		}
		if (RunMode == EGFBeatHopMode::Songs && Now > EndTime)
		{
			FinishRun();
		}
	}
	else if (!HasAuthority() && bRunning)
	{
		// Clients only drive visuals; misses show once the window passes (a lobby preview never crumbles).
		const double W = WindowSeconds();
		for (FGFBeatHopBrick& B : Bricks)
		{
			B.bMissedByEveryone = !B.bHitByAnyone && Now > B.Time + B.LenSeconds + W;
		}
	}
	UpdateLights(Now);
}

void AGFBeatHopCourse::FinishRun()
{
	if (!HasAuthority() || !bRunning)
	{
		return;
	}
	SweepMisses(TNumericLimits<double>::Max() / 4.0);
	bRunning = false;
	StopMusic();

	const int32 TotalNotes = NumBricks();
	UGFBeatHopGateSubsystem* Gate = nullptr;
	if (const UGameInstance* GI = GetGameInstance())
	{
		Gate = GI->GetSubsystem<UGFBeatHopGateSubsystem>();
	}
	bLastPassed = bCoursePassed;
	bCoursePassed = false;
	for (TPair<FString, FPlayerRun>& Pair : Runs)
	{
		FPlayerRun& Run = Pair.Value;
		const FGFRhythmScore& Score = Run.Judge.GetScore();
		// The grade bundle is the win payout. A timeout still scores, and it does not pay (#114).
		if (bLastPassed)
		{
			Grant(Pair.Key, Run, FGFBeatHopRewards::ForSongEnd(Score, TotalNotes, Rewards, bRunCozy), TEXT("BeatHop.SongEnd"), TEXT("SongEnd"));
		}

		FGFMiniGameResult Result;
		Result.Score = Score.Score;
		Result.Tier = FName(*StaticEnum<EGFBeatHopGrade>()->GetNameStringByValue(static_cast<int64>(FGFRhythmJudge::GradeFor(Score))));
		// Score and grade only: every reward went through the ledger with a player key (or waits in PendingGrants).
		if (Gate)
		{
			Gate->ReportResult(Result);
		}
		OnRunFinished.Broadcast(Pair.Key, Result);
	}
	if (RunMode == EGFBeatHopMode::Live)
	{
		UGFBeatClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
		if (Clock && Clock->GetBeatSource() == EGFBeatSource::Live)
		{
			Clock->StopClock(); // (if live audio already gave up, keep its Failed status for the UI)
		}
	}
	RetryPendingGrants();
	if (bSession)
	{
		OnSessionRunFinished();
	}
}

void AGFBeatHopCourse::EndRun(const FString& Reason)
{
	if (!HasAuthority() || !bRunning)
	{
		return;
	}
	UE_LOG(LogGF, Log, TEXT("Beat Hop: run ended early: %s"), *Reason);
	FinishRun();
	EndReason = Reason;
	OnRunEnded.Broadcast(Reason);
}

void AGFBeatHopCourse::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateRun();
	UpdateMusic();
	if (HasAuthority())
	{
		TickHazards(DeltaSeconds);
	}
	if (bSession && HasAuthority())
	{
		UpdateSession();
	}
	const double WorldNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	TickDebris(DeltaSeconds);
	UpdatePulse(WorldNow);
	UpdateScreen(WorldNow);
	DriveAnims(WorldNow);
	if (HasAuthority())
	{
		ClearTeleportForgiveness();
	}
	else
	{
		MirrorOwnerMovement();
	}
	Pops.RemoveAll([WorldNow](const FGFBeatHopPop& P) { return WorldNow - P.ShownAt > FGFBeatHopVfx::PopSeconds + 0.5; });
	if (HasAuthority() && PendingGrants.Num() > 0)
	{
		RetryTimer += DeltaSeconds;
		if (RetryTimer >= GFBeatHopCourse::RetrySeconds)
		{
			RetryTimer = 0.f;
			RetryPendingGrants();
		}
	}
}

void AGFBeatHopCourse::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopMusic();
	if (HasAuthority())
	{
		RetryPendingGrants();
		for (const FGFBeatHopPendingGrant& G : PendingGrants)
		{
			UE_LOG(LogGF, Warning, TEXT("Beat Hop: a reward for %s never reached the ledger (account not readable)"), *G.PlayerKey);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------------------------------
// Art: crumble, bursts, judgement pops (#201, #209)

void AGFBeatHopCourse::AddPop(const FString& PlayerKey, const FVector& WorldTop, EGFJudgement Judgement, int32 Combo, const FLinearColor& Color)
{
	if (Judgement == EGFJudgement::None)
	{
		return;
	}
	FGFBeatHopPop P;
	P.PlayerKey = PlayerKey;
	P.WorldLocation = WorldTop;
	P.Judgement = Judgement;
	P.Combo = Combo;
	P.LaneColor = Color;
	P.ShownAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Pops.Add(P);
	if (Pops.Num() > GFBeatHopCourse::MaxPops)
	{
		Pops.RemoveAt(0, Pops.Num() - GFBeatHopCourse::MaxPops);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Replication (#214): judgements, scores, hit bricks, hop clips, run movement, teleports.

namespace
{
	bool IsBrickLandingClip(FName Clip)
	{
		using namespace GFBeatHopCourse;
		return Clip == AnimLand || Clip == AnimLandPerfect || Clip == AnimStumble;
	}
}

FName AGFBeatHopCourse::LandingClipFor(EGFJudgement Judgement)
{
	using namespace GFBeatHopCourse;
	switch (Judgement)
	{
	case EGFJudgement::Perfect: return AnimLandPerfect;
	case EGFJudgement::Good: return AnimLand;
	case EGFJudgement::Miss: return AnimStumble;
	default: return NAME_None;
	}
}

FName AGFBeatHopCourse::ClipForViewer(bool bSimulatedProxy, FName LocalClip, FName ReplicatedClip)
{
	if (bSimulatedProxy)
	{
		return ReplicatedClip.IsNone() ? LocalClip : ReplicatedClip;
	}
	if (IsBrickLandingClip(LocalClip) && IsBrickLandingClip(ReplicatedClip))
	{
		return ReplicatedClip;
	}
	return LocalClip;
}

void AGFBeatHopCourse::AdoptSharedClock(double StartServerTime, double CalibrationSeconds)
{
	RunStartServerTime = StartServerTime;
	RunCalibrationSeconds = CalibrationSeconds;
}

FString AGFBeatHopCourse::RunnerId(const ACharacter* Character) const
{
	if (const AGFPlayerState* PS = Character ? Character->GetPlayerState<AGFPlayerState>() : nullptr)
	{
		if (!PS->PlayerKey.IsEmpty())
		{
			return PS->PlayerKey;
		}
	}
	return Character ? Character->GetName() : FString();
}

FGFBeatHopMoveRep* AGFBeatHopCourse::FindMoveRep(const FString& PlayerKey)
{
	return MoveReps.FindByPredicate([&PlayerKey](const FGFBeatHopMoveRep& Row) { return Row.PlayerKey == PlayerKey; });
}

const FGFBeatHopAnimRep* AGFBeatHopCourse::FindAnimRep(const FString& PlayerKey) const
{
	return AnimReps.FindByPredicate([&PlayerKey](const FGFBeatHopAnimRep& Row) { return Row.PlayerKey == PlayerKey; });
}

ACharacter* AGFBeatHopCourse::FindRunner(const FString& PlayerKey) const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS || PlayerKey.IsEmpty())
	{
		return nullptr;
	}
	for (APlayerState* PS : GS->PlayerArray)
	{
		const AGFPlayerState* GFPS = Cast<AGFPlayerState>(PS);
		if (GFPS && GFPS->PlayerKey == PlayerKey)
		{
			return Cast<ACharacter>(PS->GetPawn());
		}
	}
	return nullptr;
}

void AGFBeatHopCourse::RecordJudgement(const FString& PlayerKey, int32 BrickIndex, EGFJudgement Result, double OffsetSeconds, int32 Combo)
{
	if (!HasAuthority() || Result == EGFJudgement::None || PlayerKey.IsEmpty())
	{
		return;
	}
	FGFBeatHopJudgementRep Row;
	Row.PlayerKey = PlayerKey;
	Row.BrickIndex = BrickIndex;
	Row.Result = Result;
	Row.OffsetSeconds = static_cast<float>(OffsetSeconds);
	Row.Combo = Combo;
	if (++NextJudgementSeq == 0)
	{
		++NextJudgementSeq;
	}
	Row.Seq = NextJudgementSeq;
	Judgements.Add(Row);
	if (Judgements.Num() > GFBeatHopCourse::JudgementRing)
	{
		Judgements.RemoveAt(0, Judgements.Num() - GFBeatHopCourse::JudgementRing);
	}
}

void AGFBeatHopCourse::PublishScore(const FString& PlayerKey, const FGFRhythmScore& Score)
{
	if (!HasAuthority() || PlayerKey.IsEmpty())
	{
		return;
	}
	FGFBeatHopScoreRep* Row = ScoreReps.FindByPredicate([&PlayerKey](const FGFBeatHopScoreRep& Existing) { return Existing.PlayerKey == PlayerKey; });
	if (!Row)
	{
		Row = &ScoreReps.AddDefaulted_GetRef();
		Row->PlayerKey = PlayerKey;
	}
	if (Row->Score == Score.Score && Row->Combo == Score.Combo && Row->BestCombo == Score.BestCombo
		&& Row->Perfects == Score.Perfects && Row->Goods == Score.Goods && Row->Misses == Score.Misses)
	{
		return;
	}
	Row->Score = Score.Score;
	Row->Combo = Score.Combo;
	Row->BestCombo = Score.BestCombo;
	Row->Perfects = Score.Perfects;
	Row->Goods = Score.Goods;
	Row->Misses = Score.Misses;
}

void AGFBeatHopCourse::PublishHit(int32 BrickIndex)
{
	if (!HasAuthority() || BrickIndex == INDEX_NONE)
	{
		return;
	}
	HitBricks.AddUnique(BrickIndex);
}

void AGFBeatHopCourse::PublishAnim(const FString& PlayerKey, FName Clip, float PlayRate, bool bOneShot)
{
	if (!HasAuthority() || PlayerKey.IsEmpty() || Clip.IsNone())
	{
		return;
	}
	FGFBeatHopAnimRep* Row = AnimReps.FindByPredicate([&PlayerKey](const FGFBeatHopAnimRep& Existing) { return Existing.PlayerKey == PlayerKey; });
	if (!Row)
	{
		Row = &AnimReps.AddDefaulted_GetRef();
		Row->PlayerKey = PlayerKey;
	}
	if (Row->Clip == Clip && FMath::IsNearlyEqual(Row->PlayRate, PlayRate, 0.05f) && Row->bOneShot == (bOneShot ? 1 : 0))
	{
		return;
	}
	Row->Clip = Clip;
	Row->PlayRate = PlayRate;
	Row->bOneShot = bOneShot ? 1 : 0;
	++Row->Seq;
}

void AGFBeatHopCourse::PublishMove(ACharacter* Character, uint8 Phase)
{
	if (!HasAuthority() || !Character)
	{
		return;
	}
	const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (!Move)
	{
		return;
	}
	const FString Id = RunnerId(Character);
	if (Id.IsEmpty())
	{
		return;
	}
	FGFBeatHopMoveRep* Row = FindMoveRep(Id);
	if (!Row)
	{
		Row = &MoveReps.AddDefaulted_GetRef();
		Row->PlayerKey = Id;
	}
	const FSavedRunMove* Saved = SavedMoves.Find(Character);
	Row->MaxWalkSpeed = Move->MaxWalkSpeed;
	Row->GravityScale = Move->GravityScale;
	Row->AirControl = Move->AirControl;
	Row->SavedWalkSpeed = Saved ? Saved->MaxWalkSpeed : Move->MaxWalkSpeed;
	Row->SavedGravityScale = Saved ? Saved->GravityScale : Move->GravityScale;
	Row->SavedAirControl = Saved ? Saved->AirControl : Move->AirControl;
	Row->Phase = Phase;
}

void AGFBeatHopCourse::PublishHold(ACharacter* Character, const FString& PlayerKey, int32 BrickIndex, double UntilRunTime)
{
	if (!HasAuthority())
	{
		return;
	}
	const FString Id = !PlayerKey.IsEmpty() ? PlayerKey : RunnerId(Character);
	FGFBeatHopMoveRep* Row = FindMoveRep(Id);
	if (!Row)
	{
		Row = FindMoveRep(RunnerId(Character));
	}
	if (!Row)
	{
		return;
	}
	Row->HoldBrick = BrickIndex;
	Row->HoldUntilRunTime = UntilRunTime;
}

void AGFBeatHopCourse::PublishPlacement(ACharacter* Character, const FString& PlayerKey, const FVector& Velocity, bool bFalling)
{
	if (!HasAuthority() || !Character)
	{
		return;
	}
	FString Id = !PlayerKey.IsEmpty() ? PlayerKey : RunnerId(Character);
	if (Id.IsEmpty())
	{
		return;
	}
	FGFBeatHopPlacementRep* Row = Placements.FindByPredicate([&Id](const FGFBeatHopPlacementRep& Existing) { return Existing.PlayerKey == Id; });
	if (!Row)
	{
		Row = &Placements.AddDefaulted_GetRef();
		Row->PlayerKey = Id;
	}
	Row->Location = Character->GetActorLocation();
	Row->Yaw = Character->GetActorRotation().Yaw;
	Row->Velocity = Velocity;
	Row->bFalling = bFalling ? 1 : 0;
	if (++NextPlacementSeq == 0)
	{
		++NextPlacementSeq;
	}
	Row->Seq = NextPlacementSeq;
}

void AGFBeatHopCourse::ApplyIncomingJudgements()
{
	for (const FGFBeatHopJudgementRep& Row : Judgements)
	{
		if (Row.Seq == 0 || AppliedJudgementSeqs.Contains(Row.Seq))
		{
			continue;
		}
		if (!IsValidBrick(Row.BrickIndex))
		{
			continue; // course not built yet; OnRep_RunSpec tries again
		}
		AppliedJudgementSeqs.Add(Row.Seq);
		if (Row.Result == EGFJudgement::Perfect || Row.Result == EGFJudgement::Good)
		{
			Bricks[Row.BrickIndex - BrickBase].bHitByAnyone = true;
		}
		AddPop(Row.PlayerKey, GetBrickTop(Row.BrickIndex), Row.Result, Row.Combo, LaneColor(GetBrick(Row.BrickIndex).Lane));
	}
	if (AppliedJudgementSeqs.Num() > GFBeatHopCourse::JudgementRing * 2)
	{
		AppliedJudgementSeqs.Empty();
		for (const FGFBeatHopJudgementRep& Row : Judgements)
		{
			AppliedJudgementSeqs.Add(Row.Seq);
		}
	}
}

void AGFBeatHopCourse::ApplyIncomingScores()
{
	RemoteScores.Reset();
	for (const FGFBeatHopScoreRep& Row : ScoreReps)
	{
		FGFRhythmScore Score;
		Score.Score = Row.Score;
		Score.Combo = Row.Combo;
		Score.BestCombo = Row.BestCombo;
		Score.Perfects = Row.Perfects;
		Score.Goods = Row.Goods;
		Score.Misses = Row.Misses;
		RemoteScores.Add(Row.PlayerKey, Score);
	}
}

void AGFBeatHopCourse::ApplyIncomingHitBricks()
{
	for (const int32 Index : HitBricks)
	{
		if (IsValidBrick(Index))
		{
			Bricks[Index - BrickBase].bHitByAnyone = true;
		}
	}
}

void AGFBeatHopCourse::ReceiveRemoteJudgements(const TArray<FGFBeatHopJudgementRep>& In)
{
	Judgements = In;
	ApplyIncomingJudgements();
}

void AGFBeatHopCourse::ReceiveRemoteScores(const TArray<FGFBeatHopScoreRep>& In)
{
	ScoreReps = In;
	ApplyIncomingScores();
}

void AGFBeatHopCourse::ReceiveRemoteHitBricks(const TArray<int32>& In)
{
	HitBricks = In;
	ApplyIncomingHitBricks();
}

void AGFBeatHopCourse::OnRep_Judgements()
{
	ApplyIncomingJudgements();
}

void AGFBeatHopCourse::OnRep_Scores()
{
	ApplyIncomingScores();
}

void AGFBeatHopCourse::OnRep_HitBricks()
{
	ApplyIncomingHitBricks();
}

void AGFBeatHopCourse::OnRep_Moves()
{
	MirrorOwnerMovement();
}

void AGFBeatHopCourse::OnRep_Placements()
{
	if (HasAuthority())
	{
		return;
	}
	for (const FGFBeatHopPlacementRep& Row : Placements)
	{
		uint16& Seen = AppliedPlacementSeqs.FindOrAdd(Row.PlayerKey);
		if (Row.Seq == 0 || Row.Seq == Seen)
		{
			continue;
		}
		Seen = Row.Seq;
		if (ACharacter* Runner = FindRunner(Row.PlayerKey))
		{
			ApplyPlacement(Runner, Row);
		}
	}
}

void AGFBeatHopCourse::ApplyMoveRep(ACharacter* Character, const FGFBeatHopMoveRep& Row)
{
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move || Row.Phase == 0)
	{
		return;
	}
	Move->MaxWalkSpeed = Row.MaxWalkSpeed;
	Move->GravityScale = Row.GravityScale;
	Move->AirControl = Row.AirControl;
}

void AGFBeatHopCourse::ApplyPlacement(ACharacter* Character, const FGFBeatHopPlacementRep& Row)
{
	if (!Character)
	{
		return;
	}
	const FRotator Rot(0.f, Row.Yaw, 0.f);
	const FVector Dest(Row.Location);
	if (!Character->TeleportTo(Dest, Rot, false, true))
	{
		Character->SetActorLocationAndRotation(Dest, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->Velocity = FVector(Row.Velocity);
		Move->SetMovementMode(Row.bFalling ? MOVE_Falling : MOVE_Walking);
		Move->bJustTeleported = true;
	}
}

void AGFBeatHopCourse::MirrorOwnerMovement()
{
	if (HasAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector Forward = GetActorForwardVector();
	const double Now = GetRunTime();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		ACharacter* Char = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		if (!Char || !Char->IsLocallyControlled())
		{
			continue;
		}
		const FString Id = RunnerId(Char);
		const FGFBeatHopMoveRep* Row = MoveReps.FindByPredicate([&Id](const FGFBeatHopMoveRep& Existing) { return Existing.PlayerKey == Id; });
		if (!Row)
		{
			continue;
		}
		// Phase 2 has to apply after bRunning replicates false, or the owner keeps the chart's gravity.
		if (Row->Phase == 2)
		{
			ApplyMoveRep(Char, *Row);
			continue;
		}
		if (!bRunning || Row->Phase != 1)
		{
			continue;
		}
		ApplyMoveRep(Char, *Row);
		UCharacterMovementComponent* Move = Char->GetCharacterMovement();
		const bool bHeld = Row->HoldBrick != INDEX_NONE && Now < Row->HoldUntilRunTime;
		if (bHeld)
		{
			if (Move)
			{
				Move->Velocity = FVector(0.0, 0.0, Move->Velocity.Z);
			}
			continue;
		}
		Char->AddMovementInput(Forward, 1.f);
	}
}

void AGFBeatHopCourse::ClearTeleportForgiveness()
{
	const double Now = GetServerWorldTime();
	for (auto It = TeleportForgiveUntil.CreateIterator(); It; ++It)
	{
		ACharacter* Char = It.Key().Get();
		if (!Char || Now >= It.Value())
		{
			if (Char)
			{
				if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
				{
					Move->bIgnoreClientMovementErrorChecksAndCorrection = false;
				}
			}
			It.RemoveCurrent();
		}
	}
}

void AGFBeatHopCourse::TeleportRunner(ACharacter* Character, const FString& PlayerKey, const FVector& Dest, const FRotator& Rot, const FVector& Velocity, bool bFalling)
{
	if (!Character)
	{
		return;
	}
	if (!Character->TeleportTo(Dest, Rot, false, true))
	{
		Character->SetActorLocationAndRotation(Dest, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->Velocity = Velocity;
		Move->SetMovementMode(bFalling ? MOVE_Falling : MOVE_Walking);
		Move->bJustTeleported = true;
		// In-flight client moves still describe the old spot. Ignore them briefly so the server doesn't yank the runner back.
		Move->bIgnoreClientMovementErrorChecksAndCorrection = true;
		TeleportForgiveUntil.Add(Character, GetServerWorldTime() + GFBeatHopCourse::TeleportForgiveSeconds);
	}
	PublishPlacement(Character, PlayerKey, Velocity, bFalling);
}

void AGFBeatHopCourse::Crumble(FGFBeatHopBrick& Brick)
{
	Brick.bCrumbled = true;
	UStaticMeshComponent* BrickMesh = Brick.Mesh;
	if (!BrickMesh)
	{
		return;
	}
	BrickMesh->SetVisibility(false);
	BrickMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static const TCHAR* Pieces[] = { TEXT("SM_Block_Beat_Crumble_A"), TEXT("SM_Block_Beat_Crumble_B"), TEXT("SM_Block_Beat_Crumble_C") };
	// Where each chunk sits inside a 100 cm brick (fractions of the brick's half size).
	static const FVector2D Offsets[] = { FVector2D(-0.45, -0.4), FVector2D(0.4, 0.3), FVector2D(-0.05, 0.45) };
	const FTransform BrickXf = BrickMesh->GetRelativeTransform();
	const FVector S = BrickXf.GetScale3D().GetAbs();
	const double LengthCm = S.X * (Brick.Type == EGFBeatNoteType::Hold ? GFBeatHopCourse::ArtHoldLengthCm : GFBeatHopCourse::ArtBrickLengthCm);
	const int32 Sets = FMath::Clamp(FMath::RoundToInt32(LengthCm / 100.0), 1, 6);
	const FLinearColor Color = LaneColor(Brick.Lane);
	FRandomStream Rng(Brick.SourceId * 7919 + Brick.Lane);
	for (int32 Set = 0; Set < Sets; ++Set)
	{
		const double SetX = (Set + 0.5) / Sets * LengthCm - LengthCm * 0.5;
		for (int32 i = 0; i < 3; ++i)
		{
			UStaticMesh* PieceMesh = GFBeatHopCourse::LoadArtMesh(GFBeatHopCourse::ArtPath(TEXT("Bricks"), Pieces[i]));
			if (!PieceMesh)
			{
				return; // no art: the brick just vanishes
			}
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			C->SetupAttachment(Root);
			C->SetStaticMesh(PieceMesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCanEverAffectNavigation(false);
			const FVector Local(SetX + Offsets[i].X * 50.0 * FMath::Min(S.X, 1.0), Offsets[i].Y * 50.0 * S.Y, 0.0);
			C->SetRelativeLocation(BrickXf.GetLocation() + Local);
			C->SetRelativeScale3D(FVector(1.0, 1.0, S.Z));
			if (UMaterialInterface* M = C->GetMaterial(0))
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(M, this);
				Mid->SetVectorParameterValue(GFBeatHopCourse::ColorParam, Color);
				C->SetMaterial(0, Mid);
			}
			C->RegisterComponent();
			FGFBeatHopDebris D;
			D.Mesh = C;
			D.Velocity = FVector(Rng.FRandRange(-60.f, 60.f), Rng.FRandRange(-60.f, 60.f), Rng.FRandRange(0.f, 80.f));
			D.Spin = FRotator(Rng.FRandRange(-180.f, 180.f), Rng.FRandRange(-180.f, 180.f), Rng.FRandRange(-180.f, 180.f));
			D.Life = GFBeatHopCourse::CrumbleSeconds;
			D.StartScale = C->GetRelativeScale3D();
			Debris.Add(D);
		}
	}
}

void AGFBeatHopCourse::Burst(const FVector& WorldTop, const FLinearColor& Color)
{
	UStaticMesh* Cube = GFBeatHopCourse::LoadArtMesh(GFBeatHopCourse::ArtPath(TEXT("VFX"), TEXT("SM_VFX_Cube")));
	if (!Cube || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldTop);
	FRandomStream Rng(static_cast<int32>(WorldTop.X * 13.0 + WorldTop.Y));
	for (int32 i = 0; i < 6; ++i)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		C->SetupAttachment(Root);
		C->SetStaticMesh(Cube);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCanEverAffectNavigation(false);
		C->SetCastShadow(false);
		C->SetRelativeLocation(Local + FVector(0, 0, 10));
		C->SetRelativeScale3D(FVector(1.5));
		C->RegisterComponent();
		const float Angle = (i / 6.f) * 2.f * PI + Rng.FRandRange(-0.3f, 0.3f);
		FGFBeatHopDebris D;
		D.Mesh = C;
		D.Velocity = FVector(FMath::Cos(Angle) * 180.f, FMath::Sin(Angle) * 180.f, Rng.FRandRange(320.f, 460.f));
		D.Spin = FRotator(360.f, 240.f, 0.f);
		D.Life = GFBeatHopCourse::BurstSeconds;
		D.bShrink = true;
		D.StartScale = C->GetRelativeScale3D();
		Debris.Add(D);
	}
}

void AGFBeatHopCourse::TickDebris(float DeltaSeconds)
{
	for (int32 i = Debris.Num() - 1; i >= 0; --i)
	{
		FGFBeatHopDebris& D = Debris[i];
		D.Age += DeltaSeconds;
		if (!D.Mesh || D.Age >= D.Life)
		{
			if (D.Mesh)
			{
				D.Mesh->DestroyComponent();
			}
			Debris.RemoveAtSwap(i);
			continue;
		}
		D.Velocity.Z -= GFBeatHopCourse::Gravity * DeltaSeconds;
		D.Mesh->SetRelativeLocationAndRotation(D.Mesh->GetRelativeLocation() + D.Velocity * DeltaSeconds,
			D.Mesh->GetRelativeRotation() + D.Spin * DeltaSeconds);
		if (D.bShrink)
		{
			D.Mesh->SetRelativeScale3D(D.StartScale * FMath::Max(0.05f, 1.f - D.Age / D.Life));
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Track kit (#201 item 9): gates, checkpoint strips + triggers, catch floor, rails, pillars, scenery.

void AGFBeatHopCourse::ClearTrackKit()
{
	for (UPrimitiveComponent* C : KitComponents)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	KitComponents.Reset();
	CheckpointTriggers.Reset();
	CheckpointMaterials.Reset();
	CheckpointSpots.Reset();
	PulseMaterials.Reset();
	LastCheckpoint.Reset();
	StartGate = nullptr;
	FinishGate = nullptr;
	CatchTiles = nullptr;
	StartTrigger = nullptr;
	FinishTrigger = nullptr;
	ScreenMaterial = nullptr;
	ShownScreenFrame = -1;
	ShownPulse = -1.f;
}

void AGFBeatHopCourse::TrackPulse(UMeshComponent* Mesh)
{
	if (!Mesh)
	{
		return;
	}
	if (UMaterialInterface* M = Mesh->GetMaterial(0))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(M, this);
		Mesh->SetMaterial(0, Mid);
		PulseMaterials.Add(Mid);
	}
}

UStaticMeshComponent* AGFBeatHopCourse::AddKitMesh(const TCHAR* Path, const FTransform& Relative, bool bCollide)
{
	UStaticMesh* Art = GFBeatHopCourse::LoadArtMesh(Path);
	if (!Art)
	{
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	C->SetupAttachment(Root);
	C->SetStaticMesh(Art);
	C->SetRelativeTransform(Relative);
	if (bCollide)
	{
		C->SetCollisionProfileName(TEXT("BlockAll"));
	}
	else
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	C->SetCanEverAffectNavigation(false);
	C->RegisterComponent();
	KitComponents.Add(C);
	return C;
}

UInstancedStaticMeshComponent* AGFBeatHopCourse::AddKitInstances(const TCHAR* Path, const TArray<FTransform>& Relative, bool bCollide)
{
	UStaticMesh* Art = GFBeatHopCourse::LoadArtMesh(Path);
	if (!Art || Relative.Num() == 0)
	{
		return nullptr;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	C->SetupAttachment(Root);
	C->SetStaticMesh(Art);
	if (bCollide)
	{
		C->SetCollisionProfileName(TEXT("BlockAll"));
	}
	else
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	C->SetCanEverAffectNavigation(false);
	C->RegisterComponent();
	for (const FTransform& Xf : Relative)
	{
		C->AddInstance(Xf, false);
	}
	KitComponents.Add(C);
	return C;
}

void AGFBeatHopCourse::BuildTrackKit()
{
	using namespace GFBeatHopCourse;
	ClearTrackKit();
	auto TrackPath = [](const TCHAR* Name) { return ArtPath(TEXT("Track"), Name); };
	auto BrickPath = [](const TCHAR* Name) { return ArtPath(TEXT("Bricks"), Name); };

	// Course span along X (relative): from the front of the first brick to the back of the last.
	const bool bLive = RunMode == EGFBeatHopMode::Live;
	double FirstX = 0.0;
	double LastX = 0.0;
	bool bAny = false;
	for (const FGFBeatHopBrick& B : Bricks)
	{
		if (!B.Mesh)
		{
			continue;
		}
		const double Half = B.Mesh->Bounds.BoxExtent.X; // world == relative (course is never scaled)
		const double X = B.Mesh->GetRelativeLocation().X;
		FirstX = bAny ? FMath::Min(FirstX, X - Half) : X - Half;
		LastX = bAny ? FMath::Max(LastX, X + Half) : X + Half;
		bAny = true;
	}
	if (bLive || !bAny)
	{
		FirstX = 0.0;
		LastX = bLive ? LiveKitLengthCm : 0.0;
	}
	KitStartX = FirstX;
	KitEndX = LastX;
	const int32 Lanes = FMath::Max(1, NumLanes);
	const double HalfWidth = Lanes * LaneWidthCm * 0.5;
	const double BrickZ = -BrickSizeCm.Z;
	const FVector BrickScale(1.0, 1.0, BrickSizeCm.Z / ArtBrickHeightCm);

	// Start pad of neutral bricks with the start gate on it, 1-2 bricks ahead of where players stand.
	auto Pad = [&](double FromX, double ToX)
	{
		TArray<FTransform> Tiles;
		for (double X = FromX + 50.0; X < ToX; X += ArtBrickLengthCm)
		{
			for (int32 L = 0; L < Lanes; ++L)
			{
				Tiles.Add(FTransform(FRotator::ZeroRotator, FVector(X, (L - (Lanes - 1) * 0.5) * LaneWidthCm, BrickZ), BrickScale));
			}
		}
		AddKitInstances(*BrickPath(TEXT("SM_Block_Beat_Neutral")), Tiles, true);
	};
	// Session: the start pad runs back to the lobby floor (which never moves), so there's no gap to fall through.
	const double PadFromX = bSession ? FMath::Min(LobbyEndX, FirstX - 200.0) : FirstX - 200.0 - StartPadCm;
	Pad(PadFromX, FirstX - 200.0);
	// The gate's -X face (countdown screen, coral lamps) faces the players on the pad.
	StartGate = AddKitMesh(*TrackPath(TEXT("SM_Track_StartGate")), FTransform(FVector(FirstX - 450.0, 0.0, 0.0)), true);
	if (StartGate)
	{
		TrackPulse(StartGate);
		if (PulseMaterials.Num() > 0)
		{
			PulseMaterials.Last()->SetVectorParameterValue(EmissiveColorParam, Coral);
		}
		// Slot 1 "Screen": the countdown material (MI_Track_StartGate_Screen, beat_hop_v2) gets a MID whose scalar Frame
		// picks the cell; its EmissiveStrength stays at the MI's 1.5 (#204). Without that material, a dark panel.
		if (StartGate->GetNumMaterials() > 1)
		{
			UMaterialInterface* ScreenBase = StartGate->GetMaterial(1);
			float FrameDefault = 0.f;
			if (ScreenBase && ScreenBase->GetScalarParameterValue(FMaterialParameterInfo(ScreenFrameParam), FrameDefault))
			{
				ScreenMaterial = UMaterialInstanceDynamic::Create(ScreenBase, this);
				StartGate->SetMaterial(1, ScreenMaterial);
				ShownScreenFrame = -1;
			}
			else if (UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
			{
				UMaterialInstanceDynamic* Screen = UMaterialInstanceDynamic::Create(Basic, this);
				Screen->SetVectorParameterValue(ColorParam, FLinearColor(0.02f, 0.02f, 0.03f));
				StartGate->SetMaterial(1, Screen);
			}
		}
	}

	// Finish gate on its own neutral pad past the last brick (songs only: live runs don't end).
	if (!bLive && bAny)
	{
		Pad(LastX + 200.0, LastX + 200.0 + StartPadCm);
		FinishGate = AddKitMesh(*TrackPath(TEXT("SM_Track_FinishGate")), FTransform(FVector(LastX + 400.0, 0.0, 0.0)), true);
		TrackPulse(FinishGate);
	}

	const double KitFrom = (bSession ? FMath::Min(LobbyFromX, PadFromX) : PadFromX) - 400.0;
	const double KitTo = (bAny || bLive ? LastX : FirstX) + 200.0 + StartPadCm + 400.0;

	// Catch floor: 400 cm tiles under the lanes; collision top 12.5 cm above the tile origin.
	{
		TArray<FTransform> Tiles;
		const int32 Cols = FMath::Max(2, FMath::CeilToInt32((HalfWidth * 2.0 + 100.0) / CatchTileCm));
		for (double X = KitFrom + CatchTileCm * 0.5; X < KitTo; X += CatchTileCm)
		{
			for (int32 C = 0; C < Cols; ++C)
			{
				Tiles.Add(FTransform(FVector(X, (C - (Cols - 1) * 0.5) * CatchTileCm, -CatchFloorDepthCm)));
			}
		}
		CatchTiles = AddKitInstances(*TrackPath(TEXT("SM_Track_CatchFloor")), Tiles, true);
	}

	// Rails along both edges at brick-top height, pillars under them. No collision: they're outside the lanes.
	{
		TArray<FTransform> Rails;
		TArray<FTransform> Pillars;
		const double RailY = HalfWidth + 50.0;
		for (double X = KitFrom + RailPieceCm * 0.5; X < KitTo; X += RailPieceCm)
		{
			Rails.Add(FTransform(FRotator::ZeroRotator, FVector(X, RailY, -12.5)));
			Rails.Add(FTransform(FRotator(0.0, 180.0, 0.0), FVector(X, -RailY, -12.5)));
		}
		for (double X = KitFrom + PillarEveryCm * 0.5; X < KitTo; X += PillarEveryCm)
		{
			Pillars.Add(FTransform(FVector(X, RailY, BrickZ - PillarHeightCm)));
			Pillars.Add(FTransform(FVector(X, -RailY, BrickZ - PillarHeightCm)));
		}
		TrackPulse(AddKitInstances(*TrackPath(TEXT("SM_Track_Rail_Side")), Rails, false));
		TrackPulse(AddKitInstances(*TrackPath(TEXT("SM_Track_Pillar")), Pillars, false));
	}

	// Scenery: speakers by the start, floating notes and disco cubes along the sides, skylines far out.
	{
		const double SideY = HalfWidth + 300.0;
		TArray<FTransform> Speakers = {
			FTransform(FRotator(0.0, -90.0, 0.0), FVector(FirstX - 500.0, SideY, -150.0)),
			FTransform(FRotator(0.0, 90.0, 0.0), FVector(FirstX - 500.0, -SideY, -150.0)),
		};
		TArray<FTransform> NotesA, NotesB, Disco, SkyA, SkyB;
		const double DecorTo = FMath::Min(KitTo, KitFrom + 60000.0);
		int32 i = 0;
		for (double X = FirstX + 400.0; X < DecorTo; X += 800.0, ++i)
		{
			const double Side = (i % 2 == 0) ? 1.0 : -1.0;
			const FVector P(X, Side * (SideY + 100.0), 150.0 + 100.0 * FMath::Sin(i * 1.3));
			switch (i % 3)
			{
			case 0: NotesA.Add(FTransform(FRotator(0.0, Side * -90.0, 0.0), P)); break;
			case 1: NotesB.Add(FTransform(FRotator(0.0, Side * -90.0, 0.0), P)); break;
			default: Disco.Add(FTransform(FRotator(0.0, 45.0, 0.0), P)); break;
			}
		}
		i = 0;
		for (double X = KitFrom; X < DecorTo; X += 700.0, ++i)
		{
			(i % 2 == 0 ? SkyA : SkyB).Add(FTransform(FVector(X, 1600.0, -1100.0)));
			(i % 2 == 0 ? SkyB : SkyA).Add(FTransform(FRotator(0.0, 180.0, 0.0), FVector(X + 350.0, -1600.0, -1100.0)));
		}
		TrackPulse(AddKitInstances(*TrackPath(TEXT("SM_Track_Speaker")), Speakers, false));
		TrackPulse(AddKitInstances(*TrackPath(TEXT("SM_Track_FloatingNote_A")), NotesA, false));
		TrackPulse(AddKitInstances(*TrackPath(TEXT("SM_Track_FloatingNote_B")), NotesB, false));
		TrackPulse(AddKitInstances(*TrackPath(TEXT("SM_Track_DiscoCube")), Disco, false));
		TrackPulse(AddKitInstances(*TrackPath(TEXT("SM_Track_Skyline_A")), SkyA, false));
		TrackPulse(AddKitInstances(*TrackPath(TEXT("SM_Track_Skyline_B")), SkyB, false));
	}

	// Checkpoints every CheckpointEveryBeats beats (songs). The strip has no collision of its own (only its pole),
	// so a trigger box across the lanes records the reach. A caught player goes back to the brick nearest the strip.
	if (!bLive && bAny && CheckpointEveryBeats > 0 && Beatmap.Bpm > 0.f)
	{
		const double Speed = static_cast<double>(Beatmap.RefSpeedCms);
		double GapScale = 1.0;
		if (const FGFBeatHopCourseDef* Layout = FGFBeatHopCourses::Find(RunCourseId))
		{
			GapScale = FMath::Max(0.1, static_cast<double>(Layout->GapScale));
		}
		for (int32 Beat = CheckpointEveryBeats; ; Beat += CheckpointEveryBeats)
		{
			const double X = Beatmap.BeatToSeconds(Beat) * Speed * GapScale;
			if (X >= LastX - 150.0)
			{
				break;
			}
			if (X <= FirstX + 150.0)
			{
				continue;
			}
			const int32 Index = CheckpointTriggers.Num();
			UStaticMeshComponent* Strip = AddKitMesh(*TrackPath(TEXT("SM_Track_Checkpoint")), FTransform(FVector(X, 0.0, 0.0)), true);
			UMaterialInstanceDynamic* StripMid = nullptr;
			if (Strip && Strip->GetMaterial(0))
			{
				StripMid = UMaterialInstanceDynamic::Create(Strip->GetMaterial(0), this);
				StripMid->SetScalarParameterValue(EmissiveStrengthParam, 0.f);
				Strip->SetMaterial(0, StripMid);
			}
			CheckpointMaterials.Add(StripMid);

			UBoxComponent* Box = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
			Box->SetupAttachment(Root);
			Box->SetBoxExtent(FVector(60.0, HalfWidth + 50.0, 200.0));
			Box->SetRelativeLocation(FVector(X, 0.0, 150.0));
			Box->SetCollisionProfileName(TEXT("Trigger"));
			Box->SetGenerateOverlapEvents(true);
			Box->SetCanEverAffectNavigation(false);
			Box->OnComponentBeginOverlap.AddDynamic(this, &AGFBeatHopCourse::OnCheckpointOverlap);
			Box->RegisterComponent();
			KitComponents.Add(Box);
			CheckpointTriggers.Add(Box);

			FVector Spot(X, 0.0, 100.0);
			double Best = TNumericLimits<double>::Max();
			for (const FGFBeatHopBrick& B : Bricks)
			{
				if (!B.Mesh)
				{
					continue;
				}
				const FVector L = B.Mesh->GetRelativeLocation();
				const double D = FMath::Abs(L.X - X);
				if (L.X >= X - 100.0 && D < Best)
				{
					Best = D;
					Spot = FVector(L.X, L.Y, L.Z + B.TopOffsetZ + 120.0);
				}
			}
			CheckpointSpots.Add(Spot);
			UE_LOG(LogGF, Verbose, TEXT("Beat Hop: checkpoint %d at beat %d (X %.0f)"), Index, Beat, X);
		}
	}
	// Session triggers (#212): walking through the start gate starts the run; crossing the finish gate ends it.
	if (bSession)
	{
		auto Trigger = [&](double X, const FVector& Extent) -> UBoxComponent*
		{
			UBoxComponent* Box = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
			Box->SetupAttachment(Root);
			Box->SetBoxExtent(Extent);
			Box->SetRelativeLocation(FVector(X, 0.0, Extent.Z - 50.0));
			Box->SetCollisionProfileName(TEXT("Trigger"));
			Box->SetGenerateOverlapEvents(true);
			Box->SetCanEverAffectNavigation(false);
			return Box;
		};
		StartTrigger = Trigger(FirstX - 450.0 + 40.0, FVector(40.0, HalfWidth + 60.0, 250.0));
		StartTrigger->OnComponentBeginOverlap.AddDynamic(this, &AGFBeatHopCourse::OnStartTriggerOverlap);
		StartTrigger->RegisterComponent();
		KitComponents.Add(StartTrigger);
		if (!bLive && bAny)
		{
			FinishTrigger = Trigger(LastX + 400.0, FVector(80.0, HalfWidth + 300.0, 600.0));
			FinishTrigger->OnComponentBeginOverlap.AddDynamic(this, &AGFBeatHopCourse::OnFinishTriggerOverlap);
			FinishTrigger->RegisterComponent();
			KitComponents.Add(FinishTrigger);
		}
	}
	UE_LOG(LogGF, Verbose, TEXT("Beat Hop: track kit %d pieces, %d checkpoints"), KitComponents.Num(), CheckpointTriggers.Num());
}

void AGFBeatHopCourse::OnCheckpointOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACharacter* Char = Cast<ACharacter>(OtherActor);
	if (!Char)
	{
		return;
	}
	const int32 Index = CheckpointTriggers.IndexOfByPredicate([OverlappedComponent](const TObjectPtr<UBoxComponent>& B) { return B == OverlappedComponent; });
	if (Index != INDEX_NONE)
	{
		ReachCheckpoint(Char, Index);
	}
}

void AGFBeatHopCourse::ReachCheckpoint(ACharacter* Character, int32 CheckpointIndex)
{
	if (!Character || !CheckpointSpots.IsValidIndex(CheckpointIndex))
	{
		return;
	}
	int32& Last = LastCheckpoint.FindOrAdd(Character, INDEX_NONE);
	Last = FMath::Max(Last, CheckpointIndex);
	if (CheckpointMaterials.IsValidIndex(CheckpointIndex) && CheckpointMaterials[CheckpointIndex])
	{
		// Mint _E stripes light up once reached (and pulse with the beat from then on).
		UMaterialInstanceDynamic* Mid = CheckpointMaterials[CheckpointIndex];
		Mid->SetVectorParameterValue(GFBeatHopCourse::EmissiveColorParam, GFBeatHopCourse::Mint);
		if (!PulseMaterials.Contains(Mid))
		{
			PulseMaterials.Add(Mid);
			ShownPulse = -1.f;
		}
	}
}

FVector AGFBeatHopCourse::GetRespawnPoint(const ACharacter* Character) const
{
	FVector Local(KitStartX - 200.0 - GFBeatHopCourse::StartPadCm * 0.5, 0.0, 120.0);
	if (const int32* Last = LastCheckpoint.Find(const_cast<ACharacter*>(Character)))
	{
		if (CheckpointSpots.IsValidIndex(*Last))
		{
			Local = CheckpointSpots[*Last];
		}
	}
	return GetActorTransform().TransformPosition(Local);
}

void AGFBeatHopCourse::UpdatePulse(double Now)
{
	if (PulseMaterials.Num() == 0 || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// EmissiveStrength 1..4 on the conductor's beat while a run is on; a steady 120 bpm idle pulse otherwise.
	const double Phase = bRunning ? GetRunTime() / FMath::Max(PeriodSeconds(), 0.05) : Now / 0.5;
	const float Strength = BeatPulseStrength(Phase);
	if (FMath::IsNearlyEqual(Strength, ShownPulse, 0.02f))
	{
		return;
	}
	ShownPulse = Strength;
	for (UMaterialInstanceDynamic* Mid : PulseMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(GFBeatHopCourse::EmissiveStrengthParam, Strength);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Hop clips (#207). Takeoff -> Bhop_Air (loop) -> Bhop_Land / Land_Perfect; Stumble on a miss, then Run_Fast;
// Fall (loop) below the bricks -> Land_Catch on the catch floor -> back to the checkpoint -> idle; Air_Strafe_L/R for
// lane changes; Victory when the run ends, held until the player moves. 0-blend cuts (single-node playback); clips are
// in place, the character movement moves the actor. Run_Fast plays at speed / 600.

void AGFBeatHopCourse::DriveAnims(double Now)
{
	using namespace GFBeatHopCourse;
	const bool bRunEnded = bWasRunning && !bRunning;
	bWasRunning = bRunning;
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS || KitComponents.Num() == 0)
	{
		return;
	}
	const bool bDrawAnims = GetNetMode() != NM_DedicatedServer;
	const double HalfWidth = FMath::Max(1, NumLanes) * LaneWidthCm * 0.5;

	auto Play = [](USkeletalMeshComponent* Mesh, FName Key, float Rate) -> float
	{
		const FGFAnimMapRow* Row = FGFAnimMap::Find(Key);
		UAnimSequenceBase* Clip = Row ? FGFAnimMap::Load(Key) : nullptr;
		if (!Mesh || !Clip)
		{
			return 0.f;
		}
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->PlayAnimation(Clip, Row->bLoop);
		Mesh->SetPlayRate(Rate);
		return Row->bLoop ? 0.f : Clip->GetPlayLength() / FMath::Max(Rate, 0.01f);
	};

	TSet<TWeakObjectPtr<ACharacter>> Seen;
	for (APlayerState* PS : GS->PlayerArray)
	{
		ACharacter* Char = PS ? Cast<ACharacter>(PS->GetPawn()) : nullptr;
		UCharacterMovementComponent* Move = Char ? Char->GetCharacterMovement() : nullptr;
		if (!Move)
		{
			continue;
		}
		const FVector Local = GetActorTransform().InverseTransformPosition(Char->GetActorLocation());
		const bool bOnCourse = Local.X > KitStartX - 2000.0 && Local.X < KitEndX + 2000.0 && FMath::Abs(Local.Y) < HalfWidth + 800.0
			&& Local.Z > -CatchFloorDepthCm - 400.0 && Local.Z < 2000.0;
		FHopAnim* H = HopAnims.Find(Char);
		if (!bOnCourse && !(H && H->bCaught))
		{
			HopAnims.Remove(Char);
			continue;
		}
		Seen.Add(Char);
		if (!H)
		{
			H = &HopAnims.Add(Char);
			H->bWasFalling = Move->IsFalling();
		}
		UGFSimpleAnimComponent* Simple = Char->FindComponentByClass<UGFSimpleAnimComponent>();
		USkeletalMeshComponent* Mesh = Simple ? Simple->FindArtMesh() : Char->GetMesh();
		const FVector Vel = Char->GetVelocity();
		const float Speed = Vel.Size2D();
		const bool bFalling = Move->IsFalling();
		const EGFJudgement Landed = LastLanding.FindRef(Char);
		LastLanding.Remove(Char);
		const FString AnimId = RunnerId(Char);
		const FGFBeatHopAnimRep* AnimRep = FindAnimRep(AnimId);

		auto Set = [&](FName Key, float Rate, bool bOneShot)
		{
			if (HasAuthority())
			{
				PublishAnim(AnimId, Key, Rate, bOneShot);
			}
			if (!bDrawAnims)
			{
				H->Key = Key;
				return;
			}
			const float Len = Play(Mesh, Key, Rate);
			H->Key = Key;
			H->OneShotUntil = bOneShot ? Now + Len : 0.0;
		};

		// Remote pawns play the host's clip. Guessing from replicated movement misses Perfect vs Stumble.
		if (!HasAuthority() && Char->GetLocalRole() == ROLE_SimulatedProxy && AnimRep && !AnimRep->Clip.IsNone())
		{
			const FName Want = ClipForViewer(true, H->Key, AnimRep->Clip);
			if (Want != H->Key)
			{
				const float Len = bDrawAnims ? Play(Mesh, Want, AnimRep->PlayRate) : 0.f;
				H->Key = Want;
				H->OneShotUntil = AnimRep->bOneShot ? Now + Len : 0.0;
			}
			if (Simple && !H->Key.IsNone())
			{
				Simple->PauseFor(0.2f);
			}
			H->bWasFalling = bFalling;
			continue;
		}

		if (H->bCaught)
		{
			// Land_Catch plays out, then the host puts the player back on the last checkpoint (or the start pad).
			if (Now >= H->RespawnAt)
			{
				if (HasAuthority())
				{
					RespawnCharacter(Char);
				}
				H->bCaught = false;
				H->Key = NAME_None;
				H->bWasFalling = Move->IsFalling();
				continue; // released: SimpleAnim idles
			}
			if (Simple)
			{
				Simple->PauseFor(0.2f);
			}
			continue;
		}

		// A hired Ripwing catches a real drop. A hop that stays on the bricks never gets this low.
		// With no hire, NotifyFall does nothing and the catch floor below still does its own job.
		if (HasAuthority() && bRunning && bFalling && Local.Z < -FallBelowTopCm && !HoldUntil.Contains(Char))
		{
			if (NotifyFall(Char, RunnerId(Char)))
			{
				H->bWasFalling = false;
				continue;
			}
		}

		if (bRunEnded && !bFalling)
		{
			Set(AnimVictory, 1.f, false);
			H->bVictory = true;
		}

		if (bFalling && !H->bWasFalling)
		{
			H->bVictory = false;
			if (Vel.Z > 50.f)
			{
				Set(AnimTakeoff, 1.f, true);
			}
		}
		else if (!bFalling && H->bWasFalling)
		{
			const UPrimitiveComponent* Floor = Move->CurrentFloor.HitResult.GetComponent();
			if (Floor && Floor == CatchTiles)
			{
				Set(AnimLandCatch, 1.f, true);
				H->bCaught = true;
				H->RespawnAt = FMath::Max(H->OneShotUntil, Now + 0.5);
			}
			else
			{
				FName Clip = AnimLand;
				if (Landed == EGFJudgement::Perfect)
				{
					Clip = AnimLandPerfect;
				}
				else if (Landed == EGFJudgement::Miss)
				{
					Clip = AnimStumble;
				}
				if (!HasAuthority())
				{
					Clip = ClipForViewer(false, Clip, AnimRep ? AnimRep->Clip : NAME_None);
				}
				Set(Clip, 1.f, true);
			}
		}
		else if (!HasAuthority() && AnimRep && Now < H->OneShotUntil)
		{
			// The judgement arrived during the land: swap a generic Land for the host's Perfect or Stumble.
			const FName Better = ClipForViewer(false, H->Key, AnimRep->Clip);
			if (Better != H->Key)
			{
				Set(Better, AnimRep->PlayRate, true);
			}
		}
		H->bWasFalling = bFalling;

		if (Now < H->OneShotUntil && !(bFalling && Local.Z < -FallBelowTopCm && H->Key != AnimFall))
		{
			// a one-shot is playing (falling off the course cuts it short)
		}
		else if (bFalling)
		{
			FName Want = AnimAir;
			if (Local.Z < -FallBelowTopCm)
			{
				Want = AnimFall;
			}
			else if (FMath::Abs(Vel.Y) > StrafeSpeedCms)
			{
				// Lanes run along +Y; facing +X, +Y is to the player's right.
				Want = Vel.Y > 0.f ? AnimStrafeR : AnimStrafeL;
			}
			if (Want != H->Key)
			{
				Set(Want, 1.f, false);
			}
		}
		else if (H->bVictory && Speed <= MoveSpeedCms)
		{
			// Victory holds its last frame until the player moves.
		}
		else if (Speed > MoveSpeedCms)
		{
			H->bVictory = false;
			if (H->Key != AnimRunFast)
			{
				Set(AnimRunFast, AnimRate(AnimRunFast, Speed), false);
			}
			else if (bDrawAnims && Mesh)
			{
				Mesh->SetPlayRate(AnimRate(AnimRunFast, Speed));
			}
		}
		else
		{
			// Standing still on the course: SimpleAnim's idle takes over.
			H->Key = NAME_None;
			continue;
		}
		if (Simple && !H->Key.IsNone())
		{
			Simple->PauseFor(0.2f);
		}
	}
	for (auto It = HopAnims.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Session (#212): lobby with song and difficulty tiles, start trigger, run movement, finish, results, back to the hub.

FString AGFBeatHopCourse::SongTitle(FName SongId)
{
	for (const EGFBeatDifficulty D : { EGFBeatDifficulty::Easy, EGFBeatDifficulty::Normal, EGFBeatDifficulty::Hard })
	{
		if (const FGFBeatmap* Map = FGFBuiltInSongs::Find(SongId, D))
		{
			if (!Map->Title.IsEmpty())
			{
				return Map->Title;
			}
		}
	}
	return SongId.ToString();
}

FString AGFBeatHopCourse::DifficultyName(EGFBeatDifficulty Difficulty)
{
	switch (Difficulty)
	{
	case EGFBeatDifficulty::Easy: return TEXT("Easy");
	case EGFBeatDifficulty::Normal: return TEXT("Normal");
	case EGFBeatDifficulty::Hard: return TEXT("Hard");
	default: return TEXT("?");
	}
}

void AGFBeatHopCourse::BeginSession()
{
	if (!HasAuthority())
	{
		return;
	}
	if (!bSession)
	{
		bSession = true;
		SetReplicatingMovement(true);
		// Up in the sky: clear of the voxel terrain, the map's own spawn and the gate sign on the ground.
		SetActorLocationAndRotation(FVector(0.0, 0.0, SessionHeightCm), FRotator::ZeroRotator);
		const TArray<FName> Ids = FGFBuiltInSongs::SongIds();
		if (SelectedSong.IsNone() || !Ids.Contains(SelectedSong))
		{
			SelectedSong = FGFBeatHopMusic::DefaultSong(bDemoDefaultsToFallback);
		}
		SelectedDifficulty = EGFBeatDifficulty::Easy;
		if (!FGFBeatHopCourses::Find(SelectedCourseId))
		{
			SelectedCourseId = FGFBeatHopCourses::Default().Id;
		}
		BuildLobby();
		RebuildPreview();
		const FGFBeatHopCourseDef* Layout = FGFBeatHopCourses::Find(SelectedCourseId);
		UE_LOG(LogGF, Log, TEXT("Beat Hop: session on, %s, %s (%s) ready at the start gate"),
			Layout ? *Layout->Title : TEXT("Fern Lane"), *SongTitle(SelectedSong), *DifficultyName(SelectedDifficulty));
	}
	ForEachPlayerPawn([this](APawn* Pawn)
	{
		if (IsLostFromCourse(Pawn))
		{
			PlaceInLobby(Pawn);
		}
	});
}

void AGFBeatHopCourse::OnRep_Session()
{
	if (!bSession)
	{
		return;
	}
	if (LobbyComponents.Num() == 0)
	{
		BuildLobby();
		if (RunSongId.IsNone())
		{
			BuildTrackKit(); // the start pad reaches back to the lobby once the session is on
		}
		else
		{
			OnRep_RunSpec();
		}
	}
	UpdateLobbyLook();
}

UTextRenderComponent* AGFBeatHopCourse::AddLobbyText(const FString& Text, const FVector& Relative, float Size, const FColor& Color)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
	T->SetupAttachment(Root);
	T->SetText(FText::FromString(Text));
	T->SetWorldSize(Size);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetTextRenderColor(Color);
	// Text faces +X by default; the lobby looks toward +X (the gate), so turn it round to face the players.
	T->SetRelativeLocationAndRotation(Relative, FRotator(0.0, 180.0, 0.0));
	T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	T->SetCanEverAffectNavigation(false);
	T->RegisterComponent();
	LobbyComponents.Add(T);
	return T;
}

void AGFBeatHopCourse::BuildLobby()
{
	using namespace GFBeatHopCourse;
	if (LobbyComponents.Num() > 0)
	{
		return;
	}
	SongTileIds = FGFBuiltInSongs::SongIds();
	CourseTileIds.Reset();
	for (const FGFBeatHopCourseDef& Course : FGFBeatHopCourses::All())
	{
		CourseTileIds.Add(Course.Id);
	}
	SongTiles.Reset();
	DifficultyTiles.Reset();
	CourseTiles.Reset();
	RescueTiles.Reset();
	SongTileMaterials.Reset();
	DifficultyTileMaterials.Reset();
	CourseTileMaterials.Reset();
	RescueTileMaterial = nullptr;

	// Floor: neutral bricks five lanes wide (wide enough for Hard), or one engine-cube slab if the art is missing.
	{
		TArray<FTransform> Tiles;
		const FVector BrickScale(1.0, 1.0, BrickSizeCm.Z / ArtBrickHeightCm);
		for (double X = LobbyFromX + 50.0; X < LobbyEndX; X += ArtBrickLengthCm)
		{
			for (int32 C = -2; C <= 2; ++C)
			{
				Tiles.Add(FTransform(FRotator::ZeroRotator, FVector(X, C * 100.0, -BrickSizeCm.Z), BrickScale));
			}
		}
		if (UInstancedStaticMeshComponent* Floor = AddKitInstances(*ArtPath(TEXT("Bricks"), TEXT("SM_Block_Beat_Neutral")), Tiles, true))
		{
			KitComponents.Remove(Floor); // the lobby outlives track-kit rebuilds
			LobbyComponents.Add(Floor);
		}
		else if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath))
		{
			UStaticMeshComponent* Slab = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			Slab->SetupAttachment(Root);
			Slab->SetStaticMesh(Cube);
			Slab->SetRelativeLocation(FVector((LobbyFromX + LobbyEndX) * 0.5, 0.0, -50.0));
			Slab->SetRelativeScale3D(FVector((LobbyEndX - LobbyFromX) / 100.0, 5.0, 1.0));
			Slab->SetCollisionProfileName(TEXT("BlockAll"));
			Slab->SetCanEverAffectNavigation(false);
			Slab->RegisterComponent();
			LobbyComponents.Add(Slab);
		}
	}

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubePath);
	UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
	auto Tile = [&](double X, double Y, const FString& Label, TArray<TObjectPtr<UBoxComponent>>& Boxes, TArray<TObjectPtr<UMaterialInstanceDynamic>>& Mids)
	{
		UStaticMeshComponent* Plate = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Plate->SetupAttachment(Root);
		if (Cube)
		{
			Plate->SetStaticMesh(Cube);
		}
		Plate->SetRelativeLocation(FVector(X, Y, 3.0)); // engine cube: centred, 100 cm; a 6 cm plate on the floor
		Plate->SetRelativeScale3D(FVector(1.2, 1.2, 0.06));
		Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Plate->SetCanEverAffectNavigation(false);
		UMaterialInstanceDynamic* Mid = Basic ? UMaterialInstanceDynamic::Create(Basic, this) : nullptr;
		if (Mid)
		{
			Plate->SetMaterial(0, Mid);
		}
		Plate->RegisterComponent();
		LobbyComponents.Add(Plate);
		Mids.Add(Mid);

		UBoxComponent* Box = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
		Box->SetupAttachment(Root);
		Box->SetBoxExtent(FVector(60.0, 60.0, 100.0));
		Box->SetRelativeLocation(FVector(X, Y, 100.0));
		Box->SetCollisionProfileName(TEXT("Trigger"));
		Box->SetGenerateOverlapEvents(true);
		Box->SetCanEverAffectNavigation(false);
		Box->OnComponentBeginOverlap.AddDynamic(this, &AGFBeatHopCourse::OnLobbyTileOverlap);
		Box->RegisterComponent();
		LobbyComponents.Add(Box);
		Boxes.Add(Box);
		AddLobbyText(Label, FVector(X, Y, 175.0), 22.f, FColor::White);
	};

	const int32 NumSongs = SongTileIds.Num();
	for (int32 i = 0; i < NumSongs; ++i)
	{
		Tile(SongRowX, (i - (NumSongs - 1) * 0.5) * TileSpacingY, SongTitle(SongTileIds[i]), SongTiles, SongTileMaterials);
	}
	const EGFBeatDifficulty Diffs[] = { EGFBeatDifficulty::Easy, EGFBeatDifficulty::Normal, EGFBeatDifficulty::Hard };
	for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(Diffs)); ++i)
	{
		Tile(DifficultyRowX, (i - 1) * TileSpacingY, DifficultyName(Diffs[i]), DifficultyTiles, DifficultyTileMaterials);
	}
	const int32 NumCourses = CourseTileIds.Num();
	for (int32 i = 0; i < NumCourses; ++i)
	{
		const FGFBeatHopCourseDef* Course = FGFBeatHopCourses::Find(CourseTileIds[i]);
		Tile(CourseRowX, (i - (NumCourses - 1) * 0.5) * TileSpacingY, Course ? Course->Title : CourseTileIds[i].ToString(), CourseTiles, CourseTileMaterials);
	}
	Tile(HireRowX, 0.0, TEXT("Hire Ripwing"), RescueTiles, CourseTileMaterials);
	// The hire plate's material was appended onto the course list. Keep it on its own so the course row stays one-to-one.
	if (CourseTileMaterials.Num() == NumCourses + 1)
	{
		RescueTileMaterial = CourseTileMaterials.Last();
		CourseTileMaterials.RemoveAt(CourseTileMaterials.Num() - 1);
	}
	AddLobbyText(TEXT("SONG"), FVector(SongRowX, 0.0, 250.0), 34.f, Sunflower.ToFColor(true));
	AddLobbyText(TEXT("DIFFICULTY"), FVector(DifficultyRowX, 0.0, 250.0), 34.f, Mint.ToFColor(true));
	AddLobbyText(TEXT("COURSE"), FVector(CourseRowX, 0.0, 250.0), 34.f, Coral.ToFColor(true));
	AddLobbyText(TEXT("BEAT HOP"), FVector(-600.0, 0.0, 430.0), 80.f, Lavender.ToFColor(true));
	StatusText = AddLobbyText(TEXT(""), FVector(-600.0, 0.0, 350.0), 30.f, FColor::White);
	UpdateLobbyLook();
}

void AGFBeatHopCourse::UpdateLobbyLook()
{
	using namespace GFBeatHopCourse;
	for (int32 i = 0; i < SongTileMaterials.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* Mid = SongTileMaterials[i])
		{
			const bool bOn = SongTileIds.IsValidIndex(i) && SongTileIds[i] == SelectedSong;
			Mid->SetVectorParameterValue(ColorParam, bOn ? LaneColor(i + 1) : TileOff);
		}
	}
	const FLinearColor DiffColors[] = { Mint, Sunflower, Coral };
	for (int32 i = 0; i < DifficultyTileMaterials.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* Mid = DifficultyTileMaterials[i])
		{
			const bool bOn = static_cast<int32>(SelectedDifficulty) == i;
			Mid->SetVectorParameterValue(ColorParam, bOn ? DiffColors[i % 3] : TileOff);
		}
	}
	for (int32 i = 0; i < CourseTileMaterials.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* Mid = CourseTileMaterials[i])
		{
			const FGFBeatHopCourseDef* Course = CourseTileIds.IsValidIndex(i) ? FGFBeatHopCourses::Find(CourseTileIds[i]) : nullptr;
			const bool bOn = Course && Course->Id == SelectedCourseId;
			Mid->SetVectorParameterValue(ColorParam, bOn && Course ? Course->Tint : TileOff);
		}
	}
	if (RescueTileMaterial)
	{
		RescueTileMaterial->SetVectorParameterValue(ColorParam, Rescues.Hires.Num() > 0 ? Mint : TileOff);
	}
	if (StatusText)
	{
		const FGFBeatHopCourseDef* Course = FGFBeatHopCourses::Find(bRunning ? RunCourseId : SelectedCourseId);
		const FString CourseTitle = Course ? Course->Title : TEXT("Fern Lane");
		FString Line;
		if (IsShowingResults())
		{
			Line = bLastPassed ? TEXT("Course passed! Back to the hub in a moment") : TEXT("Not this time. Back to the hub in a moment");
		}
		else if (bRunning)
		{
			Line = FString::Printf(TEXT("%s, %s (%s) is on: hop!"), *CourseTitle, *SongTitle(RunSongId), *DifficultyName(RunDifficulty));
		}
		else
		{
			Line = FString::Printf(TEXT("%s, %s (%s): run through the gate"), *CourseTitle, *SongTitle(SelectedSong), *DifficultyName(SelectedDifficulty));
		}
		StatusText->SetText(FText::FromString(Line));
	}
}

void AGFBeatHopCourse::RebuildPreview()
{
	if (bRunning)
	{
		return;
	}
	const FGFBeatmap* Map = FGFBuiltInSongs::Find(SelectedSong, SelectedDifficulty);
	if (!Map)
	{
		BuildTrackKit();
		UpdateLobbyLook();
		return;
	}
	// The selected song's course stands ready (bricks idle), so the gate is where the run will start.
	RunMode = EGFBeatHopMode::Songs;
	RunSongId = SelectedSong;
	RunDifficulty = SelectedDifficulty;
	RunCourseId = SelectedCourseId.IsNone() || !FGFBeatHopCourses::Find(SelectedCourseId)
		? FGFBeatHopCourses::Default().Id
		: SelectedCourseId;
	Windows = FGFJudgeWindows::For(EGFBeatHopMode::Songs, SelectedDifficulty);
	BuildFromBeatmap(*Map);
	UpdateLobbyLook();
}

bool AGFBeatHopCourse::SelectSong(FName SongId)
{
	if (!HasAuthority() || bRunning || IsShowingResults() || !FGFBuiltInSongs::SongIds().Contains(SongId))
	{
		return false;
	}
	if (SongId != SelectedSong || RunSongId != SongId)
	{
		SelectedSong = SongId;
		if (!FGFBuiltInSongs::Find(SelectedSong, SelectedDifficulty))
		{
			SelectedDifficulty = EGFBeatDifficulty::Easy;
		}
		RebuildPreview();
		UE_LOG(LogGF, Log, TEXT("Beat Hop: song %s"), *SongTitle(SelectedSong));
	}
	return true;
}

bool AGFBeatHopCourse::SelectDifficulty(EGFBeatDifficulty Difficulty)
{
	if (!HasAuthority() || bRunning || IsShowingResults() || !FGFBuiltInSongs::Find(SelectedSong, Difficulty))
	{
		return false;
	}
	if (Difficulty != SelectedDifficulty || RunDifficulty != Difficulty)
	{
		SelectedDifficulty = Difficulty;
		RebuildPreview();
		UE_LOG(LogGF, Log, TEXT("Beat Hop: difficulty %s"), *DifficultyName(SelectedDifficulty));
	}
	return true;
}

void AGFBeatHopCourse::OnLobbyTileOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!HasAuthority() || !Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}
	for (int32 i = 0; i < SongTiles.Num(); ++i)
	{
		if (SongTiles[i] == OverlappedComponent && SongTileIds.IsValidIndex(i))
		{
			SelectSong(SongTileIds[i]);
			return;
		}
	}
	for (int32 i = 0; i < DifficultyTiles.Num(); ++i)
	{
		if (DifficultyTiles[i] == OverlappedComponent)
		{
			SelectDifficulty(static_cast<EGFBeatDifficulty>(i));
			return;
		}
	}
	for (int32 i = 0; i < CourseTiles.Num(); ++i)
	{
		if (CourseTiles[i] == OverlappedComponent && CourseTileIds.IsValidIndex(i))
		{
			SelectCourse(CourseTileIds[i]);
			return;
		}
	}
	for (UBoxComponent* RescueTile : RescueTiles)
	{
		if (RescueTile == OverlappedComponent)
		{
			FString Key;
			if (const AGFPlayerState* GFPS = Pawn->GetPlayerState<AGFPlayerState>())
			{
				Key = GFPS->PlayerKey;
			}
			if (Key.IsEmpty())
			{
				Key = Pawn->GetName();
			}
			FString Why;
			TryHireRescue(Key, Why);
			return;
		}
	}
}

void AGFBeatHopCourse::OnStartTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (HasAuthority() && Pawn && Pawn->IsPlayerControlled() && !bRunning && !IsShowingResults())
	{
		PendingStart = Pawn; // started next tick: starting rebuilds the kit, this trigger included
	}
}

void AGFBeatHopCourse::OnFinishTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (HasAuthority() && Pawn && Pawn->IsPlayerControlled() && bRunning)
	{
		PendingFinish = Pawn;
	}
}

void AGFBeatHopCourse::UpdateMusic()
{
	const FString Path = bRunning && RunMode == EGFBeatHopMode::Songs ? FGFBeatHopMusic::SoundPathFor(RunSongId) : FString();
	if (Path.IsEmpty())
	{
		StopMusic();
		return;
	}
	if (MusicRunStart == RunStartServerTime)
	{
		return; // this run's song already started (each run plays it once, from its own clock)
	}
	const double T = GetRunTime();
	if (T < 0.0)
	{
		return; // before the run clock's zero: the song starts on time
	}
	StopMusic();
	MusicRunStart = RunStartServerTime;
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn);
	if (!Sound)
	{
		UE_LOG(LogGF, Warning, TEXT("Beat Hop: song audio %s is missing; the run follows its beat map without it"), *Path);
		return;
	}
	MusicSongId = RunSongId;
	if (T < Sound->GetDuration())
	{
		// Null without an audio device (dedicated server, -nosound): the beat map still runs the course.
		Music = UGameplayStatics::CreateSound2D(this, Sound, 1.f, 1.f, 0.f, nullptr, false, false);
		if (Music)
		{
			Music->Play(static_cast<float>(T));
		}
	}
}

void AGFBeatHopCourse::StopMusic()
{
	if (Music)
	{
		Music->Stop();
		Music = nullptr;
	}
	MusicSongId = NAME_None;
	MusicRunStart = -1.0;
}

void AGFBeatHopCourse::SwitchToFallbackSong(const FString& Why)
{
	if (!HasAuthority() || !bRunning || RunMode != EGFBeatHopMode::Live)
	{
		return;
	}
	if (UGFBeatClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr)
	{
		Clock->StopClock();
	}
	// The live run never got going (no bricks, nothing judged), so it has no results or rewards.
	bRunning = false;
	FString Reason;
	if (!StartRunForPlayers(FGFBeatHopMusic::FallbackSongId(), RunDifficulty, EGFBeatHopMode::Songs, bRunCozy, CurrentPlayerCount(), Reason))
	{
		UE_LOG(LogGF, Warning, TEXT("Beat Hop: the fallback song could not start: %s"), *Reason);
		EndReason = Why;
		OnRunEnded.Broadcast(Why);
		return;
	}
	// Bricks sit at (run time x speed) along X, so the song clock starts where the runner is, like a gate start.
	APawn* Runner = nullptr;
	ForEachPlayerPawn([&Runner](APawn* Pawn)
	{
		Runner = Runner ? Runner : Pawn;
	});
	if (Runner)
	{
		RunStartServerTime -= RunTimeAtLocalX(GetActorTransform().InverseTransformPosition(Runner->GetActorLocation()).X);
	}
	FallbackReason = Why;
	UE_LOG(LogGF, Log, TEXT("Beat Hop: %s"), *Why);
}

double AGFBeatHopCourse::RunTimeAtLocalX(double LocalX) const
{
	const double Speed = FMath::Max(1.0, static_cast<double>(Beatmap.RefSpeedCms));
	double Scale = 1.0;
	if (RunMode != EGFBeatHopMode::Live)
	{
		if (const FGFBeatHopCourseDef* Layout = FGFBeatHopCourses::Find(RunCourseId))
		{
			Scale = FMath::Max(0.1, static_cast<double>(Layout->GapScale));
		}
	}
	return (LocalX / Scale) / Speed;
}

bool AGFBeatHopCourse::TryStartFromGate(APawn* Pawn, FString& OutReason)
{
	OutReason.Reset();
	PendingStart.Reset();
	if (!HasAuthority())
	{
		OutReason = TEXT("Only the host can start a run.");
		return false;
	}
	if (bRunning)
	{
		OutReason = TEXT("A run is already on.");
		return false;
	}
	if (IsShowingResults() || bTravelled)
	{
		OutReason = TEXT("The results are up: back to the hub in a moment.");
		return false;
	}
	FName Song = SelectedSong;
	if (Song.IsNone())
	{
		Song = FGFBeatHopMusic::DefaultSong(bDemoDefaultsToFallback);
	}
	if (!StartRun(Song, SelectedDifficulty, EGFBeatHopMode::Songs, false, OutReason))
	{
		UE_LOG(LogGF, Warning, TEXT("Beat Hop: the gate could not start %s: %s"), *Song.ToString(), *OutReason);
		return false;
	}

	// Bricks sit at (run time x speed) along X, so starting the clock at the crossing point puts the runner on time.
	FVector StartLocal = FVector::ZeroVector;
	if (Pawn)
	{
		StartLocal = GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
		RunStartServerTime -= RunTimeAtLocalX(StartLocal.X);
	}

	// Everyone in the session gets a result line, even if they never land on a brick.
	if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const AGFPlayerState* GFPS = Cast<AGFPlayerState>(PS);
			if (GFPS && !GFPS->PlayerKey.IsEmpty())
			{
				RunFor(GFPS->PlayerKey);
			}
		}
	}

	// Co-op: everyone else starts alongside the one who walked through, one lane over each.
	int32 Slot = 0;
	ForEachPlayerPawn([&](APawn* Other)
	{
		if (Other != Pawn && Pawn)
		{
			++Slot;
			const double Y = ((Slot % 2) ? 1.0 : -1.0) * ((Slot + 1) / 2) * LaneWidthCm;
			const FVector Dest = GetActorTransform().TransformPosition(FVector(StartLocal.X, Y, StartLocal.Z));
			if (ACharacter* OtherChar = Cast<ACharacter>(Other))
			{
				const AGFPlayerState* GFPS = OtherChar->GetPlayerState<AGFPlayerState>();
				TeleportRunner(OtherChar, GFPS ? GFPS->PlayerKey : FString(), Dest, GetActorRotation(), FVector::ZeroVector, false);
			}
			else
			{
				Other->TeleportTo(Dest, GetActorRotation(), false, false);
			}
		}
		if (ACharacter* Char = Cast<ACharacter>(Other))
		{
			ApplyRunMovement(Char);
		}
	});
	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		ApplyRunMovement(Char);
	}
	HoldUntil.Reset();
	HoldBrick.Reset();
	RefreshHazards(Pawn);
	UpdateLobbyLook();
	const FGFBeatHopCourseDef* Layout = FGFBeatHopCourses::Find(RunCourseId);
	UE_LOG(LogGF, Log, TEXT("Beat Hop: run started at the gate: %s on %s (%s), clock %.2f s, song ends at %.2f s"),
		*SongTitle(RunSongId), Layout ? *Layout->Title : TEXT("Fern Lane"), *DifficultyName(RunDifficulty), GetRunTime(), EndTime);
	return true;
}

bool AGFBeatHopCourse::ReachFinish(APawn* Pawn)
{
	PendingFinish.Reset();
	if (!HasAuthority() || !bRunning)
	{
		return false;
	}
	UE_LOG(LogGF, Log, TEXT("Beat Hop: %s crossed the finish at %.2f s"), Pawn ? *Pawn->GetName() : TEXT("someone"), GetRunTime());
	bCoursePassed = true;
	FinishRun();
	return true;
}

void AGFBeatHopCourse::ApplyRunMovement(ACharacter* Character)
{
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	if (!SavedMoves.Contains(Character))
	{
		FSavedRunMove Saved;
		Saved.MaxWalkSpeed = Move->MaxWalkSpeed;
		Saved.GravityScale = Move->GravityScale;
		Saved.AirControl = Move->AirControl;
		SavedMoves.Add(Character, Saved);
	}
	// Run at the chart's speed, and scale gravity so one ordinary hop lasts one beat: hop on the beat, land on the next brick.
	Move->MaxWalkSpeed = FMath::Max(100.f, Beatmap.RefSpeedCms);
	const double Period = PeriodSeconds();
	const float Gravity = FMath::Abs(GetWorld() ? GetWorld()->GetGravityZ() : -GFBeatHopCourse::Gravity);
	if (Period > 0.0 && Gravity > 0.f && Move->JumpZVelocity > 0.f)
	{
		Move->GravityScale = FMath::Clamp(2.f * Move->JumpZVelocity / (Gravity * static_cast<float>(Period)), 0.5f, 4.f);
	}
	Move->AirControl = 1.f;
	PublishMove(Character, 1);
}

bool AGFBeatHopCourse::HasRunMovement(const ACharacter* Character) const
{
	return SavedMoves.Contains(const_cast<ACharacter*>(Character));
}

void AGFBeatHopCourse::RestoreRunMovement()
{
	for (const TPair<TWeakObjectPtr<ACharacter>, FSavedRunMove>& Pair : SavedMoves)
	{
		ACharacter* Char = Pair.Key.Get();
		UCharacterMovementComponent* Move = Char ? Char->GetCharacterMovement() : nullptr;
		if (Move)
		{
			Move->MaxWalkSpeed = Pair.Value.MaxWalkSpeed;
			Move->GravityScale = Pair.Value.GravityScale;
			Move->AirControl = Pair.Value.AirControl;
			PublishMove(Char, 2);
		}
	}
	SavedMoves.Reset();
}

void AGFBeatHopCourse::RespawnCharacter(ACharacter* Character)
{
	if (!Character)
	{
		return;
	}
	if (bSession && !bRunning)
	{
		PlaceInLobby(Character);
		return;
	}
	if (bSession && bRunning)
	{
		const AGFPlayerState* GFPS = Character->GetPlayerState<AGFPlayerState>();
		PutBackMidRun(Character, GFPS ? GFPS->PlayerKey : FString());
		return;
	}
	const AGFPlayerState* GFPS = Character->GetPlayerState<AGFPlayerState>();
	TeleportRunner(Character, GFPS ? GFPS->PlayerKey : FString(), GetRespawnPoint(Character), Character->GetActorRotation(), FVector::ZeroVector, false);
}

void AGFBeatHopCourse::PutBackMidRun(ACharacter* Character, const FString& PlayerKey)
{
	if (!Character)
	{
		return;
	}
	// Back onto the next brick the song hasn't reached yet; stand there until it does, then carry on in time.
	const double Now = GetRunTime();
	const double Lead = PeriodSeconds();
	for (int32 i = 0; i < Bricks.Num(); ++i)
	{
		const FGFBeatHopBrick& B = Bricks[i];
		if (B.Mesh && !B.bCrumbled && B.Time >= Now + Lead)
		{
			const int32 Index = BrickBase + i;
			const FVector Top = GetActorTransform().TransformPosition(B.Mesh->GetRelativeLocation() + FVector(0.0, 0.0, B.TopOffsetZ + 100.0));
			const FRotator Facing(0.0, GetActorRotation().Yaw, 0.0);
			TeleportRunner(Character, PlayerKey, Top, Facing, FVector::ZeroVector, false);
			HoldUntil.Add(Character, B.Time);
			HoldBrick.Add(Character, Index);
			PublishHold(Character, PlayerKey, Index, B.Time);
			if (!PlayerKey.IsEmpty())
			{
				// The brick you're put back on is a free one: landing on it isn't judged and it never sweeps as a Miss.
				RunFor(PlayerKey).Judged.Add(Index);
			}
			return;
		}
	}
	// Nothing left to hop on: straight to the finish pad.
	TeleportRunner(Character, PlayerKey, GetActorTransform().TransformPosition(FVector(KitEndX + 250.0, 0.0, 120.0)),
		FRotator(0.0, GetActorRotation().Yaw, 0.0), FVector::ZeroVector, false);
}

int32 AGFBeatHopCourse::GetHoldBrick(const ACharacter* Character) const
{
	const int32* Index = HoldBrick.Find(const_cast<ACharacter*>(Character));
	return Index ? *Index : INDEX_NONE;
}

FVector AGFBeatHopCourse::GetRespawnHopVelocity(const ACharacter* Character) const
{
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		return FVector::ZeroVector;
	}
	// One ordinary hop lasts one beat at run speed (ApplyRunMovement tunes gravity for that), so this reaches the next brick.
	const double Speed = FMath::Max(100.0, static_cast<double>(Move->MaxWalkSpeed));
	return GetActorForwardVector().GetSafeNormal2D() * Speed + FVector(0.0, 0.0, Move->JumpZVelocity);
}

void AGFBeatHopCourse::ReleaseHold(ACharacter* Character, bool bAlreadyHopping)
{
	HoldUntil.Remove(Character);
	HoldBrick.Remove(Character);
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		PublishHold(Character, RunnerId(Character), INDEX_NONE, -1.0);
		return;
	}
	const FVector Hop = GetRespawnHopVelocity(Character);
	if (bAlreadyHopping)
	{
		// They jumped on their own: keep their lift, give them the run speed they were missing.
		Move->Velocity = FVector(Hop.X, Hop.Y, Move->Velocity.Z);
	}
	else
	{
		Move->Velocity = Hop;
		Move->SetMovementMode(MOVE_Falling);
	}
	const FString Id = RunnerId(Character);
	PublishHold(Character, Id, INDEX_NONE, -1.0);
	PublishPlacement(Character, Id, Move->Velocity, true);
	Move->bJustTeleported = true;
}

FVector AGFBeatHopCourse::GetLobbySpawn() const
{
	return GetActorTransform().TransformPosition(FVector(GFBeatHopCourse::LobbySpawnX, 0.0, 110.0));
}

bool AGFBeatHopCourse::IsLostFromCourse(const APawn* Pawn) const
{
	using namespace GFBeatHopCourse;
	if (!Pawn)
	{
		return false;
	}
	const FVector L = GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
	const double HalfWidth = FMath::Max(5, NumLanes) * LaneWidthCm * 0.5;
	const double FromX = (bSession ? LobbyFromX : KitStartX - 200.0 - StartPadCm) - LostMarginCm;
	const double ToX = KitEndX + 200.0 + StartPadCm + LostMarginCm;
	if (L.X < FromX || L.X > ToX || FMath::Abs(L.Y) > HalfWidth + LostMarginCm || L.Z > 4000.0)
	{
		return true;
	}
	// Mid-run the catch floor and the hop clips deal with falls; otherwise anything below the pad is off it.
	const double Floor = bRunning ? -CatchFloorDepthCm - LostMarginCm : -FallBelowTopCm - 250.0;
	return L.Z < Floor;
}

void AGFBeatHopCourse::PlaceInLobby(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	const FRotator Facing(0.0, GetActorRotation().Yaw, 0.0);
	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		const AGFPlayerState* GFPS = Char->GetPlayerState<AGFPlayerState>();
		TeleportRunner(Char, GFPS ? GFPS->PlayerKey : FString(), GetLobbySpawn(), Facing, FVector::ZeroVector, false);
	}
	else if (!Pawn->TeleportTo(GetLobbySpawn(), Facing, false, false))
	{
		Pawn->SetActorLocationAndRotation(GetLobbySpawn(), Facing, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (AController* C = Pawn->GetController())
	{
		C->SetControlRotation(Facing);
	}
	UE_LOG(LogGF, Log, TEXT("Beat Hop: %s put in the lobby"), *Pawn->GetName());
}

void AGFBeatHopCourse::ForEachPlayerPawn(TFunctionRef<void(APawn*)> Fn) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TArray<APawn*> Pawns;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawns.AddUnique(Pawn);
			}
		}
	}
	for (APawn* Pawn : Pawns)
	{
		Fn(Pawn);
	}
}

void AGFBeatHopCourse::UpdateSession()
{
	if (!HasAuthority() || !bSession)
	{
		return;
	}
	// Late joiners, respawned pawns and anyone who wandered off go back on the course.
	ForEachPlayerPawn([this](APawn* Pawn)
	{
		if (!IsLostFromCourse(Pawn))
		{
			return;
		}
		ACharacter* Char = Cast<ACharacter>(Pawn);
		if (Char && bRunning)
		{
			RespawnCharacter(Char);
			ApplyRunMovement(Char);
		}
		else
		{
			PlaceInLobby(Pawn);
		}
	});

	if (APawn* Starter = PendingStart.Get())
	{
		FString Why;
		TryStartFromGate(Starter, Why);
	}
	PendingStart.Reset();
	if (APawn* Finisher = PendingFinish.Get())
	{
		ReachFinish(Finisher);
	}
	PendingFinish.Reset();

	if (bRunning)
	{
		// Auto-run along the course; a player put back mid-run waits on their brick for the song to catch up.
		const double Now = GetRunTime();
		const FVector Forward = GetActorForwardVector();
		for (const TPair<TWeakObjectPtr<ACharacter>, FSavedRunMove>& Pair : SavedMoves)
		{
			ACharacter* Char = Pair.Key.Get();
			if (!Char)
			{
				continue;
			}
			if (const double* Hold = HoldUntil.Find(Char))
			{
				UCharacterMovementComponent* Move = Char->GetCharacterMovement();
				// A hop while waiting lets them go early (#221): with zero speed they'd drop short of the next brick.
				const bool bJumped = Move && Move->IsFalling() && Move->Velocity.Z > 1.f;
				if (Now < *Hold && !bJumped)
				{
					if (Move)
					{
						Move->Velocity = FVector(0.0, 0.0, Move->Velocity.Z);
					}
					continue;
				}
				// On the beat (or on their own hop): leave the brick with a full hop's worth of speed, not from a standstill.
				ReleaseHold(Char, bJumped);
			}
			Char->AddMovementInput(Forward, 1.f);
		}
	}

	if (IsShowingResults() && !bTravelled && GetServerWorldTime() >= ResultsEndServerTime)
	{
		TravelToHub();
	}
}

void AGFBeatHopCourse::OnSessionRunFinished()
{
	Results.Reset();
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	for (const TPair<FString, FPlayerRun>& Pair : Runs)
	{
		const FGFRhythmScore& Score = Pair.Value.Judge.GetScore();
		FGFBeatHopResultRow Row;
		Row.PlayerKey = Pair.Key;
		Row.PlayerName = Pair.Key;
		if (GS)
		{
			for (const APlayerState* PS : GS->PlayerArray)
			{
				const AGFPlayerState* GFPS = Cast<AGFPlayerState>(PS);
				if (GFPS && GFPS->PlayerKey == Pair.Key)
				{
					Row.PlayerName = GFPS->GetPlayerName();
				}
			}
		}
		Row.Score = Score.Score;
		Row.BestCombo = Score.BestCombo;
		Row.Perfects = Score.Perfects;
		Row.Goods = Score.Goods;
		Row.Misses = Score.Misses;
		Row.Grade = FGFRhythmJudge::GradeFor(Score);
		Row.bPassed = bLastPassed;
		Results.Add(Row);
	}
	Results.Sort([](const FGFBeatHopResultRow& A, const FGFBeatHopResultRow& B) { return A.Score > B.Score; });
	ResultsEndServerTime = GetServerWorldTime() + FMath::Max(0.f, ResultsSeconds);
	RestoreRunMovement();
	HoldUntil.Reset();
	HoldBrick.Reset();
	for (FGFBeatHopMoveRep& Row : MoveReps)
	{
		Row.HoldBrick = INDEX_NONE;
		Row.HoldUntilRunTime = -1.0;
	}
	UpdateLobbyLook();
	for (const FGFBeatHopResultRow& Row : Results)
	{
		UE_LOG(LogGF, Log, TEXT("Beat Hop: result %s: score %d, best combo %d, grade %s, %s (P%d G%d M%d)"), *Row.PlayerName, Row.Score, Row.BestCombo,
			*StaticEnum<EGFBeatHopGrade>()->GetNameStringByValue(static_cast<int64>(Row.Grade)),
			Row.bPassed ? TEXT("passed") : TEXT("did not pass"), Row.Perfects, Row.Goods, Row.Misses);
	}
}

double AGFBeatHopCourse::GetResultsSecondsLeft() const
{
	return IsShowingResults() ? FMath::Max(0.0, ResultsEndServerTime - GetServerWorldTime()) : 0.0;
}

void AGFBeatHopCourse::TravelToHub()
{
	bTravelled = true;
	const FString Map = FGFMiniGameCatalog::HubMap().ToString();
	LastTravelMap = Map;
	UE_LOG(LogGF, Log, TEXT("Beat Hop: back to the hub (%s)"), *Map);
	if (TravelOverride)
	{
		TravelOverride(Map);
		return;
	}
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	const ENetMode Mode = GetNetMode();
	if (Mode == NM_ListenServer)
	{
		World->ServerTravel(Map + TEXT("?listen"));
	}
	else if (Mode == NM_DedicatedServer)
	{
		World->ServerTravel(Map);
	}
	else
	{
		UGameplayStatics::OpenLevel(this, FName(*Map));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// StartGate countdown screen (beat_hop_v2): timing from T_BeatHop_Countdown_layout.json.

int32 AGFBeatHopCourse::CountdownFrame(double RunTime, double GoTime, double Period, bool bRunningNow, double WorldNow)
{
	enum : int32 { Off = 0, ReadyA = 1, Three = 2, Two = 3, One = 4, Go = 5, GoFlash = 6, ReadyB = 7 };
	if (!bRunningNow)
	{
		return (FMath::FloorToInt64(WorldNow * 2.0) % 2 == 0) ? ReadyA : ReadyB; // waiting on the pad: 2 Hz
	}
	const double P = FMath::Max(Period, 0.05);
	if (RunTime < GoTime - 3.0 * P)
	{
		return (FMath::FloorToInt64(RunTime / P) % 2 == 0) ? ReadyA : ReadyB; // on the beat
	}
	if (RunTime < GoTime - 2.0 * P) { return Three; }
	if (RunTime < GoTime - P) { return Two; }
	if (RunTime < GoTime) { return One; }
	if (RunTime < GoTime + 0.5)
	{
		return (FMath::FloorToInt64((RunTime - GoTime) / 0.125) % 2 == 0) ? GoFlash : Go;
	}
	if (RunTime < GoTime + 1.0) { return Go; }
	return Off;
}

void AGFBeatHopCourse::UpdateScreen(double WorldNow)
{
	if (!ScreenMaterial || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const double GoTime = Bricks.Num() > 0 ? Bricks[0].Time : 0.0;
	const int32 Frame = CountdownFrame(bRunning ? GetRunTime() : 0.0, GoTime, PeriodSeconds(), bRunning, WorldNow);
	if (Frame != ShownScreenFrame)
	{
		ShownScreenFrame = Frame;
		ScreenMaterial->SetScalarParameterValue(GFBeatHopCourse::ScreenFrameParam, static_cast<float>(Frame));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Courses, rescue Ripwings, revenge dinos (#114). Hazards add velocity. They do not write the run's walk speed,
// gravity or air control; that feel stays on ApplyRunMovement.

bool AGFBeatHopCourse::SelectCourse(FName CourseId)
{
	if (!HasAuthority() || bRunning || IsShowingResults() || !FGFBeatHopCourses::Find(CourseId))
	{
		return false;
	}
	if (CourseId != SelectedCourseId)
	{
		SelectedCourseId = CourseId;
		if (!RunSongId.IsNone() || bSession)
		{
			RebuildPreview();
		}
		else
		{
			RunCourseId = CourseId;
			UpdateLobbyLook();
		}
		const FGFBeatHopCourseDef* Course = FGFBeatHopCourses::Find(CourseId);
		UE_LOG(LogGF, Log, TEXT("Beat Hop: course %s"), Course ? *Course->Title : *CourseId.ToString());
	}
	return true;
}

int32 AGFBeatHopCourse::GetRescueCatches(const FString& PlayerKey) const
{
	const FGFBeatHopRescueBook::FHire* Hire = Rescues.Find(PlayerKey);
	return Hire ? Hire->CatchesLeft : 0;
}

EGFTxResult AGFBeatHopCourse::BurnCost(const FGuid& TxId, const FString& PlayerKey, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	if (LedgerOverride && AccountsOverride)
	{
		return LedgerOverride->Burn(*AccountsOverride, TxId, PlayerKey, Stacks, Reason);
	}
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	if (GS && HasAuthority())
	{
		return GS->ServerBurn(TxId, PlayerKey, Stacks, Reason);
	}
	return EGFTxResult::UnknownAccount;
}

bool AGFBeatHopCourse::TryHireRescue(const FString& PlayerKey, FString& OutReason)
{
	OutReason.Reset();
	if (!HasAuthority())
	{
		OutReason = TEXT("Only the host hires a rescue dino.");
		return false;
	}
	if (PlayerKey.IsEmpty())
	{
		OutReason = TEXT("Nobody to hire for.");
		return false;
	}
	if (Rescues.Find(PlayerKey))
	{
		OutReason = TEXT("This Ripwing is already hired.");
		return false;
	}
	const TArray<FGFItemStack> Cost = { FGFItemStack(FGFBeatHopRescueRules::CostItem(), FGFBeatHopRescueRules::Cost) };
	const FGuid Tx = MakeGrantTxId(RescueSalt, PlayerKey, TEXT("RescueHire"));
	const EGFTxResult Paid = BurnCost(Tx, PlayerKey, Cost, TEXT("BeatHop.RescueHire"));
	if (Paid != EGFTxResult::Applied && Paid != EGFTxResult::AlreadyApplied)
	{
		OutReason = FString::Printf(TEXT("A rescue Ripwing costs %d GlowDust."), FGFBeatHopRescueRules::Cost);
		return false;
	}
	if (!Rescues.Hire(PlayerKey, FGFBeatHopRescueRules::Catches))
	{
		OutReason = TEXT("This Ripwing is already hired.");
		return false;
	}
	EnsureDinoMeshes();
	UpdateLobbyLook();
	UE_LOG(LogGF, Log, TEXT("Beat Hop: %s hired a Ripwing (%d catches)"), *PlayerKey, FGFBeatHopRescueRules::Catches);
	return true;
}

bool AGFBeatHopCourse::NotifyFall(ACharacter* Character, const FString& PlayerKey)
{
	if (!HasAuthority() || !bRunning || !Character)
	{
		return false;
	}
	if (!Rescues.SpendCatch(PlayerKey))
	{
		return false;
	}
	ShowRescueAt(Character->GetActorLocation() + FVector(0.f, 0.f, 180.f));
	PutBackMidRun(Character, PlayerKey);
	UE_LOG(LogGF, Log, TEXT("Beat Hop: Ripwing caught %s (%d catches left)"), *PlayerKey, GetRescueCatches(PlayerKey));
	return true;
}

void AGFBeatHopCourse::EnsureDinoMeshes()
{
	if (RescueDino && RevengeDino)
	{
		return;
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* Basic = BaseMaterial ? BaseMaterial.Get() : LoadObject<UMaterialInterface>(nullptr, GFBeatHopCourse::MaterialPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	auto Make = [&](const TCHAR* Name, const FLinearColor& Tint) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, Name, RF_Transient);
		Mesh->SetupAttachment(Root);
		if (Sphere)
		{
			Mesh->SetStaticMesh(Sphere);
		}
		Mesh->SetRelativeScale3D(FVector(1.2f));
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetHiddenInGame(true);
		if (Basic)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Basic, this);
			if (Mid)
			{
				Mid->SetVectorParameterValue(GFBeatHopCourse::ColorParam, Tint);
				Mesh->SetMaterial(0, Mid);
			}
		}
		Mesh->RegisterComponent();
		return Mesh;
	};
	if (!RescueDino)
	{
		RescueDino = Make(TEXT("RescueDino"), GFBeatHopCourse::Mint);
	}
	if (!RevengeDino)
	{
		RevengeDino = Make(TEXT("RevengeDino"), GFBeatHopCourse::Coral);
	}
}

void AGFBeatHopCourse::ShowRescueAt(const FVector& WorldLocation)
{
	EnsureDinoMeshes();
	bRescueShown = 1;
	RescueSpot = WorldLocation;
	if (RescueDino)
	{
		RescueDino->SetRelativeLocation(GetActorTransform().InverseTransformPosition(WorldLocation));
		RescueDino->SetHiddenInGame(false);
	}
}

void AGFBeatHopCourse::ApplyHazardPush(ACharacter* Character, const FVector& CourseLocalPush)
{
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Move || CourseLocalPush.IsNearlyZero())
	{
		return;
	}
	// A shove, not a retune: walk speed, gravity and air control stay where the run left them.
	Move->Velocity += GetActorTransform().TransformVector(CourseLocalPush);
}

void AGFBeatHopCourse::RefreshHazards(const APawn* Runner)
{
	// Live bricks stay on the chart. Course hazards belong to the song courses.
	if (RunMode == EGFBeatHopMode::Live)
	{
		Revenge.bSpawned = false;
		bRevengeShown = 0;
		if (RevengeDino)
		{
			RevengeDino->SetHiddenInGame(true);
		}
		return;
	}
	const FGFBeatHopCourseDef* Layout = FGFBeatHopCourses::Find(RunCourseId);
	if (!Layout)
	{
		Layout = &FGFBeatHopCourses::Default();
	}
	float StationX = 0.f;
	float RunnerX = 0.f;
	if (Bricks.Num() > 0 && Bricks[0].Mesh && Bricks.Last().Mesh)
	{
		StationX = static_cast<float>((Bricks[0].Mesh->GetRelativeLocation().X + Bricks.Last().Mesh->GetRelativeLocation().X) * 0.5);
		RunnerX = static_cast<float>(Bricks[0].Mesh->GetRelativeLocation().X);
	}
	if (Runner)
	{
		RunnerX = static_cast<float>(GetActorTransform().InverseTransformPosition(Runner->GetActorLocation()).X);
	}
	Revenge.Reset(*Layout, StationX, RunnerX);
	bRevengeShown = Revenge.bSpawned ? 1 : 0;
	RevengeSpot = Revenge.Local;
	EnsureDinoMeshes();
	if (RevengeDino)
	{
		RevengeDino->SetRelativeLocation(Revenge.Local);
		RevengeDino->SetHiddenInGame(!Revenge.bSpawned);
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(RevengeDino->GetMaterial(0)))
		{
			Mid->SetVectorParameterValue(GFBeatHopCourse::ColorParam, Layout->Tint);
		}
	}
}

void AGFBeatHopCourse::TickHazards(float DeltaSeconds)
{
	if (!HasAuthority() || !bRunning || DeltaSeconds <= 0.f || RunMode == EGFBeatHopMode::Live)
	{
		return;
	}
	const FGFBeatHopCourseDef* Layout = FGFBeatHopCourses::Find(RunCourseId);
	if (!Layout)
	{
		Layout = &FGFBeatHopCourses::Default();
	}
	const FVector Right = GetActorRightVector();
	for (const TPair<TWeakObjectPtr<ACharacter>, FSavedRunMove>& Pair : SavedMoves)
	{
		ACharacter* Char = Pair.Key.Get();
		UCharacterMovementComponent* Move = Char ? Char->GetCharacterMovement() : nullptr;
		if (!Char || !Move)
		{
			continue;
		}
		if (Layout->IsSkiddy())
		{
			Move->Velocity = FGFBeatHopCourses::ApplySkid(Move->Velocity, Right, Layout->SkidCmPerSec, DeltaSeconds);
		}
		const FVector Local = GetActorTransform().InverseTransformPosition(Char->GetActorLocation());
		const FGFBeatHopRevengeState::FStep Step = Revenge.Step(RunnerId(Char), Local, DeltaSeconds, *Layout);
		if (Step.bKnocked || !Step.Push.IsNearlyZero())
		{
			ApplyHazardPush(Char, Step.Push);
		}
	}
	if (Revenge.bSpawned && RevengeDino)
	{
		bRevengeShown = 1;
		RevengeSpot = Revenge.Local;
		RevengeDino->SetRelativeLocation(Revenge.Local);
		RevengeDino->SetHiddenInGame(false);
	}
}

void AGFBeatHopCourse::OnRep_Hazards()
{
	EnsureDinoMeshes();
	if (RevengeDino)
	{
		RevengeDino->SetRelativeLocation(FVector(RevengeSpot));
		RevengeDino->SetHiddenInGame(bRevengeShown == 0);
	}
	if (RescueDino)
	{
		RescueDino->SetWorldLocation(FVector(RescueSpot));
		RescueDino->SetHiddenInGame(bRescueShown == 0);
	}
}
