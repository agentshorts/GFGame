// Beat Hop replication (#214): the host's judgements, scores, hit bricks, run movement, put-back teleports
// and countdown clock are what a client applies. These stand in for a second machine; a packaged two-player
// session is still the check that pops and hops show up on both screens.

#include "Misc/AutomationTest.h"
#include "GFBeatHopCourse.h"
#include "GFBuiltInSongs.h"
#include "GFRhythmJudge.h"
#include "GFRhythmSettings.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFBeatHopReplicationTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FWorld
	{
		UWorld* World = nullptr;

		FWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFBeatHopReplicationWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static ACharacter* SpawnRunner(UWorld* World, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(At), Params);
	}

	static const FGFBeatHopJudgementRep* FindJudgement(const TArray<FGFBeatHopJudgementRep>& Rows, const FString& Player, EGFJudgement Result)
	{
		for (const FGFBeatHopJudgementRep& Row : Rows)
		{
			if (Row.PlayerKey == Player && Row.Result == Result)
			{
				return &Row;
			}
		}
		return nullptr;
	}

	static bool PopHas(const TArray<FGFBeatHopPop>& Pops, const FString& Player, EGFJudgement Result)
	{
		for (const FGFBeatHopPop& Pop : Pops)
		{
			if (Pop.PlayerKey == Player && Pop.Judgement == Result)
			{
				return true;
			}
		}
		return false;
	}

	static AGFBeatHopCourse* StartSong(UWorld* World, EGFBeatDifficulty Difficulty, FString& Why)
	{
		AGFBeatHopCourse* Course = World ? World->SpawnActor<AGFBeatHopCourse>() : nullptr;
		if (!Course)
		{
			return nullptr;
		}
		const TArray<FName> Ids = FGFBuiltInSongs::SongIds();
		if (Ids.Num() == 0 || !Course->StartRunForPlayers(Ids[0], Difficulty, EGFBeatHopMode::Songs, false, 2, Why))
		{
			return nullptr;
		}
		return Course;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopJudgementReplicationTest, "GFGame.BeatHop.JudgementReplication", GFBeatHopReplicationTests::Flags)
