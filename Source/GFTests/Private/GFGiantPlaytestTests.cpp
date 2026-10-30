// Playable Big Grumble: the director is spawned with the game mode, gf.Giant.Start runs an event once a player is in,
// and PlaytestFirstEventSeconds is a pure clock (0 = off, then the 2 day / 3 h trigger is unchanged).
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/DefaultPawn.h"
#include "GFGameMode.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFGiantDirector.h"
#include "GFGiantEventComponent.h"
#include "GFGiantCheats.h"
#include "GFGiantTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFGiantPlaytestTest
{
	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		int32 NextId = 100;

		bool Open(const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World || !GEngine)
			{
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			return true;
		}

		AGFGameMode* StartMode()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFGameMode* Mode = World->SpawnActor<AGFGameMode>(AGFGameMode::StaticClass(), FTransform::Identity, Params);
			if (!Mode)
			{
				return nullptr;
			}
			FString Error;
			Mode->InitGame(TEXT("GFGiantStartup"), FString(), Error);
			return Mode;
		}

		void AddGameState()
		{
			GS = World->SpawnActor<AGFGameState>();
			if (GS)
			{
				World->SetGameState(GS);
			}
		}

		AGFPlayerState* AddPlayer(const FString& Key)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
			if (!PS || !GS)
			{
				return PS;
			}
			PS->PlayerKey = Key;
			PS->SetPlayerId(++NextId);
			PS->SetPlayerName(Key);
			if (GS->PlayerArray.Find(PS) == INDEX_NONE)
			{
				GS->AddPlayerState(PS);
			}
			if (APawn* Pawn = World->SpawnActor<ADefaultPawn>(ADefaultPawn::StaticClass(), FTransform(FVector(4000.f, 0.f, 100.f)), Params))
			{
				Pawn->SetPlayerState(PS);
			}
			return PS;
		}

		int32 Directors() const
		{
			int32 N = 0;
			for (TActorIterator<AGFGiantDirector> It(World); It; ++It)
			{
				++N;
			}
			return N;
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

	static bool HasClass(const TArray<FSoftClassPath>& Paths, const TCHAR* ClassPath)
	{
		return Paths.Contains(FSoftClassPath(ClassPath));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantDirectorStartupTest, "GFGame.Giant.Playtest.DirectorStartup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantDirectorStartupTest::RunTest(const FString&)
{
	using namespace GFGiantPlaytestTest;
	const TArray<FSoftClassPath>& Paths = GetDefault<AGFGameMode>()->StartupDirectorClasses;
	TestTrue(TEXT("Giant director is a startup class"), HasClass(Paths, TEXT("/Script/GFGiant.GFGiantDirector")));
	TestTrue(TEXT("Social director stays in the startup list"), HasClass(Paths, TEXT("/Script/GFSocial.GFSocialDirector")));
	TestTrue(TEXT("Paint volume stays in the startup list"), HasClass(Paths, TEXT("/Script/GFGame.GFPaintVolume")));
	TestEqual(TEXT("Playtest first event stays off"), GetDefault<UGFGiantEventComponent>()->PlaytestFirstEventSeconds, 0.f);

	{
		FWorld W;
		if (!TestTrue(TEXT("Startup world"), W.Open(TEXT("GFGiantStartup"))))
		{
			return false;
		}
		TestEqual(TEXT("No director before the game mode"), W.Directors(), 0);
		if (!TestNotNull(TEXT("Game mode started"), W.StartMode()))
		{
			return false;
		}
		TestEqual(TEXT("Game mode spawns one director"), W.Directors(), 1);
		AGFGiantDirector* Director = AGFGiantDirector::Get(W.World, true);
		TestEqual(TEXT("Get() does not spawn a second director"), W.Directors(), 1);
		if (TestNotNull(TEXT("Director"), Director) && TestNotNull(TEXT("Constructor created the event component"), Director->Event.Get()))
		{
			TestTrue(TEXT("GetEvent is that component"), AGFGiantDirector::GetEvent(W.World, true) == Director->Event.Get());
			TestEqual(TEXT("GetEvent does not spawn a second director"), W.Directors(), 1);
		}
	}

	{
		FWorld W;
		if (!TestTrue(TEXT("Existing-director world"), W.Open(TEXT("GFGiantStartupExisting"))))
		{
			return false;
		}
		AGFGiantDirector* Pre = AGFGiantDirector::Get(W.World, true);
		if (!TestNotNull(TEXT("Pre-spawned director"), Pre) || !TestNotNull(TEXT("Pre-spawned event component"), Pre->Event.Get()))
		{
			return false;
		}
		TestEqual(TEXT("One director before startup"), W.Directors(), 1);
		if (!TestNotNull(TEXT("Game mode with a director already in the world"), W.StartMode()))
		{
			return false;
		}
		TestEqual(TEXT("Startup does not double-spawn"), W.Directors(), 1);
		TestTrue(TEXT("Get() still finds the same director"), AGFGiantDirector::Get(W.World, true) == Pre);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantStartCommandTest, "GFGame.Giant.Playtest.Start", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantStartCommandTest::RunTest(const FString&)
{
	using namespace GFGiantPlaytestTest;
	FWorld W;
	if (!TestTrue(TEXT("Start world"), W.Open(TEXT("GFGiantStart"))))
	{
		return false;
	}
	W.AddGameState();
	if (!TestNotNull(TEXT("Game state"), W.GS))
	{
		return false;
	}
	UGFGiantEventComponent* Event = AGFGiantDirector::GetEvent(W.World, true);
	if (!TestNotNull(TEXT("Event"), Event))
	{
		return false;
	}
	TestEqual(TEXT("Nobody in: start refuses"), GFGiantCheats::Start(W.World), FString(TEXT("nobody in")));
	TestFalse(TEXT("No event yet"), Event->bEventActive);

	AGFPlayerState* PS = W.AddPlayer(TEXT("Pip"));
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Player pawn"), PS->GetPawn()))
	{
		return false;
	}
	TestEqual(TEXT("One player counted"), Event->GatherPlayers().Num(), 1);
	TestEqual(TEXT("gf.Giant.Start"), GFGiantCheats::Start(W.World), FString());
	TestTrue(TEXT("Event is running"), Event->bEventActive);
	TestEqual(TEXT("One event started"), Event->EventsStarted, 1);
	if (TestNotNull(TEXT("Giant body"), Event->Giant.Get()))
	{
		const FVector Expected = Event->GetSim().Location + FVector(0.f, 0.f, 600.f);
		TestTrue(TEXT("Body spawns 600 above the sim"), Event->Giant->GetActorLocation().Equals(Expected, 1.f));
	}
	TestEqual(TEXT("Second start refuses"), GFGiantCheats::Start(W.World), FString(TEXT("already running")));

	const FString Status = GFGiantCheats::Status(W.World);
	TestTrue(TEXT("Status names the state"), Status.Contains(TEXT("active=1")));
	TestTrue(TEXT("Status counts the player"), Status.Contains(TEXT("players=1")));

	TestEqual(TEXT("gf.Giant.End"), GFGiantCheats::End(W.World), FString());
	TestFalse(TEXT("Event ended"), Event->bEventActive);
	TestNull(TEXT("Body destroyed"), Event->Giant.Get());
	TestEqual(TEXT("End with nothing running"), GFGiantCheats::End(W.World), FString(TEXT("no event")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantPlaytestMathsTest, "GFGame.Giant.Playtest.Maths", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantPlaytestMathsTest::RunTest(const FString&)
{
	auto Nearly = [this](const TCHAR* What, double Actual, double Expected)
	{
		TestTrue(What, FMath::IsNearlyEqual(Actual, Expected, 1.0e-6));
	};

	FGFGiantPlaytestClock Off;
	Off.PlaytestFirstEventSeconds = 0.f;
	Off.bPlayerIn = true;
	Off.SecondsSinceFirstPlayer = 100.0;
	const FGFGiantPlaytestClock OffStepped = FGFGiantPlaytest::Advance(Off, 5.0);
	Nearly(TEXT("Off: elapsed stays"), OffStepped.SecondsSinceFirstPlayer, 100.0);
	TestFalse(TEXT("Off: not due"), FGFGiantPlaytest::IsFirstEventDue(OffStepped));
	TestFalse(TEXT("Off: normal clock is not held"), FGFGiantPlaytest::HoldsOffNormalClock(OffStepped));

	FGFGiantPlaytestClock Negative = Off;
	Negative.PlaytestFirstEventSeconds = -3.f;
	Negative.SecondsSinceFirstPlayer = -1.0;
	Nearly(TEXT("Negative config stays off"), FGFGiantPlaytest::Advance(Negative, 5.0).SecondsSinceFirstPlayer, -1.0);
	TestFalse(TEXT("Negative config is not due"), FGFGiantPlaytest::IsFirstEventDue(Negative));

	FGFGiantPlaytestClock Clock;
	Clock.PlaytestFirstEventSeconds = 10.f;
	for (int32 i = 0; i < 5; ++i)
	{
		Clock = FGFGiantPlaytest::Advance(Clock, 1.0);
	}
	Nearly(TEXT("No player yet: clock has not started"), Clock.SecondsSinceFirstPlayer, -1.0);
	TestFalse(TEXT("No player: not due"), FGFGiantPlaytest::IsFirstEventDue(Clock));

	Clock.bPlayerIn = true;
	for (int32 i = 0; i < 9; ++i)
	{
		Clock = FGFGiantPlaytest::Advance(Clock, 1.0);
		TestFalse(TEXT("Before the delay"), FGFGiantPlaytest::IsFirstEventDue(Clock));
	}
	Nearly(TEXT("Nine seconds in"), Clock.SecondsSinceFirstPlayer, 9.0);
	Clock = FGFGiantPlaytest::Advance(Clock, 1.0);
	Nearly(TEXT("Due at the threshold"), Clock.SecondsSinceFirstPlayer, 10.0);
	TestTrue(TEXT("Due when the delay has elapsed and a player is in"), FGFGiantPlaytest::IsFirstEventDue(Clock));
	TestTrue(TEXT("Armed playtest holds the normal clock"), FGFGiantPlaytest::HoldsOffNormalClock(Clock));

	// The 2 day / 3 h clock is not an input. A fresh save would not auto-trigger, and this clock still says due.
	FGFGiantTuning Tuning;
	FGFGiantSave Save;
	FGFGiantRules::StartClockIfNeeded(Save, 1000);
	TestFalse(TEXT("Normal clock is not ready"), FGFGiantRules::ShouldTrigger(Tuning, Save, 1000, true));
	TestTrue(TEXT("Playtest clock does not consult it"), FGFGiantPlaytest::IsFirstEventDue(Clock));

	Clock.bPlayerIn = false;
	const double LeftBehind = Clock.SecondsSinceFirstPlayer;
	Clock = FGFGiantPlaytest::Advance(Clock, 4.0);
	Nearly(TEXT("Wall clock keeps going if they leave"), Clock.SecondsSinceFirstPlayer, LeftBehind + 4.0);
	TestFalse(TEXT("Not due with nobody in"), FGFGiantPlaytest::IsFirstEventDue(Clock));
	Clock.bPlayerIn = true;
	TestTrue(TEXT("Due again as soon as someone is back"), FGFGiantPlaytest::IsFirstEventDue(Clock));

	Clock.bSessionEventStarted = true;
	const double Frozen = Clock.SecondsSinceFirstPlayer;
	Clock = FGFGiantPlaytest::Advance(Clock, 50.0);
	Nearly(TEXT("After an event this session, the playtest clock stops"), Clock.SecondsSinceFirstPlayer, Frozen);
	TestFalse(TEXT("Later events are not the playtest override"), FGFGiantPlaytest::IsFirstEventDue(Clock));
	TestFalse(TEXT("After that event, the normal clock is in charge"), FGFGiantPlaytest::HoldsOffNormalClock(Clock));

	FGFGiantPlaytestClock Rewind;
	Rewind.PlaytestFirstEventSeconds = 10.f;
	Rewind.bPlayerIn = true;
	Rewind.SecondsSinceFirstPlayer = 3.0;
	Nearly(TEXT("Negative dt does not rewind"), FGFGiantPlaytest::Advance(Rewind, -2.0).SecondsSinceFirstPlayer, 3.0);

	FGFGiantPlaytestClock Exact;
	Exact.PlaytestFirstEventSeconds = 10.f;
	Exact.bPlayerIn = true;
	Exact.SecondsSinceFirstPlayer = 10.0;
	TestTrue(TEXT("Exact threshold is due"), FGFGiantPlaytest::IsFirstEventDue(Exact));
	Exact.SecondsSinceFirstPlayer = 9.999;
	TestFalse(TEXT("Just under is not due"), FGFGiantPlaytest::IsFirstEventDue(Exact));
	return true;
}

#endif
