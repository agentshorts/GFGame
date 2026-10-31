// Beat Hop traversal kit (#277): beat-synced obstacles, the input buffer, coyote time, the brake tap,
// and the per-run opt-in. Mantle climbs stay pending until #259. Numbers come from UGFMovementTuning::BeatHop.

#include "Misc/AutomationTest.h"
#include "GFBeatHopCourse.h"
#include "GFBeatHopMantle.h"
#include "GFBeatHopNinja.h"
#include "GFBuiltInSongs.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFEconomy.h"
#include "GFMovementPolicy.h"
#include "GFMovementTuning.h"
#include "GFRhythmJudge.h"
#include "GFStaminaSim.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFBeatHopNinjaTests
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
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFBeatHopNinjaWorld"));
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

	static const FGFBeatHopTraversalProfile& Profile()
	{
		return GetDefault<UGFMovementTuning>()->BeatHop;
	}

	static bool Near(float A, float B)
	{
		return FMath::IsNearlyEqual(A, B, 0.01f);
	}

	static AGFBeatHopCourse* StartSong(UWorld* World, EGFBeatDifficulty Difficulty, FString& Why)
	{
		AGFBeatHopCourse* Course = World ? World->SpawnActor<AGFBeatHopCourse>() : nullptr;
		const TArray<FName> Ids = FGFBuiltInSongs::SongIds();
		if (!Course || Ids.Num() == 0)
		{
			Why = TEXT("No course or no built-in song.");
			return nullptr;
		}
		// The course keeps these pointers through world teardown. Heap lifetime outlives this helper.
		FGFTransferLedger* Ledger = new FGFTransferLedger();
		FGFMapLedgerAccounts* Accounts = new FGFMapLedgerAccounts();
		Accounts->Accounts.Add(TEXT("Solo"));
		Course->SetLedgerOverride(Ledger, Accounts);
		if (!Course->StartRunForPlayers(Ids[0], Difficulty, EGFBeatHopMode::Songs, false, 1, Why))
		{
			return nullptr;
		}
		return Course;
	}

	static ACharacter* SpawnRunner(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector::ZeroVector), Params);
	}

	static FGFBeatHopObstacleInstance MakeObstacle(EGFBeatHopObstacle Kind, double BeatTime, int32 SafeBrick, float HeightCm)
	{
		FGFBeatHopObstacleInstance Obstacle;
		Obstacle.Kind = Kind;
		Obstacle.BeatTime = BeatTime;
		Obstacle.SafeBrickIndex = SafeBrick;
		Obstacle.HeightCm = HeightCm;
		return Obstacle;
	}

	static FGFBeatHopNinjaPress MakePress(EGFBeatHopObstacle Kind, double Time, float HeightCm, EGFBeatDifficulty Difficulty, bool bGrounded)
	{
		FGFBeatHopNinjaPress Press;
		Press.Time = Time;
		Press.Move = GFBeatHopNinja::MoveFor(Kind, HeightCm, Difficulty, Profile());
		Press.bGrounded = bGrounded;
		return Press;
	}

	static bool GroundedKind(EGFBeatHopObstacle Kind)
	{
		return Kind == EGFBeatHopObstacle::CrumbleBrick || Kind == EGFBeatHopObstacle::BeatWall;
	}

	/** On-beat Clear is Perfect. A press outside the buffer is Miss. */
	static bool PerfectAndMiss(FAutomationTestBase& Test, EGFBeatHopObstacle Kind, EGFBeatDifficulty Difficulty, float HeightCm)
	{
		FWorld W;
		if (!Test.TestNotNull(TEXT("World"), W.World))
		{
			return false;
		}
		FString Why;
		AGFBeatHopCourse* Course = StartSong(W.World, Difficulty, Why);
		if (!Test.TestNotNull(TEXT("Run"), Course))
		{
			Test.AddError(Why);
			return false;
		}
		const double Period = Course->GetBeatPeriod();
		const double Beat = Period * static_cast<double>(Profile().StationBeatIndex);
		TArray<FGFBeatHopObstacleInstance> Chart;
		Chart.Add(MakeObstacle(Kind, Beat, 0, HeightCm));
		Chart.Add(MakeObstacle(Kind, Beat, 0, HeightCm));
		Course->SetNinjaChart(Chart);
		Test.TestEqual(TEXT("Both obstacles are placed"), Course->NumNinjaObstacles(), static_cast<int32>(2));

		const FGFBeatHopNinjaPress On = MakePress(Kind, Beat, HeightCm, Difficulty, GroundedKind(Kind));
		const EGFJudgement Perfect = Course->JudgeNinja(TEXT("Solo"), 0, On, EGFBeatHopNinjaContact::Clear);
		Test.TestTrue(TEXT("On the beat is Perfect"), Perfect == EGFJudgement::Perfect);
		const FGFRhythmScore* Score = Course->GetScore(TEXT("Solo"));
		Test.TestTrue(TEXT("Perfect counted"), Score && Score->Perfects == 1 && Score->Misses == 0);

		FGFBeatHopNinjaPress Early = On;
		Early.Time = Beat - static_cast<double>(Profile().InputBufferSeconds) - static_cast<double>(Profile().MissMarginSeconds);
		const EGFJudgement Miss = Course->JudgeNinja(TEXT("Solo"), 1, Early, EGFBeatHopNinjaContact::Clear);
		Test.TestTrue(TEXT("Off the beat is Miss"), Miss == EGFJudgement::Miss);
		Score = Course->GetScore(TEXT("Solo"));
		Test.TestTrue(TEXT("Miss counted and the perfect remains"), Score && Score->Perfects == 1 && Score->Misses == 1);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaOptInTest, "GFGame.BeatHop.Ninja.OptIn", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaOptInTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	const FGFBeatHopTraversalProfile& Tuned = Profile();
	TestTrue(TEXT("Profile is the tuning asset"), Near(GFBeatHopNinja::Profile().InputBufferSeconds, Tuned.InputBufferSeconds));
	TestTrue(TEXT("Buffer default is 120 ms"), Near(Tuned.InputBufferSeconds, 0.12f));
	TestTrue(TEXT("Coyote default is 80 ms"), Near(Tuned.CoyoteSeconds, 0.08f));
	TestTrue(TEXT("Brake default is 0.3 s"), Near(Tuned.BrakeTapSeconds, 0.3f));
	TestTrue(TEXT("Air dash is half a beat"), Near(Tuned.AirDashBeats, 0.5f));
	TestTrue(TEXT("A run does not spend stamina"), Tuned.StaminaCostScale <= 0.f);
	TestTrue(TEXT("Wall-run camera pullback"), Near(Tuned.WallRunCameraPullbackCm, 180.f));
	TestTrue(TEXT("Grapple camera pullback"), Near(Tuned.GrappleCameraPullbackCm, 220.f));
	TestFalse(TEXT("Beat Hop map stays off without a run"), GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Beat"), false, false, false));
	TestTrue(TEXT("The run opt-in turns the kit on"), GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Beat"), false, false, true));
	TestTrue(TEXT("The hub stays on with no run"), GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Hub"), false, false, false));

	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Normal, Why);
	if (!TestNotNull(TEXT("Run"), Course))
	{
		AddError(Why);
		return false;
	}
	TestTrue(TEXT("Run opts in"), Course->IsNinjaOptIn());
	TestTrue(TEXT("Course reads the tuning profile"), Near(Course->GetNinjaProfile().BrakeTapSeconds, Tuned.BrakeTapSeconds));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFCharacter* Pawn = W.World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), FTransform(FVector::ZeroVector), Params);
	UGFCharacterMovementComponent* Move = Pawn ? Pawn->GetGFMovement() : nullptr;
	if (TestNotNull(TEXT("Movement"), Move))
	{
		Move->RefreshBeatHop();
		TestTrue(TEXT("Movement sees the run"), Move->IsBeatHopRun());
		TestTrue(TEXT("Kit is enabled for the run"), Move->IsStaminaEnabled());
		TestTrue(TEXT("Camera stays put until a wall run or grapple"), Near(Move->GetBeatHopCameraPullback(), 0.f));
	}

	const UGFMovementTuning& Tuning = *GetDefault<UGFMovementTuning>();
	const float RefSpeed = Course->GetBeatmap().RefSpeedCms;
	const float AirDuration = GFBeatHopNinja::DurationForBeats(Tuned.AirDashBeats, RefSpeed, Tuning.DashSpeed);
	TestTrue(TEXT("Air dash covers half a beat of chart distance"),
		Near(AirDuration * Tuning.DashSpeed, GFBeatHopNinja::ChartDistanceCm(Tuned.AirDashBeats, RefSpeed)));

	const float Step = 0.05f;
	FGFStaminaState State;
	State.ResetFull(Tuning);
	FGFStaminaCommand Command;
	Command.DeltaSeconds = Step;
	Command.bStaminaEnabled = true;
	Command.bGrounded = true;
	Command.bDashPressed = true;
	Command.bCostsFree = true;
	Command.GroundDashDurationOverride = GFBeatHopNinja::DurationForBeats(Tuned.GroundDashBeats, RefSpeed, Tuning.DashSpeed);
	const FGFStaminaState Free = GFStaminaSim::Step(State, Command, Tuning);
	TestTrue(TEXT("Beat Hop dash still fires"), Free.bDashing);
	TestTrue(TEXT("Beat Hop dash is free"), Near(Free.Stamina, Tuning.MaxStamina));
	TestTrue(TEXT("Dash lasts the beat distance"), Near(Free.DashTimeLeft, Command.GroundDashDurationOverride - Step));

	Command.bCostsFree = false;
	Command.GroundDashDurationOverride = -1.f;
	const FGFStaminaState Paid = GFStaminaSim::Step(State, Command, Tuning);
	TestTrue(TEXT("Hub dash still spends stamina"), Paid.Stamina < Tuning.MaxStamina);

	Course->FinishRun();
	TestFalse(TEXT("Opt-in ends with the run"), Course->IsNinjaOptIn());
	if (Move)
	{
		Move->RefreshBeatHop();
		TestFalse(TEXT("Movement drops the run"), Move->IsBeatHopRun());
	}
	TestFalse(TEXT("Beat Hop map is off again"), GFMovementPolicy::IsStaminaEnabled(TEXT("/Game/Maps/L_Beat"), false, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaInputBufferTest, "GFGame.BeatHop.Ninja.InputBuffer", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaInputBufferTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	const FGFBeatHopTraversalProfile& P = Profile();
	const double Note = 4.0;
	const FGFJudgeWindows Windows = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal);
	TestTrue(TEXT("100 ms early is inside the buffer"), GFBeatHopNinja::IsWithinBuffer(Note - 0.1, Note, P));
	TestFalse(TEXT("200 ms early is outside the buffer"), GFBeatHopNinja::IsWithinBuffer(Note - 0.2, Note, P));

	FGFBeatHopInputBuffer Buffer;
	Buffer.Note(Note - 0.1, EGFBeatHopKitMove::WallRun);
	double Found = 0.0;
	TestTrue(TEXT("Buffered press is still there"), Buffer.Find(Note, EGFBeatHopKitMove::WallRun, P, Found));
	TestTrue(TEXT("It is the 100 ms press"), FMath::IsNearlyEqual(Found, Note - 0.1, 0.0001));
	FGFBeatHopInputBuffer Late;
	Late.Note(Note - 0.2, EGFBeatHopKitMove::WallRun);
	TestFalse(TEXT("200 ms press has expired"), Late.Find(Note, EGFBeatHopKitMove::WallRun, P, Found));

	const double PastGood = static_cast<double>(Windows.GoodMs) * static_cast<double>(P.MillisecondsToSeconds) + static_cast<double>(P.TimingSlackSeconds);
	TestTrue(TEXT("Just past the Good window is a Miss without the buffer"),
		FGFRhythmJudge::Classify(-PastGood, Windows) == EGFJudgement::Miss);
	TestTrue(TEXT("The buffer still scores that early press"),
		PastGood <= static_cast<double>(P.InputBufferSeconds)
		&& GFBeatHopNinja::JudgeTiming(Note - PastGood, Note, Windows, P) == EGFJudgement::Good);

	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Normal, Why);
	if (!TestNotNull(TEXT("Run"), Course))
	{
		AddError(Why);
		return false;
	}
	const double Beat = Course->GetBeatPeriod() * static_cast<double>(P.StationBeatIndex);
	TArray<FGFBeatHopObstacleInstance> Chart;
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::WallRunPanel, Beat, 0, 0.f));
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::WallRunPanel, Beat, 0, 0.f));
	Course->SetNinjaChart(Chart);
	FGFBeatHopNinjaPress Early = MakePress(EGFBeatHopObstacle::WallRunPanel, Beat - 0.1, 0.f, EGFBeatDifficulty::Normal, false);
	const EGFJudgement Scored = Course->JudgeNinja(TEXT("Solo"), 0, Early, EGFBeatHopNinjaContact::Clear);
	TestTrue(TEXT("100 ms early still scores"), Scored == EGFJudgement::Perfect || Scored == EGFJudgement::Good);
	FGFBeatHopNinjaPress TooEarly = Early;
	TooEarly.Time = Beat - 0.2;
	TestTrue(TEXT("200 ms early does not score"),
		Course->JudgeNinja(TEXT("Solo"), 1, TooEarly, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Miss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaCoyoteTest, "GFGame.BeatHop.Ninja.Coyote", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaCoyoteTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	const FGFBeatHopTraversalProfile& P = Profile();
	const double Note = 2.0;
	TestTrue(TEXT("Inside the coyote window"), GFBeatHopNinja::IsWithinCoyote(Note - static_cast<double>(P.CoyoteSeconds) + static_cast<double>(P.TimingSlackSeconds), Note, P));
	TestFalse(TEXT("Past the coyote window"), GFBeatHopNinja::IsWithinCoyote(Note - static_cast<double>(P.CoyoteSeconds) - static_cast<double>(P.MissMarginSeconds), Note, P));

	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Easy, Why);
	if (!TestNotNull(TEXT("Run"), Course))
	{
		AddError(Why);
		return false;
	}
	const double Beat = Course->GetBeatPeriod() * static_cast<double>(P.StationBeatIndex);
	TArray<FGFBeatHopObstacleInstance> Chart;
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::CrumbleBrick, Beat, 0, 0.f));
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::CrumbleBrick, Beat, 0, 0.f));
	Course->SetNinjaChart(Chart);

	FGFBeatHopNinjaPress Inside = MakePress(EGFBeatHopObstacle::CrumbleBrick, Beat, 0.f, EGFBeatDifficulty::Easy, false);
	Inside.LeftGroundAt = Beat - static_cast<double>(P.CoyoteSeconds) + static_cast<double>(P.TimingSlackSeconds);
	TestTrue(TEXT("Coyote landing on the beat scores"),
		Course->JudgeNinja(TEXT("Solo"), 0, Inside, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Perfect);

	FGFBeatHopNinjaPress Outside = Inside;
	Outside.LeftGroundAt = Beat - static_cast<double>(P.CoyoteSeconds) - static_cast<double>(P.MissMarginSeconds);
	TestTrue(TEXT("Leaving the edge earlier is a Miss"),
		Course->JudgeNinja(TEXT("Solo"), 1, Outside, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Miss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaBrakeTest, "GFGame.BeatHop.Ninja.Brake", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaBrakeTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Easy, Why);
	ACharacter* Runner = W.World ? SpawnRunner(W.World) : nullptr;
	if (!TestNotNull(TEXT("Run"), Course) || !TestNotNull(TEXT("Runner"), Runner))
	{
		AddError(Why);
		return false;
	}
	UCharacterMovementComponent* Move = Runner->GetCharacterMovement();
	if (!TestNotNull(TEXT("Movement"), Move))
	{
		return false;
	}
	Move->Velocity = FVector(Course->GetBeatmap().RefSpeedCms, 0.f, 0.f);
	Course->SetDebugRunTime(1.0);
	Course->NotifyBrakeTap(Runner);
	TestTrue(TEXT("Brake tap is held"), Course->IsNinjaBraking(Runner, 1.0));
	Course->TickNinjaBrake(Runner);
	TestTrue(TEXT("Brake kills horizontal speed"), Move->Velocity.Size2D() <= static_cast<double>(Profile().BrakeSpeedScale) + 0.01);
	const double Release = 1.0 + static_cast<double>(Profile().BrakeTapSeconds);
	TestFalse(TEXT("Brake ends when the tuned time is up"), Course->IsNinjaBraking(Runner, Release));
	TestTrue(TEXT("Brake can be turned off on the profile"), Profile().bBrakeTapEnabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaWallRunTest, "GFGame.BeatHop.Ninja.WallRunPanel", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaWallRunTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	if (!PerfectAndMiss(*this, EGFBeatHopObstacle::WallRunPanel, EGFBeatDifficulty::Normal, 0.f))
	{
		return false;
	}
	const FGFBeatHopTraversalProfile& P = Profile();
	const double Period = static_cast<double>(P.StationPeriodSeconds);
	const double Beat = Period * static_cast<double>(P.StationBeatIndex);
	FGFBeatHopObstacleInstance Obstacle = MakeObstacle(EGFBeatHopObstacle::WallRunPanel, Beat, 0, 0.f);
	const double Window = static_cast<double>(FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal).GoodMs) * static_cast<double>(P.MillisecondsToSeconds);
	TestTrue(TEXT("Panel glows on the beat"), GFBeatHopNinja::IsGlowing(Obstacle, Beat, Period, Window, P));
	const double Before = Beat - (static_cast<double>(P.GlowLeadBeats) + static_cast<double>(P.HopBeats)) * Period;
	TestFalse(TEXT("Panel is dark before the lead"), GFBeatHopNinja::IsGlowing(Obstacle, Before, Period, Window, P));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaMantleTest, "GFGame.BeatHop.Ninja.MantleLedge", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaMantleTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	const FGFBeatHopTraversalProfile& P = Profile();
	TestTrue(TEXT("1.2 m is an auto mantle"), GFBeatHopNinja::MantleBand(120.f, EGFBeatDifficulty::Easy, P) == EGFBeatHopMantleBand::Auto);
	TestTrue(TEXT("1.5 m is an auto mantle"), GFBeatHopNinja::MantleBand(150.f, EGFBeatDifficulty::Normal, P) == EGFBeatHopMantleBand::Auto);
	TestTrue(TEXT("105 cm step is not a mantle"), GFBeatHopNinja::MantleBand(105.f, EGFBeatDifficulty::Hard, P) == EGFBeatHopMantleBand::None);
	TestTrue(TEXT("2.2 m grab is Hard only"), GFBeatHopNinja::MantleBand(220.f, EGFBeatDifficulty::Hard, P) == EGFBeatHopMantleBand::Grab);
	TestTrue(TEXT("2.2 m is refused on Easy"), GFBeatHopNinja::MantleBand(220.f, EGFBeatDifficulty::Easy, P) == EGFBeatHopMantleBand::TooHigh);
	TestTrue(TEXT("The gap between 1.5 m and 1.8 m is nothing"), GFBeatHopNinja::MantleBand(160.f, EGFBeatDifficulty::Hard, P) == EGFBeatHopMantleBand::TooHigh);
	TestTrue(TEXT("Above 2.2 m is nothing"), GFBeatHopNinja::MantleBand(230.f, EGFBeatDifficulty::Hard, P) == EGFBeatHopMantleBand::TooHigh);
	TestTrue(TEXT("Mantle is free inside a run"), P.bMantleFreeInRun);

	GFBeatHopMantle::FQuery Query;
	Query.HeightCm = P.MantleAutoSampleCm;
	Query.bForwardHeld = true;
	Query.bAirborne = true;
	Query.Difficulty = EGFBeatDifficulty::Easy;
	const GFBeatHopMantle::FResult Band = GFBeatHopMantle::Query(Query, P);
	TestTrue(TEXT("Query would auto-mantle 1.2 m"), Band.bWouldAutoMantle);
	TestFalse(TEXT("Mantle movement is not implemented"), Band.bAvailable);

	if (!GFBeatHopNinja::IsMantleImplemented())
	{
		AddInfo(TEXT("Pending: mantling (#259) is not on staging. Ledges are placed and judging is hooked to GFBeatHopMantle::Query."));
		FWorld W;
		if (!TestNotNull(TEXT("World"), W.World))
		{
			return false;
		}
		FString Why;
		AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Hard, Why);
		if (!TestNotNull(TEXT("Run"), Course))
		{
			AddError(Why);
			return false;
		}
		const double Beat = Course->GetBeatPeriod() * static_cast<double>(P.StationBeatIndex);
		TArray<FGFBeatHopObstacleInstance> Chart;
		Chart.Add(MakeObstacle(EGFBeatHopObstacle::MantleLedge, Beat, 0, 120.f));
		Chart.Add(MakeObstacle(EGFBeatHopObstacle::MantleLedge, Beat, 0, 150.f));
		Chart.Add(MakeObstacle(EGFBeatHopObstacle::MantleLedge, Beat, 0, 220.f));
		Course->SetNinjaChart(Chart);
		TestEqual(TEXT("Three ledges placed"), Course->NumNinjaObstacles(), static_cast<int32>(3));
		const int32 Heights[] = { 0, 1, 2 };
		const float Expected[] = { 120.f, 150.f, 220.f };
		for (int32 Index : Heights)
		{
			TestTrue(TEXT("Ledge keeps its height"), Near(Course->GetNinjaObstacle(Index).HeightCm, Expected[Index]));
			const FGFBeatHopNinjaPress Press = MakePress(EGFBeatHopObstacle::MantleLedge, Beat, Expected[Index], EGFBeatDifficulty::Hard, false);
			TestTrue(TEXT("Pending mantle is not a Miss"),
				Course->JudgeNinja(TEXT("Solo"), Index, Press, EGFBeatHopNinjaContact::Clear) == EGFJudgement::None);
		}
		const FGFRhythmScore* Score = Course->GetScore(TEXT("Solo"));
		TestTrue(TEXT("Pending ledges add no Miss"), Score == nullptr || Score->Misses == 0);
		int32 Pending = 0;
		for (const FGFBeatHopNinjaRep& Row : Course->GetNinjaReps())
		{
			if (Row.bMantlePending != 0)
			{
				++Pending;
			}
		}
		TestEqual(TEXT("Each ledge reports pending"), Pending, static_cast<int32>(3));
		return true;
	}

	return PerfectAndMiss(*this, EGFBeatHopObstacle::MantleLedge, EGFBeatDifficulty::Hard, P.MantleAutoSampleCm);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaGrappleTest, "GFGame.BeatHop.Ninja.GrapplePost", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaGrappleTest::RunTest(const FString&)
{
	return GFBeatHopNinjaTests::PerfectAndMiss(*this, EGFBeatHopObstacle::GrapplePost, EGFBeatDifficulty::Normal, 0.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaDashGateTest, "GFGame.BeatHop.Ninja.DashGate", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaDashGateTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	if (!PerfectAndMiss(*this, EGFBeatHopObstacle::DashGate, EGFBeatDifficulty::Easy, 0.f))
	{
		return false;
	}
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Easy, Why);
	if (!TestNotNull(TEXT("Run"), Course))
	{
		AddError(Why);
		return false;
	}
	const double Period = Course->GetBeatPeriod();
	const double Beat = Period * static_cast<double>(Profile().StationBeatIndex);
	TArray<FGFBeatHopObstacleInstance> Chart;
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::DashGate, Beat, 0, 0.f));
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::DashGate, Beat, 0, 0.f));
	Course->SetNinjaChart(Chart);
	FGFBeatHopNinjaPress Air = MakePress(EGFBeatHopObstacle::DashGate, Beat, 0.f, EGFBeatDifficulty::Easy, false);
	Air.Move = EGFBeatHopKitMove::AirDash;
	TestTrue(TEXT("An air dash through the open gate is Perfect"),
		Course->JudgeNinja(TEXT("Solo"), 0, Air, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Perfect);
	const double Closed = Beat + static_cast<double>(Profile().DashGateOpenBeats) * Period;
	FGFBeatHopNinjaPress Press = MakePress(EGFBeatHopObstacle::DashGate, Closed, 0.f, EGFBeatDifficulty::Easy, false);
	TestFalse(TEXT("Gate has shut"), GFBeatHopNinja::IsDashGateOpen(Course->GetNinjaObstacle(1), Closed, Period, Profile()));
	TestTrue(TEXT("Dashing a shut gate is a Miss"),
		Course->JudgeNinja(TEXT("Solo"), 1, Press, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Miss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaSwivelTest, "GFGame.BeatHop.Ninja.SwivelPole", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaSwivelTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	const double Period = static_cast<double>(Profile().StationPeriodSeconds);
	const double Beat = Period * static_cast<double>(Profile().StationBeatIndex);
	TestTrue(TEXT("Station beat is a downbeat"), GFBeatHopNinja::IsDownbeat(Beat, Period, Profile()));
	return PerfectAndMiss(*this, EGFBeatHopObstacle::SwivelPole, EGFBeatDifficulty::Hard, 0.f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaCrumbleTest, "GFGame.BeatHop.Ninja.CrumbleBrick", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaCrumbleTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	if (!PerfectAndMiss(*this, EGFBeatHopObstacle::CrumbleBrick, EGFBeatDifficulty::Easy, 0.f))
	{
		return false;
	}
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Easy, Why);
	if (!TestNotNull(TEXT("Run"), Course))
	{
		AddError(Why);
		return false;
	}
	const double Period = Course->GetBeatPeriod();
	const double Beat = Period * static_cast<double>(Profile().StationBeatIndex);
	TArray<FGFBeatHopObstacleInstance> Chart;
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::CrumbleBrick, Beat, 0, 0.f));
	Course->SetNinjaChart(Chart);
	const FGFBeatHopNinjaPress Press = MakePress(EGFBeatHopObstacle::CrumbleBrick, Beat, 0.f, EGFBeatDifficulty::Easy, true);
	TestTrue(TEXT("Touch on the beat"), Course->JudgeNinja(TEXT("Solo"), 0, Press, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Perfect);
	const double Delay = static_cast<double>(Profile().CrumbleDelayBeats) * Period;
	TestFalse(TEXT("Brick is still there just before the delay"), Course->IsNinjaObstacleFallen(0, Beat + Delay - static_cast<double>(Profile().TimingSlackSeconds)));
	TestTrue(TEXT("Brick falls after the tuned beats"), Course->IsNinjaObstacleFallen(0, Beat + Delay));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaBeatWallTest, "GFGame.BeatHop.Ninja.BeatWall", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaBeatWallTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Easy, Why);
	if (!TestNotNull(TEXT("Run"), Course))
	{
		AddError(Why);
		return false;
	}
	const double Period = Course->GetBeatPeriod();
	const double Down = Period * static_cast<double>(Profile().BeatWallCycleBeats);
	const double Up = Period * static_cast<double>(Profile().BeatWallHoldBeats);
	TestFalse(TEXT("Wall is down on the even beat"), GFBeatHopNinja::IsBeatWallRaised(Down, Period, Profile()));
	TestTrue(TEXT("Wall is up on the next beat"), GFBeatHopNinja::IsBeatWallRaised(Up, Period, Profile()));

	TArray<FGFBeatHopObstacleInstance> Chart;
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::BeatWall, Down, 0, 0.f));
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::BeatWall, Down, 0, 0.f));
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::BeatWall, Up, 0, 0.f));
	Course->SetNinjaChart(Chart);

	const FGFBeatHopNinjaPress Clear = MakePress(EGFBeatHopObstacle::BeatWall, Down, 0.f, EGFBeatDifficulty::Easy, true);
	TestTrue(TEXT("Passing a lowered wall on the beat is Perfect"),
		Course->JudgeNinja(TEXT("Solo"), 0, Clear, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Perfect);
	FGFBeatHopNinjaPress Early = Clear;
	Early.Time = Down - static_cast<double>(Profile().InputBufferSeconds) - static_cast<double>(Profile().MissMarginSeconds);
	TestTrue(TEXT("Off the beat is Miss"),
		Course->JudgeNinja(TEXT("Solo"), 1, Early, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Miss);
	const FGFBeatHopNinjaPress Raised = MakePress(EGFBeatHopObstacle::BeatWall, Up, 0.f, EGFBeatDifficulty::Easy, true);
	TestTrue(TEXT("Hitting a raised wall is Miss"),
		Course->JudgeNinja(TEXT("Solo"), 2, Raised, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Miss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaRespawnTest, "GFGame.BeatHop.Ninja.Respawn", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaRespawnTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Normal, Why);
	ACharacter* Runner = W.World ? SpawnRunner(W.World) : nullptr;
	if (!TestNotNull(TEXT("Run"), Course) || !TestNotNull(TEXT("Runner"), Runner) || !TestTrue(TEXT("Bricks"), Course->NumBricks() > 2))
	{
		AddError(Why);
		return false;
	}
	const double Beat = Course->GetBeatPeriod() * static_cast<double>(Profile().StationBeatIndex);
	TArray<FGFBeatHopObstacleInstance> Chart;
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::WallRunPanel, Beat, 1, 0.f));
	Course->SetNinjaChart(Chart);
	TestTrue(TEXT("Respawn is the brick before the panel"), Course->RespawnBeforeObstacle(Runner, TEXT("Solo"), 0));
	TestEqual(TEXT("Held on the safe brick, not the panel"), Course->GetHoldBrick(Runner), static_cast<int32>(1));
	const FVector SafeTop = Course->GetBrickTop(1);
	const FVector Stood = Runner->GetActorLocation();
	const double Lift = static_cast<double>(Profile().RespawnAboveBrickCm);
	TestTrue(TEXT("Stands on the safe brick"),
		FVector::Dist2D(Stood, SafeTop) < 1.0 && FMath::Abs(Stood.Z - (SafeTop.Z + Lift)) < 1.0);
	const FGFRhythmScore* Score = Course->GetScore(TEXT("Solo"));
	TestTrue(TEXT("Respawn adds no Miss"), Score && Score->Misses == 0);
	const FGFBeatHopNinjaPress Press = MakePress(EGFBeatHopObstacle::WallRunPanel, Beat, 0.f, EGFBeatDifficulty::Normal, false);
	TestTrue(TEXT("The next on-beat wall run still lands"),
		Course->JudgeNinja(TEXT("Solo"), 0, Press, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Perfect);
	Score = Course->GetScore(TEXT("Solo"));
	TestTrue(TEXT("Still no Miss"), Score && Score->Misses == 0 && Score->Perfects == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaNetTest, "GFGame.BeatHop.Ninja.Net", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaNetTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Host = StartSong(W.World, EGFBeatDifficulty::Normal, Why);
	AGFBeatHopCourse* Client = W.World ? W.World->SpawnActor<AGFBeatHopCourse>() : nullptr;
	if (!TestNotNull(TEXT("Host"), Host) || !TestNotNull(TEXT("Client"), Client))
	{
		AddError(Why);
		return false;
	}
	const double Beat = Host->GetBeatPeriod() * static_cast<double>(Profile().StationBeatIndex);
	TArray<FGFBeatHopObstacleInstance> Chart;
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::WallRunPanel, Beat, 0, 0.f));
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::GrapplePost, Beat, 0, 0.f));
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::MantleLedge, Beat, 0, Profile().MantleAutoSampleCm));
	Host->SetNinjaChart(Chart);
	Client->SetNinjaChart(Chart);

	const FGFBeatHopNinjaPress Wall = MakePress(EGFBeatHopObstacle::WallRunPanel, Beat, 0.f, EGFBeatDifficulty::Normal, false);
	const FGFBeatHopNinjaPress Grapple = MakePress(EGFBeatHopObstacle::GrapplePost, Beat, 0.f, EGFBeatDifficulty::Normal, false);
	const FGFBeatHopNinjaPress Mantle = MakePress(EGFBeatHopObstacle::MantleLedge, Beat, Profile().MantleAutoSampleCm, EGFBeatDifficulty::Normal, false);
	TestTrue(TEXT("Host wall run"), Host->JudgeNinja(TEXT("Solo"), 0, Wall, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Perfect);
	TestTrue(TEXT("Host grapple"), Host->JudgeNinja(TEXT("Solo"), 1, Grapple, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Perfect);
	TestTrue(TEXT("Host mantle is pending, not a Miss"),
		Host->JudgeNinja(TEXT("Solo"), 2, Mantle, EGFBeatHopNinjaContact::Clear) == EGFJudgement::None);

	Client->ReceiveRemoteNinja(Host->GetNinjaReps());
	bool bSawWall = false;
	bool bSawGrapple = false;
	bool bSawMantle = false;
	for (const FGFBeatHopNinjaRep& Row : Client->GetNinjaReps())
	{
		bSawWall = bSawWall || Row.bWallRun != 0;
		bSawGrapple = bSawGrapple || Row.bGrapple != 0;
		bSawMantle = bSawMantle || Row.bMantle != 0;
	}
	TestTrue(TEXT("Client sees the wall run"), bSawWall);
	TestTrue(TEXT("Client sees the grapple"), bSawGrapple);
	TestTrue(TEXT("Client sees the mantle"), bSawMantle);

	const UGFMovementTuning& Tuning = *GetDefault<UGFMovementTuning>();
	FGFStaminaState HostSim;
	HostSim.ResetFull(Tuning);
	FGFStaminaCommand Run;
	Run.DeltaSeconds = 0.05f;
	Run.bStaminaEnabled = true;
	Run.bGrounded = false;
	Run.bCostsFree = true;
	Run.HorizontalSpeed = Tuning.WallRunMinSpeed + Profile().TimingSlackSeconds;
	Run.MoveDirection = FVector(0.f, 1.f, 0.f);
	Run.Contact.bWallContact = true;
	Run.Contact.WallNormal = FVector(1.f, 0.f, 0.f);
	const FGFStaminaState HostWall = GFStaminaSim::Step(HostSim, Run, Tuning);
	const FGFStaminaState ClientWall = GFStaminaSim::Step(HostSim, Run, Tuning);
	TestTrue(TEXT("Both machines wall run"), HostWall.bWallRunning && ClientWall.bWallRunning);
	const float HostQuantized = FMath::RoundToFloat(HostWall.Stamina * 2.f) * 0.5f;
	const float ClientQuantized = FMath::RoundToFloat(ClientWall.Stamina * 2.f) * 0.5f;
	TestTrue(TEXT("Wall run would not correct"), Near(HostQuantized, ClientQuantized));

	FGFStaminaCommand Swing = Run;
	Swing.Contact.bWallContact = false;
	Swing.bGrappleSwingHeld = true;
	Swing.Contact.bGrappleHit = true;
	Swing.PlayerLocation = FVector(0.f, 0.f, 100.f);
	Swing.Contact.GrapplePoint = FVector(800.f, 0.f, 100.f);
	Swing.Velocity = FVector(0.f, 400.f, 0.f);
	const FGFStaminaState HostSwing = GFStaminaSim::Step(HostSim, Swing, Tuning);
	const FGFStaminaState ClientSwing = GFStaminaSim::Step(HostSim, Swing, Tuning);
	TestTrue(TEXT("Both machines grapple"), HostSwing.bSwinging && ClientSwing.bSwinging);
	TestTrue(TEXT("Grapple would not correct"),
		Near(FMath::RoundToFloat(HostSwing.Stamina * 2.f) * 0.5f, FMath::RoundToFloat(ClientSwing.Stamina * 2.f) * 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaStationsTest, "GFGame.BeatHop.Ninja.Stations", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaStationsTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFBeatHopNinjaRange* Range = W.World->SpawnActor<AGFBeatHopNinjaRange>();
	if (!TestNotNull(TEXT("Range"), Range))
	{
		return false;
	}
	const TArray<FGFBeatHopNinjaStation>& Catalog = GFBeatHopNinja::Stations();
	TestEqual(TEXT("One station per obstacle"), Range->NumStations(), Catalog.Num());
	TestTrue(TEXT("Catalog is not empty"), Catalog.Num() > 0);
	for (int32 Index = 0; Index < Range->NumStations(); ++Index)
	{
		const FGFBeatHopNinjaStation& Station = Range->GetStation(Index);
		TestEqual(TEXT("Station kind matches the catalog"), static_cast<int32>(Station.Kind), static_cast<int32>(Catalog[Index].Kind));
		EGFBeatDifficulty Difficulty = EGFBeatDifficulty::Hard;
		const EGFBeatHopKitMove Move = GFBeatHopNinja::MoveFor(Station.Kind, Station.HeightCm, EGFBeatDifficulty::Easy, Profile());
		if (GFBeatHopNinja::Allows(EGFBeatDifficulty::Easy, Move, Profile()))
		{
			Difficulty = EGFBeatDifficulty::Easy;
		}
		else if (GFBeatHopNinja::Allows(EGFBeatDifficulty::Normal, Move, Profile()))
		{
			Difficulty = EGFBeatDifficulty::Normal;
		}
		const FGFBeatHopNinjaVerdict On = Range->JudgeStation(Index, true, Difficulty);
		const FGFBeatHopNinjaVerdict Off = Range->JudgeStation(Index, false, Difficulty);
		if (Station.Kind == EGFBeatHopObstacle::MantleLedge && !GFBeatHopNinja::IsMantleImplemented())
		{
			AddInfo(TEXT("Pending: mantle station waits on #259."));
			TestTrue(TEXT("Mantle station is pending on the beat"), On.bPendingMantle);
			TestTrue(TEXT("Mantle station is pending off the beat"), Off.bPendingMantle);
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s on the beat"), *Station.Id.ToString()), On.Judgement == EGFJudgement::Perfect);
		TestTrue(*FString::Printf(TEXT("%s off the beat"), *Station.Id.ToString()), Off.Judgement == EGFJudgement::Miss);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopNinjaDifficultyTest, "GFGame.BeatHop.Ninja.Difficulty", GFBeatHopNinjaTests::Flags)
bool FGFBeatHopNinjaDifficultyTest::RunTest(const FString&)
{
	using namespace GFBeatHopNinjaTests;
	const FGFBeatHopTraversalProfile& P = Profile();
	TestTrue(TEXT("Easy gets mantle ledges"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Easy, EGFBeatHopKitMove::MantleLedge, P));
	TestTrue(TEXT("Easy gets dash gates"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Easy, EGFBeatHopKitMove::DashGate, P));
	TestFalse(TEXT("Easy does not wall run"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Easy, EGFBeatHopKitMove::WallRun, P));
	TestFalse(TEXT("Easy does not grapple"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Easy, EGFBeatHopKitMove::Grapple, P));
	TestTrue(TEXT("Normal wall runs"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Normal, EGFBeatHopKitMove::WallRun, P));
	TestTrue(TEXT("Normal grapples"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Normal, EGFBeatHopKitMove::Grapple, P));
	TestFalse(TEXT("Normal does not grab ledges"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Normal, EGFBeatHopKitMove::LedgeGrab, P));
	TestTrue(TEXT("Hard grabs ledges"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Hard, EGFBeatHopKitMove::LedgeGrab, P));
	TestTrue(TEXT("Hard swivels"), GFBeatHopNinja::Allows(EGFBeatDifficulty::Hard, EGFBeatHopKitMove::Swivel, P));
	TestEqual(TEXT("Easy is one hop"), GFBeatHopNinja::JumpsFor(EGFBeatDifficulty::Easy, P), static_cast<int32>(1));
	TestEqual(TEXT("Hard is the triple jump"), GFBeatHopNinja::JumpsFor(EGFBeatDifficulty::Hard, P), static_cast<int32>(3));

	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FString Why;
	AGFBeatHopCourse* Course = StartSong(W.World, EGFBeatDifficulty::Easy, Why);
	if (!TestNotNull(TEXT("Easy run"), Course))
	{
		AddError(Why);
		return false;
	}
	const double Beat = Course->GetBeatPeriod() * static_cast<double>(P.StationBeatIndex);
	TArray<FGFBeatHopObstacleInstance> Chart;
	Chart.Add(MakeObstacle(EGFBeatHopObstacle::WallRunPanel, Beat, 0, 0.f));
	Course->SetNinjaChart(Chart);
	const FGFBeatHopNinjaPress Press = MakePress(EGFBeatHopObstacle::WallRunPanel, Beat, 0.f, EGFBeatDifficulty::Easy, false);
	TestTrue(TEXT("Easy wall run on the beat is still a Miss"),
		Course->JudgeNinja(TEXT("Solo"), 0, Press, EGFBeatHopNinjaContact::Clear) == EGFJudgement::Miss);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFCharacter* Pawn = W.World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), FTransform(FVector::ZeroVector), Params);
	UGFCharacterMovementComponent* Move = Pawn ? Pawn->GetGFMovement() : nullptr;
	if (!TestNotNull(TEXT("Movement"), Move))
	{
		return false;
	}
	Move->RefreshBeatHop();
	FGFStaminaCommand Masked;
	Masked.bDashPressed = true;
	Masked.bGrappleSwingHeld = true;
	Masked.bGrappleYankPressed = true;
	Masked.Contact.bWallContact = true;
	Masked.Contact.bGrappleHit = true;
	Masked.Contact.bSwivelContact = true;
	Masked.Contact.bDashImpact = true;
	Move->ApplyBeatHopMoveMask(Masked);
	TestFalse(TEXT("Easy cannot wall run"), Masked.Contact.bWallContact);
	TestFalse(TEXT("Easy cannot grapple"), Masked.bGrappleSwingHeld || Masked.Contact.bGrappleHit);
	TestFalse(TEXT("Easy cannot swivel"), Masked.Contact.bSwivelContact);
	TestFalse(TEXT("Easy cannot dash-grip"), Masked.Contact.bDashImpact);
	TestTrue(TEXT("Easy still dashes through a gate"), Masked.bDashPressed);

	const UGFMovementTuning& Tuning = *GetDefault<UGFMovementTuning>();
	FGFStaminaState EasyHop;
	EasyHop.ResetFull(Tuning);
	FGFStaminaCommand Hop;
	Hop.DeltaSeconds = 0.05f;
	Hop.bStaminaEnabled = true;
	Hop.bCostsFree = true;
	Hop.bGrounded = true;
	Hop.bJumpPressed = true;
	Hop.JumpsPerAirtimeOverride = GFBeatHopNinja::JumpsFor(EGFBeatDifficulty::Easy, P);
	EasyHop = GFStaminaSim::Step(EasyHop, Hop, Tuning);
	TestTrue(TEXT("Easy hop is jump 1"), EasyHop.bJumpAccepted && EasyHop.JumpsUsed == 1);
	Hop.bGrounded = false;
	EasyHop = GFStaminaSim::Step(EasyHop, Hop, Tuning);
	TestFalse(TEXT("Easy has no second jump"), EasyHop.bJumpAccepted);

	FGFStaminaState HardHop;
	HardHop.ResetFull(Tuning);
	Hop.bGrounded = true;
	Hop.JumpsPerAirtimeOverride = GFBeatHopNinja::JumpsFor(EGFBeatDifficulty::Hard, P);
	HardHop = GFStaminaSim::Step(HardHop, Hop, Tuning);
	Hop.bGrounded = false;
	HardHop = GFStaminaSim::Step(HardHop, Hop, Tuning);
	HardHop = GFStaminaSim::Step(HardHop, Hop, Tuning);
	TestTrue(TEXT("Hard reaches the triple jump"), HardHop.bJumpAccepted && HardHop.JumpsUsed == 3);
	TestTrue(TEXT("The triple jump is free in a run"), Near(HardHop.Stamina, Tuning.MaxStamina));
	HardHop = GFStaminaSim::Step(HardHop, Hop, Tuning);
	TestFalse(TEXT("Hard stops at three"), HardHop.bJumpAccepted);

	Course->FinishRun();
	AGFBeatHopCourse* HardCourse = StartSong(W.World, EGFBeatDifficulty::Hard, Why);
	if (!TestNotNull(TEXT("Hard run"), HardCourse))
	{
		AddError(Why);
		return false;
	}
	Move->RefreshBeatHop();
	TestTrue(TEXT("Movement follows the hard run"), Move->IsBeatHopRun());
	FGFStaminaCommand HardMask;
	HardMask.Contact.bWallContact = true;
	HardMask.bGrappleSwingHeld = true;
	HardMask.Contact.bGrappleHit = true;
	HardMask.Contact.bSwivelContact = true;
	HardMask.Contact.bDashImpact = true;
	HardMask.bDashPressed = true;
	Move->ApplyBeatHopMoveMask(HardMask);
	TestTrue(TEXT("Hard keeps the wall run"), HardMask.Contact.bWallContact);
	TestTrue(TEXT("Hard keeps the grapple"), HardMask.bGrappleSwingHeld && HardMask.Contact.bGrappleHit);
	TestTrue(TEXT("Hard keeps the swivel"), HardMask.Contact.bSwivelContact);
	TestTrue(TEXT("Hard keeps the dash grip"), HardMask.Contact.bDashImpact);
	return true;
}

#endif
