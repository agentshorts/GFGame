#include "GFBeatHopCourse.h"
#include "GFBuiltInSongs.h"
#include "GFBeatClockSubsystem.h"
#include "GFBeatHopGateSubsystem.h"
#include "GFRhythmSettings.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
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

	// Original palette: coral, sunflower, mint, sky, lilac.
	LaneColors = {
		FLinearColor(1.00f, 0.42f, 0.38f),
		FLinearColor(1.00f, 0.80f, 0.25f),
		FLinearColor(0.35f, 0.90f, 0.62f),
		FLinearColor(0.35f, 0.65f, 1.00f),
		FLinearColor(0.72f, 0.52f, 1.00f),
	};
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

	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Mesh->SetupAttachment(Root);
	if (CubeMesh)
	{
		Mesh->SetStaticMesh(CubeMesh);
	}
	// Engine cube is 100 cm, centred on its origin.
	const float PadScale = Type == EGFBeatNoteType::Pad ? 1.3f : 1.f;
	Mesh->SetRelativeLocation(FVector(X, Y, -BrickSizeCm.Z * 0.5));
	Mesh->SetRelativeScale3D(FVector(Length / 100.0, BrickSizeCm.Y * PadScale / 100.0, BrickSizeCm.Z / 100.0));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(false);
	if (BaseMaterial)
	{
		B.Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Mesh->SetMaterial(0, B.Material);
	}
	Mesh->RegisterComponent();
	B.Mesh = Mesh;
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
	return GetBrick(BrickIndex).Mesh->GetComponentLocation() + FVector(0, 0, BrickSizeCm.Z * 0.5);
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
		const float Glow = FGFBrickLighting::GlowAt(Now / P, B.Time / P, B.LenSeconds / P, W, B.bHitByAnyone, B.bMissedByEveryone);
		if (FMath::IsNearlyEqual(Glow, B.ShownGlow, 0.01f) || !B.Material)
		{
			continue;
		}
		B.ShownGlow = Glow;
		FLinearColor C = LaneColor(B.Lane);
		if (B.bMissedByEveryone)
		{
			C = FLinearColor(0.08f, 0.08f, 0.09f);
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