bool FGFBeatHopJudgementReplicationTest::RunTest(const FString&)
{
	using namespace GFBeatHopReplicationTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Host = StartSong(W.World, EGFBeatDifficulty::Normal, Why);
	if (!TestNotNull(TEXT("Host run"), Host))
	{
		AddError(Why);
		return false;
	}
	if (!TestTrue(TEXT("Two bricks"), Host->NumBricks() > 2))
	{
		return false;
	}

	const double T0 = Host->GetBrick(0).Time;
	Host->SetDebugRunTime(T0);
	TestTrue(TEXT("A perfect on the beat"), Host->HandleLanding(TEXT("A"), 0, T0) == EGFJudgement::Perfect);
	const double Late = T0 + 0.08;
	TestTrue(TEXT("B good, a little late"), Host->HandleLanding(TEXT("B"), 0, Late) == EGFJudgement::Good);

	const FGFBeatHopJudgementRep* JA = FindJudgement(Host->GetJudgements(), TEXT("A"), EGFJudgement::Perfect);
	const FGFBeatHopJudgementRep* JB = FindJudgement(Host->GetJudgements(), TEXT("B"), EGFJudgement::Good);
	if (!TestNotNull(TEXT("A's judgement is on the record"), JA) || !TestNotNull(TEXT("B's judgement is on the record"), JB))
	{
		return false;
	}
	TestEqual(TEXT("A's brick"), JA->BrickIndex, 0);
	TestEqual(TEXT("A's offset is zero"), JA->OffsetSeconds, 0.f, 0.001f);
	TestEqual(TEXT("A's combo"), JA->Combo, 1);
	TestEqual(TEXT("B's brick"), JB->BrickIndex, 0);
	TestEqual(TEXT("B's offset is how late they were"), JB->OffsetSeconds, 0.08f, 0.002f);
	TestTrue(TEXT("Brick 0 is hit for everyone"), Host->IsBrickHitByAnyone(0));

	const double T1 = Host->GetBrick(1).Time;
	Host->SetDebugRunTime(T1 + 0.2);
	Host->UpdateRun();
	const FGFBeatHopJudgementRep* Miss = FindJudgement(Host->GetJudgements(), TEXT("A"), EGFJudgement::Miss);
	if (!TestNotNull(TEXT("A's missed brick is on the record"), Miss))
	{
		return false;
	}
	TestEqual(TEXT("Miss names the brick"), Miss->BrickIndex, 1);
	TestTrue(TEXT("Miss offset is past the note"), Miss->OffsetSeconds > 0.f);

	const FGFRhythmScore* ScoreA = Host->GetScore(TEXT("A"));
	const FGFRhythmScore* ScoreB = Host->GetScore(TEXT("B"));
	if (!TestNotNull(TEXT("A's score"), ScoreA) || !TestNotNull(TEXT("B's score"), ScoreB))
	{
		return false;
	}
	TArray<FGFBeatHopScoreRep> Board;
	Host->GetScoreboard(Board);
	TestEqual(TEXT("Scoreboard lists both players"), Board.Num(), 2);

	// A second course with the same chart stands in for a client that received the replicated view.
	AGFBeatHopCourse* Client = StartSong(W.World, EGFBeatDifficulty::Normal, Why);
	if (!TestNotNull(TEXT("Client course"), Client))
	{
		AddError(Why);
		return false;
	}
	Client->ReceiveRemoteHitBricks(Host->GetHitBricks());
	Client->ReceiveRemoteJudgements(Host->GetJudgements());
	Client->ReceiveRemoteScores(Host->GetScoreReps());
	TestTrue(TEXT("Client keeps the hit brick"), Client->IsBrickHitByAnyone(0));
	TestFalse(TEXT("Client does not pretend the miss was a hit"), Client->IsBrickHitByAnyone(1));
	TestTrue(TEXT("Client pop for A"), PopHas(Client->GetPops(), TEXT("A"), EGFJudgement::Perfect));
	TestTrue(TEXT("Client pop for B"), PopHas(Client->GetPops(), TEXT("B"), EGFJudgement::Good));
	TestTrue(TEXT("Client pop for the miss"), PopHas(Client->GetPops(), TEXT("A"), EGFJudgement::Miss));
	const FGFRhythmScore* ClientA = Client->GetScore(TEXT("A"));
	const FGFRhythmScore* ClientB = Client->GetScore(TEXT("B"));
	if (!TestNotNull(TEXT("Client score A"), ClientA) || !TestNotNull(TEXT("Client score B"), ClientB))
	{
		return false;
	}
	TestEqual(TEXT("Client has A's score"), ClientA->Score, ScoreA->Score);
	TestEqual(TEXT("Client has A's perfects"), ClientA->Perfects, ScoreA->Perfects);
	TestEqual(TEXT("Client has A's misses"), ClientA->Misses, ScoreA->Misses);
	TestEqual(TEXT("Client has B's score"), ClientB->Score, ScoreB->Score);
	TestEqual(TEXT("Client has B's goods"), ClientB->Goods, ScoreB->Goods);

	Host->BeginSession();
	Host->FinishRun();
	const TArray<FGFBeatHopResultRow>& Results = Host->GetResults();
	if (!TestEqual(TEXT("Results carry both players"), Results.Num(), 2))
	{
		return false;
	}
	const FGFBeatHopResultRow* RowA = Results.FindByPredicate([](const FGFBeatHopResultRow& Row) { return Row.PlayerKey == TEXT("A"); });
	const FGFBeatHopResultRow* RowB = Results.FindByPredicate([](const FGFBeatHopResultRow& Row) { return Row.PlayerKey == TEXT("B"); });
	if (!TestNotNull(TEXT("Result A"), RowA) || !TestNotNull(TEXT("Result B"), RowB))
	{
		return false;
	}
	TestEqual(TEXT("A's result perfects"), RowA->Perfects, 1);
	TestTrue(TEXT("A's result counts the misses"), RowA->Misses >= 1);
	TestEqual(TEXT("B's result goods"), RowB->Goods, 1);
	TestTrue(TEXT("Results are up for every screen"), Host->IsShowingResults());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopJudgementRingTest, "GFGame.BeatHop.JudgementRing", GFBeatHopReplicationTests::Flags)
