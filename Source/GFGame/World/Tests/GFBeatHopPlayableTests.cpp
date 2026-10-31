// Beat Hop, playable end to end (#212): the course appears on L_Beat as a session, the start gate starts the picked
// song, a scripted run reaches the finish with results, and the results hand everyone back to the hub.

#include "Misc/AutomationTest.h"
#include "GFBeatHopCourse.h"
#include "GFBuiltInSongs.h"
#include "GFEconomy.h"
#include "GFHubGateDirector.h"
#include "GFMiniGameCatalog.h"
#include "GFRhythmJudge.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFBeatHopPlayableTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FBeatWorld
	{
		UWorld* World = nullptr;

		FBeatWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFBeatHopPlayableWorld"));
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

	/** What the game does when L_Beat loads: the gate director places the course and turns its session on. */
	static AGFBeatHopCourse* PlaceOnBeatMap(UWorld* World)
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

	static int32 CountCourses(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
		{
			++N;
		}
		return N;
	}

	static ACharacter* SpawnRunner(UWorld* World, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(At), Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopCourseOnMapTest, "GFGame.BeatHop.CourseOnMap", GFBeatHopPlayableTests::Flags)
bool FGFBeatHopCourseOnMapTest::RunTest(const FString&)
{
	using namespace GFBeatHopPlayableTests;
	FBeatWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = PlaceOnBeatMap(W.World);
	if (!TestNotNull(TEXT("L_Beat gets a course at runtime"), Course))
	{
		return false;
	}
	TestEqual(TEXT("Exactly one course"), CountCourses(W.World), 1);
	TestTrue(TEXT("Course runs as a session"), Course->IsSession());
	TestEqual(TEXT("Course is lifted clear of the terrain"), Course->GetActorLocation().Z, AGFBeatHopCourse::SessionHeightCm, 1.0);
	TestFalse(TEXT("No run yet"), Course->IsRunning());
	TestFalse(TEXT("No results yet"), Course->IsShowingResults());

	const TArray<FName> Ids = FGFBuiltInSongs::SongIds();
	TestTrue(TEXT("There are songs"), Ids.Num() > 0);
	TestEqual(TEXT("One lobby tile per song"), Course->NumSongTiles(), Ids.Num());
	TestEqual(TEXT("Three difficulty tiles"), Course->NumDifficultyTiles(), 3);
	TestTrue(TEXT("Preview course is built"), Course->NumBricks() > 0);

	UBoxComponent* Start = Course->GetStartTrigger();
	if (TestNotNull(TEXT("Start trigger"), Start) && TestNotNull(TEXT("Start gate"), Course->GetStartGate()))
	{
		TestTrue(TEXT("Start trigger overlaps"), Start->GetGenerateOverlapEvents());
		TestTrue(TEXT("Start trigger sits at the start gate"),
			FVector::Dist2D(Start->GetComponentLocation(), Course->GetStartGate()->GetComponentLocation()) < 120.0);
		TestTrue(TEXT("Start trigger is before the first brick"), Start->GetComponentLocation().X < Course->GetBrickTop(0).X);
	}
	UBoxComponent* Finish = Course->GetFinishTrigger();
	if (TestNotNull(TEXT("Finish trigger"), Finish))
	{
		TestTrue(TEXT("Finish trigger is past the last brick"), Finish->GetComponentLocation().X > Course->GetBrickTop(Course->NumBricks() - 1).X);
	}

	const FVector Lobby = Course->GetLobbySpawn();
	TestTrue(TEXT("Lobby is behind the start trigger"), Start && Lobby.X < Start->GetComponentLocation().X);
	ACharacter* Runner = SpawnRunner(W.World, FVector(0.0, 0.0, 200.0));
	if (TestNotNull(TEXT("Runner"), Runner))
	{
		TestTrue(TEXT("A pawn on the ground is lost from the sky course"), Course->IsLostFromCourse(Runner));
		Course->PlaceInLobby(Runner);
		TestFalse(TEXT("Placed in the lobby, it's on the course"), Course->IsLostFromCourse(Runner));
		TestTrue(TEXT("Lobby spawn"), FVector::Dist(Runner->GetActorLocation(), Lobby) < 150.0);
	}

	Course->BeginSession();
	AGFHubGateDirector* Again = W.World->SpawnActor<AGFHubGateDirector>();
	if (Again)
	{
		Again->Place(TEXT("/Game/Maps/L_Beat"), FVector::ZeroVector);
	}
	TestEqual(TEXT("Placing again keeps a single course"), CountCourses(W.World), 1);
	TestEqual(TEXT("Lobby tiles not doubled"), Course->NumSongTiles(), Ids.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopGateStartsRunTest, "GFGame.BeatHop.GateStartsRun", GFBeatHopPlayableTests::Flags)
bool FGFBeatHopGateStartsRunTest::RunTest(const FString&)
{
	using namespace GFBeatHopPlayableTests;
	FBeatWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = PlaceOnBeatMap(W.World);
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	const TArray<FName> Ids = FGFBuiltInSongs::SongIds();
	if (!TestTrue(TEXT("Songs"), Ids.Num() > 0))
	{
		return false;
	}
	TestEqual(TEXT("Default song is the first one"), Course->GetSelectedSong(), Ids[0]);
	TestTrue(TEXT("Default difficulty is Easy"), Course->GetSelectedDifficulty() == EGFBeatDifficulty::Easy);
	TestFalse(TEXT("Unknown song refused"), Course->SelectSong(TEXT("NoSuchSong")));

	const FName Pick = Ids.Last();
	TestTrue(TEXT("Pick a song"), Course->SelectSong(Pick));
	TestTrue(TEXT("Pick Normal"), Course->SelectDifficulty(EGFBeatDifficulty::Normal));
	TestEqual(TEXT("Selected song"), Course->GetSelectedSong(), Pick);
	TestTrue(TEXT("Selected difficulty"), Course->GetSelectedDifficulty() == EGFBeatDifficulty::Normal);
	TestTrue(TEXT("Preview follows the pick"), Course->GetBeatmap().SongId == Pick);
	TestTrue(TEXT("Back to Easy"), Course->SelectDifficulty(EGFBeatDifficulty::Easy));

	UBoxComponent* Start = Course->GetStartTrigger();
	if (!TestNotNull(TEXT("Start trigger"), Start))
	{
		return false;
	}
	ACharacter* Runner = SpawnRunner(W.World, Start->GetComponentLocation() + FVector(0.0, 0.0, 0.0));
	if (!TestNotNull(TEXT("Runner"), Runner))
	{
		return false;
	}
	UCharacterMovementComponent* Move = Runner->GetCharacterMovement();
	const float WalkBefore = Move ? Move->MaxWalkSpeed : 0.f;
	const float GravityBefore = Move ? Move->GravityScale : 0.f;

	Course->SetDebugServerTime(500.0);
	FString Why;
	TestTrue(TEXT("Walking through the gate starts the run"), Course->TryStartFromGate(Runner, Why));
	TestTrue(TEXT("Running"), Course->IsRunning());
	TestTrue(TEXT("Picked song is the one running"), Course->GetRunSongId() == Pick && Course->GetRunDifficulty() == EGFBeatDifficulty::Easy);
	TestTrue(TEXT("Run has bricks"), Course->NumBricks() > 0);
	// The clock lines up with where the runner crossed: the first brick is still ahead, about distance / speed away.
	const double LocalX = Course->GetActorTransform().InverseTransformPosition(Runner->GetActorLocation()).X;
	TestEqual(TEXT("Run time matches the crossing point"), Course->GetRunTime(), Course->RunTimeAtLocalX(LocalX), 0.5);
	TestTrue(TEXT("First brick is still ahead"), Course->GetRunTime() < Course->GetBrick(0).Time);
	TestTrue(TEXT("Runner gets run movement"), Course->HasRunMovement(Runner));
	if (Move)
	{
		TestEqual(TEXT("Runs at the chart speed"), Move->MaxWalkSpeed, Course->GetBeatmap().RefSpeedCms, 1.f);
		TestTrue(TEXT("Gravity tuned for beat-length hops"), Move->GravityScale >= 0.5f && Move->GravityScale <= 4.f);
	}
	TestFalse(TEXT("No second start mid-run"), Course->TryStartFromGate(Runner, Why));
	TestFalse(TEXT("No song change mid-run"), Course->SelectSong(Ids[0]));

	TestTrue(TEXT("Finish ends the run"), Course->ReachFinish(Runner));
	TestFalse(TEXT("Stopped"), Course->IsRunning());
	TestTrue(TEXT("Results up"), Course->IsShowingResults());
	TestFalse(TEXT("Run movement handed back"), Course->HasRunMovement(Runner));
	if (Move)
	{
		TestEqual(TEXT("Walk speed restored"), Move->MaxWalkSpeed, WalkBefore, 0.01f);
		TestEqual(TEXT("Gravity restored"), Move->GravityScale, GravityBefore, 0.001f);
	}
	TestFalse(TEXT("Gate shut while the results are up"), Course->TryStartFromGate(Runner, Why));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopFullRunTest, "GFGame.BeatHop.FullRun", GFBeatHopPlayableTests::Flags)
bool FGFBeatHopFullRunTest::RunTest(const FString&)
{
	using namespace GFBeatHopPlayableTests;
	FBeatWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = PlaceOnBeatMap(W.World);
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	FGFTransferLedger Ledger;
	FGFMapLedgerAccounts Accounts;
	Accounts.Accounts.Add(TEXT("Solo"));
	Course->SetLedgerOverride(&Ledger, &Accounts);
	Course->SetDebugServerTime(1000.0);

	// The default song on Easy, scripted: hop dead on every brick, then cross the finish gate.
	FString Why;
	ACharacter* Runner = SpawnRunner(W.World, Course->GetStartTrigger() ? Course->GetStartTrigger()->GetComponentLocation() : Course->GetLobbySpawn());
	if (!TestTrue(TEXT("Run starts from the gate"), Course->TryStartFromGate(Runner, Why)))
	{
		AddError(Why);
		return false;
	}
	const int32 N = Course->NumBricks();
	if (!TestTrue(TEXT("Bricks"), N > 3))
	{
		return false;
	}
	for (int32 i = 0; i < N; ++i)
	{
		const double T = Course->GetBrick(i).Time;
		Course->SetDebugRunTime(T);
		Course->UpdateRun();
		TestTrue(FString::Printf(TEXT("brick %d perfect"), i), Course->HandleLanding(TEXT("Solo"), i, T) == EGFJudgement::Perfect);
	}
	TestTrue(TEXT("Still running before the finish"), Course->IsRunning());
	TestTrue(TEXT("Crossing the finish ends the run"), Course->ReachFinish(Runner));
	TestFalse(TEXT("Run over"), Course->IsRunning());
	TestTrue(TEXT("Results showing"), Course->IsShowingResults());
	const TArray<FGFBeatHopResultRow>& Results = Course->GetResults();
	if (TestEqual(TEXT("One result row"), Results.Num(), 1))
	{
		const FGFBeatHopResultRow& R = Results[0];
		TestEqual(TEXT("Row is the runner"), R.PlayerKey, FString(TEXT("Solo")));
		TestEqual(TEXT("Best combo is every brick"), R.BestCombo, N);
		TestEqual(TEXT("Perfect on every brick"), R.Perfects, N);
		TestEqual(TEXT("No misses"), R.Misses, 0);
		TestTrue(TEXT("Score counted"), R.Score > 0);
		TestTrue(TEXT("Grade S"), R.Grade == EGFBeatHopGrade::S);
	}
	TestTrue(TEXT("Song end reward paid"), Ledger.WasApplied(AGFBeatHopCourse::MakeGrantTxId(Course->GetRunId(), TEXT("Solo"), TEXT("SongEnd"))));

	// A sloppy second run (new session world state): hop on the first brick, then let the song run out.
	FBeatWorld W2;
	AGFBeatHopCourse* Course2 = W2.World ? PlaceOnBeatMap(W2.World) : nullptr;
	if (!TestNotNull(TEXT("Second course"), Course2))
	{
		return false;
	}
	FGFTransferLedger Ledger2;
	FGFMapLedgerAccounts Accounts2;
	Accounts2.Accounts.Add(TEXT("Solo"));
	Course2->SetLedgerOverride(&Ledger2, &Accounts2);
	Course2->SetDebugServerTime(1000.0);
	TestTrue(TEXT("Second run starts"), Course2->TryStartFromGate(nullptr, Why));
	const double T0 = Course2->GetBrick(0).Time;
	Course2->SetDebugRunTime(T0);
	Course2->UpdateRun();
	TestTrue(TEXT("First hop perfect"), Course2->HandleLanding(TEXT("Solo"), 0, T0) == EGFJudgement::Perfect);
	Course2->SetDebugRunTime(Course2->GetRunEndTime() + 1.0);
	Course2->UpdateRun();
	TestFalse(TEXT("Song running out ends the run"), Course2->IsRunning());
	TestTrue(TEXT("Results after the song runs out"), Course2->IsShowingResults());
	if (TestEqual(TEXT("One row"), Course2->GetResults().Num(), 1))
	{
		TestEqual(TEXT("Every skipped brick is a miss"), Course2->GetResults()[0].Misses, Course2->NumBricks() - 1);
		TestTrue(TEXT("Low grade"), Course2->GetResults()[0].Grade == EGFBeatHopGrade::C);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopReturnToHubTest, "GFGame.BeatHop.ReturnToHub", GFBeatHopPlayableTests::Flags)
bool FGFBeatHopReturnToHubTest::RunTest(const FString&)
{
	using namespace GFBeatHopPlayableTests;
	FBeatWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = PlaceOnBeatMap(W.World);
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	FGFTransferLedger Ledger;
	FGFMapLedgerAccounts Accounts;
	Accounts.Accounts.Add(TEXT("Solo"));
	Course->SetLedgerOverride(&Ledger, &Accounts);
	TArray<FString> Travels;
	Course->SetTravelOverride([&Travels](const FString& Map) { Travels.Add(Map); });

	const double T0 = 2000.0;
	Course->SetDebugServerTime(T0);
	ACharacter* Runner = SpawnRunner(W.World, Course->GetStartTrigger() ? Course->GetStartTrigger()->GetComponentLocation() : Course->GetLobbySpawn());
	FString Why;
	if (!TestTrue(TEXT("Run starts"), Course->TryStartFromGate(Runner, Why)))
	{
		return false;
	}
	Course->UpdateSession();
	TestEqual(TEXT("No travel mid-run"), Travels.Num(), 0);
	TestTrue(TEXT("Finish"), Course->ReachFinish(Runner));
	TestEqual(TEXT("Results countdown"), Course->GetResultsSecondsLeft(), static_cast<double>(Course->ResultsSeconds), 0.01);

	Course->SetDebugServerTime(T0 + Course->ResultsSeconds * 0.5);
	Course->UpdateSession();
	TestEqual(TEXT("Still on the results halfway"), Travels.Num(), 0);
	TestTrue(TEXT("Results still up"), Course->IsShowingResults());

	Course->SetDebugServerTime(T0 + Course->ResultsSeconds + 0.1);
	Course->UpdateSession();
	if (TestEqual(TEXT("Back to the hub once"), Travels.Num(), 1))
	{
		TestEqual(TEXT("Travels to the hub map"), Travels[0], FGFMiniGameCatalog::HubMap().ToString());
		TestEqual(TEXT("Hub is L_Hub"), Travels[0], FString(TEXT("/Game/Maps/L_Hub")));
	}
	TestEqual(TEXT("Last travel recorded"), Course->GetLastTravelMap(), FGFMiniGameCatalog::HubMap().ToString());
	Course->UpdateSession();
	TestEqual(TEXT("Only travels once"), Travels.Num(), 1);
	TestFalse(TEXT("Runner has normal movement back"), Runner && Course->HasRunMovement(Runner));
	TestFalse(TEXT("Gate stays shut after leaving"), Course->TryStartFromGate(Runner, Why));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
