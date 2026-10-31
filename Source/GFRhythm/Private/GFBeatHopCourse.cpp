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
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
}

void AGFBeatHopCourse::BeginPlay()
{
	Super::BeginPlay();
	if (KitComponents.Num() == 0)
	{
		BuildTrackKit(); // the start pad and gate stand ready before the first run
	}
}

void AGFBeatHopCourse::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFBeatHopCourse, RunSongId);
	DOREPLIFETIME(AGFBeatHopCourse, RunDifficulty);
	DOREPLIFETIME(AGFBeatHopCourse, RunMode);
	DOREPLIFETIME(AGFBeatHopCourse, bRunCozy);
	DOREPLIFETIME(AGFBeatHopCourse, RunStartServerTime);
	DOREPLIFETIME(AGFBeatHopCourse, bRunning);
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
	RunId = FGuid::NewGuid(); // identifies the run; every grant's TxId is derived from it
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
	const double X = (Time - StartTime) * Speed + LenSeconds * Speed * 0.5;
	const double Y = (Lane - (Lanes - 1) * 0.5) * LaneWidthCm;

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
	const double OffsetMs = GetDefault<UGFRhythmSettings>()->SongsOffsetMs;
	return GetServerWorldTime() - RunStartServerTime - OffsetMs / 1000.0;
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
	return Run ? &Run->Judge.GetScore() : nullptr;
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
	AddPop(PlayerKey, GetBrickTop(BrickIndex), J, Run.Judge.GetScore().Combo, LaneColor(B.Lane));
	if (J == EGFJudgement::Perfect)
	{
		Burst(GetBrickTop(BrickIndex), LaneColor(B.Lane));
	}

	int32 Earned = 0;
	if (J == EGFJudgement::Perfect || J == EGFJudgement::Good)
	{
		B.bHitByAnyone = true;
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
					AddPop(Pair.Key, GetBrickTop(Run.MissSweep), EGFJudgement::Miss, 0, LaneColor(B.Lane));
				}
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
	else if (!HasAuthority())
	{
		// Clients only drive visuals; misses show once the window passes.
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

	const int32 TotalNotes = NumBricks();
	UGFBeatHopGateSubsystem* Gate = nullptr;
	if (const UGameInstance* GI = GetGameInstance())
	{
		Gate = GI->GetSubsystem<UGFBeatHopGateSubsystem>();
	}
	for (TPair<FString, FPlayerRun>& Pair : Runs)
	{
		FPlayerRun& Run = Pair.Value;
		const FGFRhythmScore& Score = Run.Judge.GetScore();
		Grant(Pair.Key, Run, FGFBeatHopRewards::ForSongEnd(Score, TotalNotes, Rewards, bRunCozy), TEXT("BeatHop.SongEnd"), TEXT("SongEnd"));

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
	const double WorldNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	TickDebris(DeltaSeconds);
	UpdatePulse(WorldNow);
	DriveAnims(WorldNow);
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
	Pad(FirstX - 200.0 - StartPadCm, FirstX - 200.0);
	// The gate's -X face (countdown screen, coral lamps) faces the players on the pad.
	StartGate = AddKitMesh(*TrackPath(TEXT("SM_Track_StartGate")), FTransform(FVector(FirstX - 450.0, 0.0, 0.0)), true);
	if (StartGate)
	{
		TrackPulse(StartGate);
		if (PulseMaterials.Num() > 0)
		{
			PulseMaterials.Last()->SetVectorParameterValue(EmissiveColorParam, Coral);
		}
		// The Screen slot has no art material yet: keep it a dark panel instead of the engine grid.
		if (StartGate->GetNumMaterials() > 1)
		{
			if (UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
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

	const double KitFrom = FirstX - 200.0 - StartPadCm - 400.0;
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
		for (int32 Beat = CheckpointEveryBeats; ; Beat += CheckpointEveryBeats)
		{
			const double X = Beatmap.BeatToSeconds(Beat) * Speed;
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

		auto Set = [&](FName Key, float Rate, bool bOneShot)
		{
			if (!bDrawAnims)
			{
				H->Key = Key;
				return;
			}
			const float Len = Play(Mesh, Key, Rate);
			H->Key = Key;
			H->OneShotUntil = bOneShot ? Now + Len : 0.0;
		};

		if (H->bCaught)
		{
			// Land_Catch plays out, then the host puts the player back on the last checkpoint (or the start pad).
			if (Now >= H->RespawnAt)
			{
				if (HasAuthority())
				{
					Char->TeleportTo(GetRespawnPoint(Char), Char->GetActorRotation(), false, true);
					Move->StopMovementImmediately();
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
			else if (Landed == EGFJudgement::Perfect)
			{
				Set(AnimLandPerfect, 1.f, true);
			}
			else if (Landed == EGFJudgement::Miss)
			{
				Set(AnimStumble, 1.f, true);
			}
			else
			{
				Set(AnimLand, 1.f, true);
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