bool FGFBeatHopJudgementRingTest::RunTest(const FString&)
{
	using namespace GFBeatHopReplicationTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Host = StartSong(W.World, EGFBeatDifficulty::Easy, Why);
	if (!TestNotNull(TEXT("Host"), Host) || !TestTrue(TEXT("Long enough chart"), Host->NumBricks() > 30))
	{
		AddError(Why);
		return false;
	}
	for (int32 i = 0; i < 30; ++i)
	{
		const double T = Host->GetBrick(i).Time;
		Host->SetDebugRunTime(T);
		if (!TestTrue(FString::Printf(TEXT("Hop %d"), i), Host->HandleLanding(TEXT("A"), i, T) == EGFJudgement::Perfect))
		{
			return false;
		}
	}
	TestEqual(TEXT("Ring keeps the newest hops"), Host->GetJudgements().Num(), 24);
	TestEqual(TEXT("Newest hop is still there"), Host->GetJudgements().Last().BrickIndex, 29);
	bool bKeptFirst = false;
	for (const FGFBeatHopJudgementRep& Row : Host->GetJudgements())
	{
		bKeptFirst |= Row.BrickIndex == 0;
	}
	TestFalse(TEXT("The oldest hop fell out of the ring"), bKeptFirst);
	TestTrue(TEXT("The oldest brick is still marked hit"), Host->GetHitBricks().Contains(0));

	AGFBeatHopCourse* Client = StartSong(W.World, EGFBeatDifficulty::Easy, Why);
	if (!TestNotNull(TEXT("Client"), Client))
	{
		return false;
	}
	Client->ReceiveRemoteHitBricks(Host->GetHitBricks());
	Client->ReceiveRemoteJudgements(Host->GetJudgements());
	TestTrue(TEXT("Late joiner still sees the first brick was hit"), Client->IsBrickHitByAnyone(0));
	TestTrue(TEXT("Late joiner sees a recent hop"), Client->IsBrickHitByAnyone(29));
	bool bPopOnFirst = false;
	for (const FGFBeatHopPop& Pop : Client->GetPops())
	{
		// Pops are only built for judgements still in the ring. Brick 0's top is a different place; match by combo of the newest.
		bPopOnFirst |= Pop.Combo == 1;
	}
	TestFalse(TEXT("The dropped hop does not pop on the client"), bPopOnFirst);
	TestEqual(TEXT("Client pops the ring, not the whole song"), Client->GetPops().Num(), 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopMovementMirrorTest, "GFGame.BeatHop.MovementMirror", GFBeatHopReplicationTests::Flags)
bool FGFBeatHopMovementMirrorTest::RunTest(const FString&)
{
	using namespace GFBeatHopReplicationTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = W.World->SpawnActor<AGFBeatHopCourse>();
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	Course->BeginSession();
	ACharacter* Runner = SpawnRunner(W.World, Course->GetStartTrigger() ? Course->GetStartTrigger()->GetComponentLocation() : Course->GetLobbySpawn());
	UCharacterMovementComponent* Move = Runner ? Runner->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("Runner"), Runner) || !TestNotNull(TEXT("Movement"), Move))
	{
		return false;
	}
	const float AirBefore = Move->AirControl;
	const float WalkBefore = Move->MaxWalkSpeed;
	const float GravityBefore = Move->GravityScale;
	Course->SetDebugServerTime(1000.0);
	FString Why;
	if (!TestTrue(TEXT("Run starts"), Course->TryStartFromGate(Runner, Why)))
	{
		AddError(Why);
		return false;
	}
	if (!TestTrue(TEXT("Move row published"), Course->GetMoveReps().Num() >= 1))
	{
		return false;
	}
	const FGFBeatHopMoveRep& Row = Course->GetMoveReps().Last();
	TestEqual(TEXT("Phase is the run"), Row.Phase, static_cast<uint8>(1));
	TestEqual(TEXT("Replicated walk speed"), Row.MaxWalkSpeed, Move->MaxWalkSpeed, 0.01f);
	TestEqual(TEXT("Replicated gravity"), Row.GravityScale, Move->GravityScale, 0.001f);
	TestEqual(TEXT("Replicated air control"), Row.AirControl, 1.f, 0.001f);
	TestEqual(TEXT("Saved walk speed"), Row.SavedWalkSpeed, WalkBefore, 0.01f);
	TestEqual(TEXT("Saved air control"), Row.SavedAirControl, AirBefore, 0.001f);

	ACharacter* Owner = SpawnRunner(W.World, FVector(0.0, 0.0, 200.0));
	UCharacterMovementComponent* OwnerMove = Owner ? Owner->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("Owning client pawn"), Owner) || !TestNotNull(TEXT("Owning client movement"), OwnerMove))
	{
		return false;
	}
	OwnerMove->MaxWalkSpeed = 1.f;
	OwnerMove->GravityScale = 9.f;
	OwnerMove->AirControl = 0.f;
	Course->ApplyMoveRep(Owner, Row);
	TestEqual(TEXT("Owner runs at the chart speed"), OwnerMove->MaxWalkSpeed, Move->MaxWalkSpeed, 0.01f);
	TestEqual(TEXT("Owner gravity matches the beat"), OwnerMove->GravityScale, Move->GravityScale, 0.001f);
	TestEqual(TEXT("Owner air control matches"), OwnerMove->AirControl, 1.f, 0.001f);

	TestTrue(TEXT("Finish"), Course->ReachFinish(Runner));
	if (!TestTrue(TEXT("Move row still there"), Course->GetMoveReps().Num() >= 1))
	{
		return false;
	}
	const FGFBeatHopMoveRep& Restored = Course->GetMoveReps().Last();
	TestEqual(TEXT("Phase handed back"), Restored.Phase, static_cast<uint8>(2));
	TestEqual(TEXT("Host walk speed restored"), Move->MaxWalkSpeed, WalkBefore, 0.01f);
	TestEqual(TEXT("Host gravity restored"), Move->GravityScale, GravityBefore, 0.001f);
	TestEqual(TEXT("Host air control restored"), Move->AirControl, AirBefore, 0.001f);
	Course->ApplyMoveRep(Owner, Restored);
	TestEqual(TEXT("Owner walk speed handed back"), OwnerMove->MaxWalkSpeed, WalkBefore, 0.01f);
	TestEqual(TEXT("Owner gravity handed back"), OwnerMove->GravityScale, GravityBefore, 0.001f);
	TestEqual(TEXT("Owner air control handed back"), OwnerMove->AirControl, AirBefore, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopRespawnPlacementTest, "GFGame.BeatHop.RespawnPlacement", GFBeatHopReplicationTests::Flags)
