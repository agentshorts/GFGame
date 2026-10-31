// Hand-shaped Beat Hop courses (#278). Each course loads from data, shows a pictogram and two return gates,
// and a scripted run clears every brick and obstacle on the paired song.

#include "Misc/AutomationTest.h"
#include "GFBeatHopCourse.h"
#include "GFBeatHopMapGates.h"
#include "GFBeatHopMaps.h"
#include "GFBeatHopNinja.h"
#include "GFBuiltInSongs.h"
#include "GFEconomy.h"
#include "GFHubGateDirector.h"
#include "GFLog.h"
#include "GFReturnToHub.h"
#include "GFRhythmJudge.h"
#include "Components/BoxComponent.h"
#include "Containers/Set.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFBeatHopMapTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FBeatWorld
	{
		UWorld* World = nullptr;

		explicit FBeatWorld(const TCHAR* Name = TEXT("GFBeatHopMapWorld"))
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FBeatWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static AGFBeatHopCourse* Place(UWorld* World)
	{
		AGFHubGateDirector* Director = World->SpawnActor<AGFHubGateDirector>();
		if (!Director)
		{
			return nullptr;
		}
		Director->Place(TEXT("/Game/Maps/L_Beat"), FVector::ZeroVector);
		for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static bool ClearedObstacle(const AGFBeatHopCourse& Course, int32 Index, const FString& Player)
	{
		const bool bMantle = Course.GetNinjaObstacle(Index).Kind == EGFBeatHopObstacle::MantleLedge;
		for (const FGFBeatHopNinjaRep& Rep : Course.GetNinjaReps())
		{
			if (Rep.ObstacleIndex != Index || Rep.PlayerKey != Player)
			{
				continue;
			}
			if (bMantle)
			{
				return Rep.bMantlePending != 0;
			}
			return Rep.Result == EGFJudgement::Perfect || Rep.Result == EGFJudgement::Good;
		}
		return false;
	}

	static bool RunCourse(FAutomationTestBase& Test, int32 Number)
	{
		const FGFBeatHopMapDef* Map = GFBeatHopMaps::ByNumber(Number);
		if (!Test.TestNotNull(TEXT("Course file"), Map))
		{
			return false;
		}
		FBeatWorld W(*FString::Printf(TEXT("GFBeatHopMap%d"), Number));
		AGFBeatHopCourse* Course = W.World ? Place(W.World) : nullptr;
		if (!Test.TestNotNull(TEXT("Course actor"), Course))
		{
			return false;
		}
		Test.TestEqual(TEXT("Fern row stays three tiles"), Course->NumCourseTiles(), 3);
		Test.TestEqual(TEXT("Map row is Free Hop plus three courses"), Course->NumMapTiles(), 4);
		const FString Loaded = GFBeatHopMaps::LoadCourse(W.World, Number);
		if (!Test.TestTrue(TEXT("gf.BeatHop.Course loads it"), Loaded.IsEmpty()))
		{
			Test.AddError(Loaded);
			return false;
		}
		Test.TestEqual(TEXT("Selected map"), Course->GetSelectedMapId(), Map->Id);
		Test.TestEqual(TEXT("Paired song"), Course->GetSelectedSong(), Map->DefaultSong);
		Test.TestEqual(TEXT("Arrival prompt"), Course->GetArrivalPrompt(), FString(TEXT("Walk through the start gate.")));
		Test.TestTrue(TEXT("Pictogram at the start"), Course->HasCourseSign());
		Test.TestEqual(TEXT("Course number"), Course->GetCourseSignNumber(), Number);
		Test.TestEqual(TEXT("Difficulty pips"), Course->GetCourseSignPips(), static_cast<int32>(Map->DisplayDifficulty) + 1);
		Test.TestTrue(TEXT("Move marks"), Course->GetCourseSignMoveMarks() >= 4);
		Test.TestEqual(TEXT("Section signs"), Course->NumSectionSigns(), Map->Sections.Num());
		Test.TestEqual(TEXT("World label"), Course->GetCourseSignName(), Map->Title);
		Test.TestNotNull(TEXT("Start pad gate"), Course->GetStartGate());
		Test.TestNotNull(TEXT("Start trigger"), Course->GetStartTrigger());
		Test.TestNotNull(TEXT("Finish gate"), Course->GetFinishGate());
		Test.TestNotNull(TEXT("Finish trigger"), Course->GetFinishTrigger());
		Test.TestTrue(TEXT("A checkpoint stands on the course"), Course->NumCheckpoints() > 0);
		if (Map->Sections.Num() >= 2)
		{
			const double FirstSection = Map->Sections[1].StartBeat * 60.0 / FMath::Max(Map->ReferenceBpm, 1.f);
			Test.TestTrue(TEXT("First section is under a minute"), FirstSection > 0.0 && FirstSection < 60.0);
		}

		Course->SetDebugServerTime(1000.0 + Number * 100.0);
		FString Why;
		if (!Test.TestTrue(TEXT("Run starts on the paired song"),
			Course->StartRun(Map->DefaultSong, Map->DisplayDifficulty, EGFBeatHopMode::Songs, false, Why)))
		{
			Test.AddError(Why);
			return false;
		}
		Test.TestEqual(TEXT("Layout BPM"), Course->GetRunLayoutBpm(), Map->ReferenceBpm, 0.1f);
		Test.TestEqual(TEXT("Spawned pieces"), Course->NumBricks() + Course->NumNinjaObstacles(), GFBeatHopMaps::ObstacleCount(*Map));
		const FString Player(TEXT("Solo"));
		if (!Test.TestTrue(TEXT("Scripted clear"), Course->ScriptClearCourse(Player)))
		{
			return false;
		}
		Test.TestTrue(TEXT("Passed"), Course->DidPass());
		Test.TestFalse(TEXT("Run ended"), Course->IsRunning());
		TSet<int32> Kinds;
		for (int32 Index = 0; Index < Course->NumNinjaObstacles(); ++Index)
		{
			Kinds.Add(static_cast<int32>(Course->GetNinjaObstacle(Index).Kind));
			if (!Test.TestTrue(TEXT("Obstacle cleared"), ClearedObstacle(*Course, Index, Player)))
			{
				Test.AddError(FString::Printf(TEXT("Obstacle %d was not cleared"), Index));
				return false;
			}
		}
		Test.TestTrue(TEXT("Every obstacle type on the course was hit"), Kinds.Num() > 0);
		if (!Test.TestTrue(TEXT("A result row"), Course->GetResults().Num() == 1))
		{
			return false;
		}
		const FGFBeatHopResultRow& Row = Course->GetResults()[0];
		Test.TestTrue(TEXT("Result passed"), Row.bPassed);
		Test.TestEqual(TEXT("No misses"), Row.Misses, 0);
		Test.TestEqual(TEXT("Course name on the result"), Row.MapTitle, Map->Title);
		Test.TestEqual(TEXT("Par on the result"), Row.ParScore, Map->ParScore);
		Test.TestTrue(TEXT("Score meets par"), Row.Score >= Map->ParScore);
		Test.TestTrue(TEXT("Grade B or better"), static_cast<uint8>(Row.Grade) >= static_cast<uint8>(EGFBeatHopGrade::B));
		Test.TestTrue(TEXT("Best recorded"), Row.BestScore >= Row.Score);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopCourse1CompleteTest, "GFGame.BeatHop.Course.1.Complete", GFBeatHopMapTests::Flags)
bool FGFBeatHopCourse1CompleteTest::RunTest(const FString&)
{
	return GFBeatHopMapTests::RunCourse(*this, 1);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopCourse2CompleteTest, "GFGame.BeatHop.Course.2.Complete", GFBeatHopMapTests::Flags)
bool FGFBeatHopCourse2CompleteTest::RunTest(const FString&)
{
	return GFBeatHopMapTests::RunCourse(*this, 2);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopCourse3CompleteTest, "GFGame.BeatHop.Course.3.Complete", GFBeatHopMapTests::Flags)
bool FGFBeatHopCourse3CompleteTest::RunTest(const FString&)
{
	return GFBeatHopMapTests::RunCourse(*this, 3);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopMapDifficultyTest, "GFGame.BeatHop.Map.Difficulty", GFBeatHopMapTests::Flags)
bool FGFBeatHopMapDifficultyTest::RunTest(const FString&)
{
	using namespace GFBeatHopMapTests;
	if (!TestEqual(TEXT("Three course files"), GFBeatHopMaps::All().Num(), 3))
	{
		return false;
	}
	const FGFBeatHopMapDef* A = GFBeatHopMaps::ByNumber(1);
	const FGFBeatHopMapDef* B = GFBeatHopMaps::ByNumber(2);
	const FGFBeatHopMapDef* C = GFBeatHopMaps::ByNumber(3);
	if (!TestNotNull(TEXT("Course 1"), A) || !TestNotNull(TEXT("Course 2"), B) || !TestNotNull(TEXT("Course 3"), C))
	{
		return false;
	}
	const FString LineA = GFBeatHopMaps::Describe(*A);
	const FString LineB = GFBeatHopMaps::Describe(*B);
	const FString LineC = GFBeatHopMaps::Describe(*C);
	AddInfo(LineA);
	AddInfo(LineB);
	AddInfo(LineC);
	UE_LOG(LogGF, Log, TEXT("Beat Hop difficulty: %s"), *LineA);
	UE_LOG(LogGF, Log, TEXT("Beat Hop difficulty: %s"), *LineB);
	UE_LOG(LogGF, Log, TEXT("Beat Hop difficulty: %s"), *LineC);
	TestTrue(TEXT("Obstacle count rises"), GFBeatHopMaps::ObstacleCount(*A) < GFBeatHopMaps::ObstacleCount(*B)
		&& GFBeatHopMaps::ObstacleCount(*B) < GFBeatHopMaps::ObstacleCount(*C));
	TestTrue(TEXT("Move variety rises"), GFBeatHopMaps::MoveVariety(*A) < GFBeatHopMaps::MoveVariety(*B)
		&& GFBeatHopMaps::MoveVariety(*B) < GFBeatHopMaps::MoveVariety(*C));
	TestTrue(TEXT("Raw windows get tighter"), GFBeatHopMaps::AverageWindowMs(*A) > GFBeatHopMaps::AverageWindowMs(*B)
		&& GFBeatHopMaps::AverageWindowMs(*B) > GFBeatHopMaps::AverageWindowMs(*C));
	TestTrue(TEXT("Timing demand rises"), GFBeatHopMaps::TimingDemand(*A) < GFBeatHopMaps::TimingDemand(*B)
		&& GFBeatHopMaps::TimingDemand(*B) < GFBeatHopMaps::TimingDemand(*C));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopMapSongFitTest, "GFGame.BeatHop.Map.SongFit", GFBeatHopMapTests::Flags)
bool FGFBeatHopMapSongFitTest::RunTest(const FString&)
{
	using namespace GFBeatHopMapTests;
	const FGFBeatHopMapDef* Easy = GFBeatHopMaps::ByNumber(1);
	if (!TestNotNull(TEXT("Course 1"), Easy))
	{
		return false;
	}
	FString Msg;
	TestTrue(TEXT("Reference BPM fits"), GFBeatHopMaps::Fits(*Easy, Easy->ReferenceBpm, Msg));
	TestFalse(TEXT("Zero BPM is refused"), GFBeatHopMaps::Fits(*Easy, 0.f, Msg));
	TestTrue(TEXT("Zero BPM names the range"), Msg.Contains(TEXT("BPM")) && Msg.Contains(Easy->Title));
	TestFalse(TEXT("A tempo above the range is refused"), GFBeatHopMaps::Fits(*Easy, Easy->BpmMax + 20.f, Msg));
	TestTrue(TEXT("The refusal names the heard tempo"), Msg.Contains(TEXT("This one is")));

	FGFBeatHopMapBuilt Slow;
	FGFBeatHopMapBuilt Fast;
	FString Error;
	TestTrue(TEXT("Build at the slow limit"), GFBeatHopMaps::Build(*Easy, Easy->BpmMin, Slow, Error));
	TestTrue(TEXT("Build at the fast limit"), GFBeatHopMaps::Build(*Easy, Easy->BpmMax, Fast, Error));
	if (Slow.Beatmap.Notes.Num() > 0 && Fast.Beatmap.Notes.Num() > 0)
	{
		const double SlowX = Slow.Beatmap.BeatToDistanceCm(Slow.Beatmap.Notes.Last().Beat);
		const double FastX = Fast.Beatmap.BeatToDistanceCm(Fast.Beatmap.Notes.Last().Beat);
		TestTrue(TEXT("A slower tempo spreads the course out"), SlowX > FastX + 100.0);
	}

	FBeatWorld W(TEXT("GFBeatHopSongFit"));
	AGFBeatHopCourse* Course = W.World ? Place(W.World) : nullptr;
	if (!TestNotNull(TEXT("Course"), Course) || !TestTrue(TEXT("Select course 1"), Course->SelectMap(Easy->Id)))
	{
		return false;
	}
	const FName Paired = Course->GetSelectedSong();
	TestFalse(TEXT("Difficulty tiles are locked"), Course->SelectDifficulty(EGFBeatDifficulty::Hard));
	TestTrue(TEXT("Display difficulty stays"), Course->GetSelectedDifficulty() == Easy->DisplayDifficulty);
	TestFalse(TEXT("Moonlit Moss is too fast for course 1"), Course->SelectSong(TEXT("MoonMoss")));
	TestEqual(TEXT("The paired song stays"), Course->GetSelectedSong(), Paired);
	TestTrue(TEXT("The pad says why"), Course->GetSongRefuseReason().Contains(TEXT("BPM")));

	FString Why;
	TestFalse(TEXT("Live mode with no tempo in range does not start"),
		Course->StartRun(Easy->DefaultSong, Easy->DisplayDifficulty, EGFBeatHopMode::Live, false, Why));
	TestFalse(TEXT("Still idle"), Course->IsRunning());
	TestTrue(TEXT("Live refusal names the tempo"), Why.Contains(TEXT("BPM")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopMapGatesTest, "GFGame.BeatHop.Map.Gates", GFBeatHopMapTests::Flags)
bool FGFBeatHopMapGatesTest::RunTest(const FString&)
{
	using namespace GFBeatHopMapTests;
	FBeatWorld W(TEXT("GFBeatHopMapGates"));
	AGFBeatHopCourse* Course = W.World ? Place(W.World) : nullptr;
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	AGFReturnGate* Canonical = AGFReturnGate::Find(W.World, TEXT("BeatHop"));
	if (!TestNotNull(TEXT("Canonical return gate"), Canonical))
	{
		return false;
	}
	TestTrue(TEXT("Canonical gate stays at the map spot"), Canonical->GetActorLocation().Equals(AGFReturnGate::MapLocation(), 1.f));

	auto CountTag = [&](FName Tag) -> int32
	{
		int32 Count = 0;
		for (TActorIterator<AGFReturnGate> It(W.World); It; ++It)
		{
			if (It->GetModeId() == FName(TEXT("BeatHop")) && It->ActorHasTag(Tag))
			{
				++Count;
			}
		}
		return Count;
	};
	TestEqual(TEXT("One start return gate"), CountTag(GFBeatHopMapGates::StartTag()), 1);
	TestEqual(TEXT("One finish return gate"), CountTag(GFBeatHopMapGates::FinishTag()), 1);

	for (int32 Number = 1; Number <= 3; ++Number)
	{
		const FGFBeatHopMapDef* Map = GFBeatHopMaps::ByNumber(Number);
		if (!TestNotNull(TEXT("Map"), Map) || !TestTrue(TEXT("Select"), Course->SelectMap(Map->Id)))
		{
			return false;
		}
		TestTrue(TEXT("Canonical gate is not moved"), Canonical->GetActorLocation().Equals(AGFReturnGate::MapLocation(), 1.f));
		TestFalse(TEXT("Canonical gate is not a course gate"), Canonical->ActorHasTag(GFBeatHopMapGates::StartTag()));
		AGFReturnGate* Start = nullptr;
		AGFReturnGate* Finish = nullptr;
		for (TActorIterator<AGFReturnGate> It(W.World); It; ++It)
		{
			if (It->ActorHasTag(GFBeatHopMapGates::StartTag())) { Start = *It; }
			if (It->ActorHasTag(GFBeatHopMapGates::FinishTag())) { Finish = *It; }
		}
		if (!TestNotNull(TEXT("Start return"), Start) || !TestNotNull(TEXT("Finish return"), Finish) || !TestNotNull(TEXT("Start mesh"), Course->GetStartGate()) || !TestNotNull(TEXT("Finish mesh"), Course->GetFinishGate()))
		{
			return false;
		}
		const double StartDist = FVector::Dist(Start->GetActorLocation(), Course->GetStartGate()->GetComponentLocation());
		const double FinishDist = FVector::Dist(Finish->GetActorLocation(), Course->GetFinishGate()->GetComponentLocation());
		TestTrue(TEXT("Start return stands by the start gate"), StartDist > 200.0 && StartDist < 600.0);
		TestTrue(TEXT("Finish return stands past the finish"), FinishDist > 200.0 && FinishDist < 800.0);
		TestTrue(TEXT("Start return is up on the course"), FMath::Abs(Start->GetActorLocation().Z - Course->GetActorLocation().Z) < 50.0);
		TestTrue(TEXT("Same hub rules"), Start->IsMapTravel() && Finish->IsMapTravel());
	}
	TestEqual(TEXT("Still one start gate after every course"), CountTag(GFBeatHopMapGates::StartTag()), 1);
	TestEqual(TEXT("Still one finish gate after every course"), CountTag(GFBeatHopMapGates::FinishTag()), 1);
	TestTrue(TEXT("Find still returns the canonical gate"), AGFReturnGate::Find(W.World, TEXT("BeatHop")) == Canonical);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopMapTwoPlayerTest, "GFGame.BeatHop.Map.TwoPlayer", GFBeatHopMapTests::Flags)
bool FGFBeatHopMapTwoPlayerTest::RunTest(const FString&)
{
	using namespace GFBeatHopMapTests;
	for (int32 Number = 1; Number <= 3; ++Number)
	{
		const FGFBeatHopMapDef* Map = GFBeatHopMaps::ByNumber(Number);
		FBeatWorld W(*FString::Printf(TEXT("GFBeatHopMapCoop%d"), Number));
		AGFBeatHopCourse* Course = (W.World && Map) ? Place(W.World) : nullptr;
		if (!TestNotNull(TEXT("Map"), Map) || !TestNotNull(TEXT("Course"), Course) || !TestTrue(TEXT("Select"), Course->SelectMap(Map->Id)))
		{
			return false;
		}
		Course->SetDebugServerTime(3000.0 + Number);
		FString Why;
		if (!TestTrue(TEXT("Two-player run"),
			Course->StartRunForPlayers(Map->DefaultSong, Map->DisplayDifficulty, EGFBeatHopMode::Songs, false, 2, Why)))
		{
			AddError(Why);
			return false;
		}
		const FString A(TEXT("RunnerA"));
		const FString B(TEXT("RunnerB"));
		if (!TestTrue(TEXT("First runner clears"), Course->ScriptClearCourse(A, false)))
		{
			return false;
		}
		if (!TestTrue(TEXT("Second runner clears"), Course->ScriptClearCourse(B, false)))
		{
			return false;
		}
		TestTrue(TEXT("Finish"), Course->ReachFinish(nullptr));
		if (!TestEqual(TEXT("Both results"), Course->GetResults().Num(), 2))
		{
			return false;
		}
		for (const FGFBeatHopResultRow& Row : Course->GetResults())
		{
			TestTrue(TEXT("Both passed"), Row.bPassed);
			TestEqual(TEXT("Both clean"), Row.Misses, 0);
			TestEqual(TEXT("Same course name"), Row.MapTitle, Map->Title);
		}
		for (int32 Index = 0; Index < Course->NumNinjaObstacles(); ++Index)
		{
			TestTrue(TEXT("First runner hit it"), ClearedObstacle(*Course, Index, A));
			TestTrue(TEXT("Second runner hit it"), ClearedObstacle(*Course, Index, B));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopMapPayTest, "GFGame.BeatHop.Map.Pay", GFBeatHopMapTests::Flags)
bool FGFBeatHopMapPayTest::RunTest(const FString&)
{
	using namespace GFBeatHopMapTests;
	const FGFBeatHopMapDef* Map = GFBeatHopMaps::ByNumber(1);
	if (!TestNotNull(TEXT("Course 1"), Map))
	{
		return false;
	}
	FGFTransferLedger Ledger;
	FGFMapLedgerAccounts Accounts;
	Accounts.Accounts.Add(TEXT("Solo"));
	FBeatWorld W(TEXT("GFBeatHopMapPay"));
	AGFBeatHopCourse* Course = W.World ? Place(W.World) : nullptr;
	if (!TestNotNull(TEXT("Course"), Course) || !TestTrue(TEXT("Select"), Course->SelectMap(Map->Id)))
	{
		return false;
	}
	Course->SetLedgerOverride(&Ledger, &Accounts);
	Course->SetDebugServerTime(4000.0);
	FString Why;
	if (!TestTrue(TEXT("Run"), Course->StartRun(Map->DefaultSong, Map->DisplayDifficulty, EGFBeatHopMode::Songs, false, Why)))
	{
		AddError(Why);
		return false;
	}
	if (!TestTrue(TEXT("Clear"), Course->ScriptClearCourse(TEXT("Solo"))))
	{
		return false;
	}
	const FGuid Tx = AGFBeatHopCourse::MakeGrantTxId(Course->GetRunId(), TEXT("Solo"), TEXT("SongEnd"));
	TestTrue(TEXT("Grade B or better pays once"), Ledger.WasApplied(Tx));
	TestFalse(TEXT("The finish does not pay again"), Course->ReachFinish(nullptr));
	TestTrue(TEXT("Still one song-end grant"), Ledger.WasApplied(Tx));

	FGFTransferLedger Short;
	FGFMapLedgerAccounts ShortAccounts;
	ShortAccounts.Accounts.Add(TEXT("Solo"));
	FBeatWorld Again(TEXT("GFBeatHopMapPayShort"));
	AGFBeatHopCourse* Other = Again.World ? Place(Again.World) : nullptr;
	if (!TestNotNull(TEXT("Second course"), Other) || !TestTrue(TEXT("Select again"), Other->SelectMap(Map->Id)))
	{
		return false;
	}
	Other->SetLedgerOverride(&Short, &ShortAccounts);
	Other->SetDebugServerTime(5000.0);
	if (!TestTrue(TEXT("Short run starts"), Other->StartRun(Map->DefaultSong, Map->DisplayDifficulty, EGFBeatHopMode::Songs, false, Why)))
	{
		return false;
	}
	Other->SetDebugRunTime(Other->GetBrick(0).Time);
	TestTrue(TEXT("One hop"), Other->HandleLanding(TEXT("Solo"), 0, Other->GetBrick(0).Time) == EGFJudgement::Perfect);
	TestTrue(TEXT("Finish still passes"), Other->ReachFinish(nullptr));
	if (TestTrue(TEXT("Row"), Other->GetResults().Num() == 1))
	{
		TestTrue(TEXT("A single hop is grade C"), Other->GetResults()[0].Grade == EGFBeatHopGrade::C);
		TestTrue(TEXT("The row passed"), Other->GetResults()[0].bPassed);
	}
	TestFalse(TEXT("Grade C does not pay"), Short.WasApplied(AGFBeatHopCourse::MakeGrantTxId(Other->GetRunId(), TEXT("Solo"), TEXT("SongEnd"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopMapSectionTest, "GFGame.BeatHop.Map.Section", GFBeatHopMapTests::Flags)
bool FGFBeatHopMapSectionTest::RunTest(const FString&)
{
	using namespace GFBeatHopMapTests;
	const FGFBeatHopMapDef* Map = GFBeatHopMaps::ByNumber(1);
	FBeatWorld W(TEXT("GFBeatHopMapSection"));
	AGFBeatHopCourse* Course = (W.World && Map) ? Place(W.World) : nullptr;
	if (!TestNotNull(TEXT("Map"), Map) || !TestNotNull(TEXT("Course"), Course) || !TestTrue(TEXT("Select"), Course->SelectMap(Map->Id)))
	{
		return false;
	}
	if (!TestTrue(TEXT("Two sections"), Map->Sections.Num() >= 2))
	{
		return false;
	}
	Course->SetDebugServerTime(6000.0);
	const FString Why = Course->DevJumpToSection(2);
	if (!TestTrue(TEXT("Section jump"), Why.IsEmpty()))
	{
		AddError(Why);
		return false;
	}
	TestTrue(TEXT("The run is going"), Course->IsRunning());
	const double Expect = Map->Sections[1].StartBeat * 60.0 / Map->ReferenceBpm;
	TestEqual(TEXT("Clock is at the section"), Course->GetRunTime(), Expect, 0.05);
	TestFalse(TEXT("Section 0 is not a section"), Course->DevJumpToSection(0).IsEmpty());
	return true;
}

#endif