bool FGFBeatHopRespawnPlacementTest::RunTest(const FString&)
{
	using namespace GFBeatHopReplicationTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = W.World->SpawnActor<AGFBeatHopCourse>();
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	Course->BeginSession();
	Course->SetDebugServerTime(1000.0);
	ACharacter* Runner = SpawnRunner(W.World, Course->GetStartTrigger() ? Course->GetStartTrigger()->GetComponentLocation() : Course->GetLobbySpawn());
	FString Why;
	if (!TestNotNull(TEXT("Runner"), Runner) || !TestTrue(TEXT("Run starts"), Course->TryStartFromGate(Runner, Why)))
	{
		AddError(Why);
		return false;
	}
	UCharacterMovementComponent* Move = Runner->GetCharacterMovement();
	if (!TestNotNull(TEXT("Movement"), Move) || !TestTrue(TEXT("Bricks"), Course->NumBricks() > 4))
	{
		return false;
	}
	const double T0 = Course->GetBrick(0).Time;
	Course->SetDebugRunTime(T0);
	Course->PutBackMidRun(Runner, TEXT("Solo"));
	const int32 Held = Course->GetHoldBrick(Runner);
	if (!TestTrue(TEXT("Held on a brick"), Course->IsValidBrick(Held)))
	{
		return false;
	}
	const FGFBeatHopPlacementRep* Place = nullptr;
	for (const FGFBeatHopPlacementRep& Row : Course->GetPlacements())
	{
		if (!Place || Row.Seq > Place->Seq)
		{
			Place = &Row;
		}
	}
	if (!TestNotNull(TEXT("Placement published"), Place))
	{
		return false;
	}
	TestEqual(TEXT("Placement names the runner"), Place->PlayerKey, FString(TEXT("Solo")));
	TestTrue(TEXT("Placement is where they were put"), FVector::Dist(FVector(Place->Location), Runner->GetActorLocation()) < 2.0);
	TestEqual(TEXT("Waiting, so no velocity"), FVector(Place->Velocity).Size(), 0.0, 0.01);
	TestEqual(TEXT("Waiting on the brick, not falling"), Place->bFalling, static_cast<uint8>(0));
	TestTrue(TEXT("Server teleport won't be smoothed into a correction"), Move->bJustTeleported);
	TestTrue(TEXT("In-flight client moves are ignored for a moment"), Move->bIgnoreClientMovementErrorChecksAndCorrection);

	const FGFBeatHopMoveRep* Hold = Course->GetMoveReps().FindByPredicate([](const FGFBeatHopMoveRep& Row) { return Row.HoldBrick != INDEX_NONE; });
	if (TestNotNull(TEXT("Hold replicated"), Hold))
	{
		TestEqual(TEXT("Hold brick"), Hold->HoldBrick, Held);
		TestEqual(TEXT("Hold until the brick's time"), Hold->HoldUntilRunTime, Course->GetBrick(Held).Time, 0.001);
	}

	ACharacter* ClientPawn = SpawnRunner(W.World, FVector(0.0, 0.0, -500.0));
	if (!TestNotNull(TEXT("Client pawn"), ClientPawn))
	{
		return false;
	}
	Course->ApplyPlacement(ClientPawn, *Place);
	TestTrue(TEXT("Client snaps onto the brick"), FVector::Dist(ClientPawn->GetActorLocation(), Runner->GetActorLocation()) < 2.0);
	UCharacterMovementComponent* ClientMove = ClientPawn->GetCharacterMovement();
	if (TestNotNull(TEXT("Client movement"), ClientMove))
	{
		TestTrue(TEXT("Client snap is a teleport"), ClientMove->bJustTeleported);
		TestFalse(TEXT("Client is standing"), ClientMove->IsFalling());
	}

	Course->SetDebugRunTime(Course->GetBrick(Held).Time);
	Course->UpdateSession();
	const FGFBeatHopPlacementRep* Launch = nullptr;
	for (const FGFBeatHopPlacementRep& Row : Course->GetPlacements())
	{
		if (!Launch || Row.Seq > Launch->Seq)
		{
			Launch = &Row;
		}
	}
	if (!TestNotNull(TEXT("Launch placement"), Launch))
	{
		return false;
	}
	TestTrue(TEXT("Launch is a new placement"), Launch->Seq > Place->Seq);
	TestEqual(TEXT("Launch is airborne"), Launch->bFalling, static_cast<uint8>(1));
	TestEqual(TEXT("Launch lift matches the hop"), FVector(Launch->Velocity).Z, static_cast<double>(Move->JumpZVelocity), 1.0);
	Course->ApplyPlacement(ClientPawn, *Launch);
	if (ClientMove)
	{
		TestEqual(TEXT("Client leaves with the same lift"), ClientMove->Velocity.Z, static_cast<double>(Move->JumpZVelocity), 1.0);
		TestTrue(TEXT("Client is falling"), ClientMove->IsFalling());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopSharedCountdownTest, "GFGame.BeatHop.SharedCountdown", GFBeatHopReplicationTests::Flags)
bool FGFBeatHopSharedCountdownTest::RunTest(const FString&)
{
	using namespace GFBeatHopReplicationTests;
	UGFRhythmSettings* Settings = UGFRhythmSettings::GetMutable();
	if (!TestNotNull(TEXT("Settings"), Settings))
	{
		return false;
	}
	const float SavedOffset = Settings->SongsOffsetMs;
	Settings->SongsOffsetMs = 180.f;

	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		Settings->SongsOffsetMs = SavedOffset;
		return false;
	}
	AGFBeatHopCourse* Host = W.World->SpawnActor<AGFBeatHopCourse>();
	if (!TestNotNull(TEXT("Host"), Host))
	{
		Settings->SongsOffsetMs = SavedOffset;
		return false;
	}
	Host->SetDebugServerTime(1000.0);
	FString Why;
	const TArray<FName> Ids = FGFBuiltInSongs::SongIds();
	if (!TestTrue(TEXT("Songs"), Ids.Num() > 0) || !TestTrue(TEXT("Run"), Host->StartRunForPlayers(Ids[0], EGFBeatDifficulty::Easy, EGFBeatHopMode::Songs, false, 2, Why)))
	{
		AddError(Why);
		Settings->SongsOffsetMs = SavedOffset;
		return false;
	}
	TestEqual(TEXT("Calibration snapshotted from the host"), Host->GetRunCalibrationSeconds(), 0.18, 0.0001);

	// A client that changes its own songs offset must not move the shared clock.
	Settings->SongsOffsetMs = -90.f;
	const double Go = Host->GetBrick(0).Time;
	const double Period = Host->GetBeatmap().SecondsPerBeat();
	const double Want = Go - 2.0 * Period - 0.01;
	const double ServerNow = Host->GetRunStartServerTime() + Host->GetRunCalibrationSeconds() + Want;
	Host->SetDebugServerTime(ServerNow);
	const int32 HostFrame = AGFBeatHopCourse::CountdownFrame(Host->GetRunTime(), Go, Period, true, 0.0);
	TestEqual(TEXT("Host gate shows 3"), HostFrame, 2);

	AGFBeatHopCourse* Client = W.World->SpawnActor<AGFBeatHopCourse>();
	if (!TestNotNull(TEXT("Client"), Client))
	{
		Settings->SongsOffsetMs = SavedOffset;
		return false;
	}
	Client->AdoptSharedClock(Host->GetRunStartServerTime(), Host->GetRunCalibrationSeconds());
	Client->SetDebugServerTime(ServerNow);
	TestEqual(TEXT("Client run clock matches the host"), Client->GetRunTime(), Host->GetRunTime(), 0.0001);
	TestEqual(TEXT("Client gate shows the same 3"), AGFBeatHopCourse::CountdownFrame(Client->GetRunTime(), Go, Period, true, 0.0), HostFrame);

	const double Drifted = ServerNow - Host->GetRunStartServerTime() - (-0.09);
	TestEqual(TEXT("A client using its own offset would already show 2"), AGFBeatHopCourse::CountdownFrame(Drifted, Go, Period, true, 0.0), 3);
	TestTrue(TEXT("Shared clock and the drifted clock disagree"), HostFrame != AGFBeatHopCourse::CountdownFrame(Drifted, Go, Period, true, 0.0));

	Settings->SongsOffsetMs = SavedOffset;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopAnimReplicationTest, "GFGame.BeatHop.AnimReplication", GFBeatHopReplicationTests::Flags)
bool FGFBeatHopAnimReplicationTest::RunTest(const FString&)
{
	const FName Perfect = AGFBeatHopCourse::LandingClipFor(EGFJudgement::Perfect);
	const FName Good = AGFBeatHopCourse::LandingClipFor(EGFJudgement::Good);
	const FName Miss = AGFBeatHopCourse::LandingClipFor(EGFJudgement::Miss);
	const FName None = AGFBeatHopCourse::LandingClipFor(EGFJudgement::None);
	TestEqual(TEXT("Perfect lands on Land_Perfect"), Perfect, FName(TEXT("Player.Land_Perfect")));
	TestEqual(TEXT("Good lands on Bhop_Land"), Good, FName(TEXT("Player.Bhop_Land")));
	TestEqual(TEXT("Miss lands on Stumble"), Miss, FName(TEXT("Player.Stumble")));
	TestTrue(TEXT("No judgement, no landing clip"), None.IsNone());

	const FName Air = TEXT("Player.Bhop_Air");
	const FName Strafe = TEXT("Player.Air_Strafe_L");
	TestEqual(TEXT("A remote pawn plays the host's Perfect"), AGFBeatHopCourse::ClipForViewer(true, Good, Perfect), Perfect);
	TestEqual(TEXT("A remote pawn plays the host's strafe"), AGFBeatHopCourse::ClipForViewer(true, Air, Strafe), Strafe);
	TestEqual(TEXT("A remote pawn with no replicate yet keeps the local clip"), AGFBeatHopCourse::ClipForViewer(true, Air, NAME_None), Air);
	TestEqual(TEXT("The owner upgrades a generic land to Stumble"), AGFBeatHopCourse::ClipForViewer(false, Good, Miss), Miss);
	TestEqual(TEXT("The owner keeps takeoff until the landing"), AGFBeatHopCourse::ClipForViewer(false, FName(TEXT("Player.Bhop_Takeoff")), Perfect), FName(TEXT("Player.Bhop_Takeoff")));
	TestEqual(TEXT("The owner in the air is not yanked into an old land"), AGFBeatHopCourse::ClipForViewer(false, Air, Perfect), Air);
	return true;
}

#endif
