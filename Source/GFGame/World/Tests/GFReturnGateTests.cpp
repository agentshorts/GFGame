// One return home per mode. The gate, the fallback key and the pause menu share one path.
// A remote client is moved alone. The host pawn stays where it was.

#include "Misc/AutomationTest.h"
#include "GFArenaDirector.h"
#include "GFArenaSite.h"
#include "GFBeatHopCourse.h"
#include "GFDepthsDirector.h"
#include "GFFerryDirector.h"
#include "GFFerrySpawn.h"
#include "GFFerryStand.h"
#include "GFHUD.h"
#include "GFHubGateDirector.h"
#include "GFLabDirector.h"
#include "GFLabTypes.h"
#include "GFMiniGameCatalog.h"
#include "GFModeTypes.h"
#include "GFPlayerController.h"
#include "GFPlayerState.h"
#include "GFReturnToHub.h"
#include "GFRunwayShow.h"
#include "GFRunwayStage.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "InputCoreTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFReturnGateTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FReturnWorld
	{
		UWorld* World = nullptr;

		explicit FReturnWorld(const TCHAR* Name)
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

		~FReturnWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static ADefaultPawn* SpawnPawn(UWorld* World, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ADefaultPawn>(At, FRotator::ZeroRotator, Params);
	}

	static bool PlaceMode(UWorld* World, const TCHAR* Map)
	{
		AGFHubGateDirector* Director = World->SpawnActor<AGFHubGateDirector>();
		if (!Director)
		{
			return false;
		}
		Director->Place(Map, FVector::ZeroVector);
		return true;
	}

	static bool SameSpot(const FVector& A, const FVector& B)
	{
		return A.Equals(B, 5.f);
	}

	/** Gate, key and menu. Client travel does not move the host pawn. */
	static bool CheckHome(FAutomationTestBase& Test, UWorld* World, FName ModeId, bool bMap, const TCHAR* Tag)
	{
		AGFReturnGate* Gate = AGFReturnGate::Find(World, ModeId);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s return gate"), Tag), Gate))
		{
			return false;
		}
		Test.TestEqual(*FString::Printf(TEXT("%s sign"), Tag), Gate->GetSignText(), FString(TEXT("HUB")));
		Test.TestEqual(*FString::Printf(TEXT("%s map travel"), Tag), Gate->IsMapTravel(), bMap);
		const FGFMiniGameEntry* Entry = FGFMiniGameCatalog::Find(ModeId);
		const FVector WantAt = bMap ? AGFReturnGate::MapLocation() : (Entry ? AGFReturnGate::PadLocation(*Entry, FVector::ZeroVector) : FVector::ZeroVector);
		Test.TestTrue(*FString::Printf(TEXT("%s gate is where you arrive"), Tag), Gate->GetActorLocation().Equals(WantAt, 1.f));
		Test.TestEqual(*FString::Printf(TEXT("%s menu"), Tag), FString(AGFHUD::PauseEntryLabel(0)), FString(TEXT("Return to hub")));
		Test.TestTrue(*FString::Printf(TEXT("%s key"), Tag), AGFPlayerController::DefaultReturnKey() == EKeys::Home);

		const EGFReturnSeat Client = EGFReturnSeat::RemoteClient;
		const EGFReturnSeat HostSeat = EGFReturnSeat::ListenHost;
		if (bMap)
		{
			Test.TestTrue(*FString::Printf(TEXT("%s client route"), Tag), FGFReturnToHub::RouteFor(true, Client) == EGFReturnRoute::ClientTravel);
			Test.TestTrue(*FString::Printf(TEXT("%s host route"), Tag), FGFReturnToHub::RouteFor(true, HostSeat) == EGFReturnRoute::ServerTravel);
		}
		else
		{
			Test.TestTrue(*FString::Printf(TEXT("%s client teleports"), Tag), FGFReturnToHub::RouteFor(false, Client) == EGFReturnRoute::Teleport);
			Test.TestTrue(*FString::Printf(TEXT("%s host teleports"), Tag), FGFReturnToHub::RouteFor(false, HostSeat) == EGFReturnRoute::Teleport);
		}

		const FVector GateAt = Gate->GetActorLocation() + FVector(0.f, 0.f, 50.f);
		const FVector Away(80.f, -80.f, 5000.f);
		const FVector Stand = FGFReturnToHub::HubStand(Gate->GetRimeCenter());
		Test.TestTrue(*FString::Printf(TEXT("%s stand height"), Tag), FMath::IsNearlyEqual(Stand.Z - Gate->GetRimeCenter().Z, FGFPlayerStartRules::FallbackZ, 0.1f));

		ADefaultPawn* Host = SpawnPawn(World, Away);
		ADefaultPawn* KeyPawn = SpawnPawn(World, GateAt);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s host"), Tag), Host) || !Test.TestNotNull(*FString::Printf(TEXT("%s key pawn"), Tag), KeyPawn))
		{
			return false;
		}

		const FGFReturnResult Key = FGFReturnToHub::FromKey(World, KeyPawn, true, Gate, bMap ? &Client : nullptr);
		Test.TestTrue(*FString::Printf(TEXT("%s key moved"), Tag), Key.bMoved);
		Test.TestFalse(*FString::Printf(TEXT("%s key needed no confirm"), Tag), Key.bNeedsConfirm);
		Test.TestTrue(*FString::Printf(TEXT("%s host stays"), Tag), SameSpot(Host->GetActorLocation(), Away));
		if (bMap)
		{
			Test.TestTrue(*FString::Printf(TEXT("%s key is a client travel"), Tag), Key.bClientTravel && !Key.bServerTravel);
			Test.TestEqual(*FString::Printf(TEXT("%s key map"), Tag), Key.TravelMap, FGFMiniGameCatalog::HubMap().ToString());
			Test.TestTrue(*FString::Printf(TEXT("%s client was not swept to the host"), Tag), SameSpot(KeyPawn->GetActorLocation(), GateAt));
		}
		else
		{
			Test.TestTrue(*FString::Printf(TEXT("%s key lands on the stand"), Tag), SameSpot(KeyPawn->GetActorLocation(), Stand));
			Test.TestFalse(*FString::Printf(TEXT("%s key did not change maps"), Tag), Key.bClientTravel || Key.bServerTravel);
		}

		ADefaultPawn* MenuPawn = SpawnPawn(World, GateAt + FVector(20.f, 0.f, 0.f));
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s menu pawn"), Tag), MenuPawn))
		{
			return false;
		}
		const FGFReturnResult Menu = FGFReturnToHub::FromMenu(World, MenuPawn, true, Gate, bMap ? &HostSeat : nullptr);
		Test.TestTrue(*FString::Printf(TEXT("%s menu moved"), Tag), Menu.bMoved);
		Test.TestTrue(*FString::Printf(TEXT("%s host still stays"), Tag), SameSpot(Host->GetActorLocation(), Away));
		if (bMap)
		{
			Test.TestTrue(*FString::Printf(TEXT("%s menu is the host route"), Tag), Menu.bServerTravel && !Menu.bClientTravel);
			Test.TestEqual(*FString::Printf(TEXT("%s menu map"), Tag), Menu.TravelMap, FGFMiniGameCatalog::HubMap().ToString());
		}
		else
		{
			Test.TestTrue(*FString::Printf(TEXT("%s menu lands on the stand"), Tag), SameSpot(MenuPawn->GetActorLocation(), Stand));
		}

		ADefaultPawn* Walker = SpawnPawn(World, GateAt + FVector(0.f, 20.f, 0.f));
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s walker"), Tag), Walker))
		{
			return false;
		}
		const FGFReturnResult Walk = Gate->TryReturn(Walker, true);
		Test.TestTrue(*FString::Printf(TEXT("%s gate moved"), Tag), Walk.bMoved);
		Test.TestTrue(*FString::Printf(TEXT("%s host after the gate"), Tag), SameSpot(Host->GetActorLocation(), Away));
		if (bMap)
		{
			Test.TestEqual(*FString::Printf(TEXT("%s gate map"), Tag), Walk.TravelMap, FGFMiniGameCatalog::HubMap().ToString());
			Test.TestTrue(*FString::Printf(TEXT("%s gate takes the server route here"), Tag), Walk.bServerTravel && !Walk.bClientTravel);
		}
		else
		{
			Test.TestTrue(*FString::Printf(TEXT("%s gate lands on the stand"), Tag), SameSpot(Walker->GetActorLocation(), Stand));
		}
		return true;
	}

	static AGFPlayerState* SpawnPlayer(UWorld* World, const TCHAR* Key)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
		if (PS)
		{
			PS->PlayerKey = Key;
			PS->SetPlayerId(11);
		}
		return PS;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnValleyTest, "GFGame.Return.Valley", GFReturnGateTests::Flags)
bool FGFReturnValleyTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld W(TEXT("GFReturnValley"));
	if (!TestNotNull(TEXT("World"), W.World) || !TestTrue(TEXT("Place"), PlaceMode(W.World, TEXT("/Game/Maps/L_Valley"))))
	{
		return false;
	}
	if (!CheckHome(*this, W.World, TEXT("Taming"), true, TEXT("Valley")))
	{
		return false;
	}
	AGFPlayerController* PC = W.World->SpawnActor<AGFPlayerController>();
	if (!TestNotNull(TEXT("Controller"), PC))
	{
		return false;
	}
	TestTrue(TEXT("Fallback key is Home"), PC->GetReturnKey() == EKeys::Home);
	PC->SetReturnKey(EKeys::J);
	TestTrue(TEXT("Fallback key rebinds"), PC->GetReturnKey() == EKeys::J);
	PC->SetReturnKey(EKeys::Invalid);
	TestTrue(TEXT("An empty bind falls back to Home"), PC->GetReturnKey() == EKeys::Home);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnRunwayTest, "GFGame.Return.Runway", GFReturnGateTests::Flags)
bool FGFReturnRunwayTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld W(TEXT("GFReturnRunway"));
	if (!TestNotNull(TEXT("World"), W.World) || !TestTrue(TEXT("Place"), PlaceMode(W.World, TEXT("/Game/Maps/L_Runway"))))
	{
		return false;
	}
	if (!CheckHome(*this, W.World, TEXT("Fashion"), true, TEXT("Runway")))
	{
		return false;
	}
	AGFRunwayStage* Stage = AGFRunwayStage::Find(W.World);
	AGFReturnGate* Gate = AGFReturnGate::Find(W.World, TEXT("Fashion"));
	if (!TestNotNull(TEXT("Stage"), Stage) || !TestNotNull(TEXT("Gate"), Gate))
	{
		return false;
	}
	TestTrue(TEXT("Show starts"), Stage->StartShowFor(TEXT("GardenParty"), { TEXT("Walker") }, 3) == EGFRunwayRequestResult::Ok);
	TestEqual(TEXT("Dressing"), Stage->GetPhase(), EGFRunwayPhase::Dressing);
	ADefaultPawn* Pawn = SpawnPawn(W.World, Gate->GetActorLocation());
	const FGFReturnResult Asked = FGFReturnToHub::FromKey(W.World, Pawn, false, Gate, nullptr);
	TestTrue(TEXT("Mid-show asks"), Asked.bNeedsConfirm);
	TestFalse(TEXT("Mid-show does not leave yet"), Asked.bMoved);
	TestEqual(TEXT("Still dressing"), Stage->GetPhase(), EGFRunwayPhase::Dressing);
	const FGFReturnResult Left = FGFReturnToHub::FromMenu(W.World, Pawn, true, Gate, nullptr);
	TestTrue(TEXT("Confirmed menu leaves"), Left.bMoved);
	TestEqual(TEXT("Show is dropped"), Stage->GetPhase(), EGFRunwayPhase::Idle);
	TestEqual(TEXT("Forfeit"), Left.Forfeit, FName(TEXT("Runway")));
	TestEqual(TEXT("Hub map"), Left.TravelMap, FGFMiniGameCatalog::HubMap().ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnCrystalClashTest, "GFGame.Return.CrystalClash", GFReturnGateTests::Flags)
bool FGFReturnCrystalClashTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld W(TEXT("GFReturnMoba"));
	if (!TestNotNull(TEXT("World"), W.World) || !TestTrue(TEXT("Place"), PlaceMode(W.World, TEXT("/Game/Maps/L_Moba"))))
	{
		return false;
	}
	return CheckHome(*this, W.World, TEXT("Moba"), true, TEXT("Crystal Clash"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnBeatHopTest, "GFGame.Return.BeatHop", GFReturnGateTests::Flags)
bool FGFReturnBeatHopTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld Lobby(TEXT("GFReturnBeatLobby"));
	if (!TestNotNull(TEXT("Lobby world"), Lobby.World) || !TestTrue(TEXT("Place lobby"), PlaceMode(Lobby.World, TEXT("/Game/Maps/L_Beat"))))
	{
		return false;
	}
	if (!CheckHome(*this, Lobby.World, TEXT("BeatHop"), true, TEXT("Beat Hop lobby")))
	{
		return false;
	}
	AGFBeatHopCourse* Course = nullptr;
	for (TActorIterator<AGFBeatHopCourse> It(Lobby.World); It; ++It)
	{
		Course = *It;
		break;
	}
	AGFReturnGate* Gate = AGFReturnGate::Find(Lobby.World, TEXT("BeatHop"));
	if (!TestNotNull(TEXT("Course"), Course) || !TestNotNull(TEXT("Gate"), Gate))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Runner = Lobby.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Gate->GetActorLocation()), Params);
	FString Why;
	if (!TestTrue(TEXT("Run starts"), Runner && Course->TryStartFromGate(Runner, Why)))
	{
		AddInfo(Why);
		return false;
	}
	TestTrue(TEXT("Mid-round"), Course->IsRunning());
	const FVector Held(1200.f, 1200.f, 400.f);
	ADefaultPawn* Host = SpawnPawn(Lobby.World, Held);
	const EGFReturnSeat Client = EGFReturnSeat::RemoteClient;
	const FGFReturnResult Asked = FGFReturnToHub::FromKey(Lobby.World, Runner, false, Gate, &Client);
	TestTrue(TEXT("Mid-round asks"), Asked.bNeedsConfirm);
	TestTrue(TEXT("Still running"), Course->IsRunning());
	TestTrue(TEXT("Host has not moved"), Host && SameSpot(Host->GetActorLocation(), Held));
	const FGFReturnResult Left = FGFReturnToHub::FromMenu(Lobby.World, Runner, true, Gate, &Client);
	TestTrue(TEXT("Confirmed leave"), Left.bMoved && Left.bClientTravel && !Left.bServerTravel);
	TestEqual(TEXT("Hub map"), Left.TravelMap, FGFMiniGameCatalog::HubMap().ToString());
	TestFalse(TEXT("Run stopped"), Course->IsRunning());
	TestEqual(TEXT("Forfeit reason"), Course->GetEndReason(), FString(TEXT("ReturnForfeit")));
	TestTrue(TEXT("Host still there"), Host && SameSpot(Host->GetActorLocation(), Held));

	FReturnWorld Results(TEXT("GFReturnBeatResults"));
	if (!TestNotNull(TEXT("Results world"), Results.World) || !TestTrue(TEXT("Place results"), PlaceMode(Results.World, TEXT("/Game/Maps/L_Beat"))))
	{
		return false;
	}
	AGFBeatHopCourse* Again = nullptr;
	for (TActorIterator<AGFBeatHopCourse> It(Results.World); It; ++It)
	{
		Again = *It;
		break;
	}
	AGFReturnGate* ResultsGate = AGFReturnGate::Find(Results.World, TEXT("BeatHop"));
	ACharacter* Finisher = Results.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(ResultsGate ? ResultsGate->GetActorLocation() : FVector::ZeroVector), Params);
	FString Why2;
	if (!TestNotNull(TEXT("Results course"), Again) || !TestNotNull(TEXT("Results gate"), ResultsGate) || !TestTrue(TEXT("Results run"), Finisher && Again->TryStartFromGate(Finisher, Why2)))
	{
		AddInfo(Why2);
		return false;
	}
	TestTrue(TEXT("Finish"), Again->ReachFinish(Finisher));
	TestTrue(TEXT("Results are up"), Again->IsShowingResults());
	TestFalse(TEXT("Not running"), Again->IsRunning());
	const FGFReturnResult Home = FGFReturnToHub::FromKey(Results.World, Finisher, false, ResultsGate, &Client);
	TestFalse(TEXT("Results do not ask"), Home.bNeedsConfirm);
	TestTrue(TEXT("Results key goes home"), Home.bMoved && Home.bClientTravel);
	TestEqual(TEXT("Results map"), Home.TravelMap, FGFMiniGameCatalog::HubMap().ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnArenaTest, "GFGame.Return.Arena", GFReturnGateTests::Flags)
bool FGFReturnArenaTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld W(TEXT("GFReturnArena"));
	if (!TestNotNull(TEXT("World"), W.World) || !TestTrue(TEXT("Place"), PlaceMode(W.World, TEXT("/Game/Maps/L_Hub"))))
	{
		return false;
	}
	if (!CheckHome(*this, W.World, TEXT("Arena"), false, TEXT("Arena")))
	{
		return false;
	}
	AGFArenaDirector* Director = AGFArenaDirector::Get(W.World, false);
	AGFArenaSite* Site = AGFArenaSite::Get(W.World);
	AGFReturnGate* Gate = AGFReturnGate::Find(W.World, TEXT("Arena"));
	if (!TestNotNull(TEXT("Director"), Director) || !TestNotNull(TEXT("Site"), Site) || !TestNotNull(TEXT("Gate"), Gate))
	{
		return false;
	}
	Director->Phase = EGFArenaPhase::Fighting;
	ADefaultPawn* Pawn = SpawnPawn(W.World, Gate->GetActorLocation() + FVector(0.f, 0.f, 40.f));
	APlayerController* PC = W.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("View"), PC))
	{
		return false;
	}
	PC->Possess(Pawn);
	const bool bWatched = Site->ApplyBalconyView(Pawn);
	if (bWatched && Site->GetBalconyCamera())
	{
		TestEqual(TEXT("Balcony view"), PC->GetViewTarget(), static_cast<AActor*>(Site->GetBalconyCamera()));
	}
	const FVector Before = Pawn->GetActorLocation();
	const FGFReturnResult Asked = FGFReturnToHub::FromKey(W.World, Pawn, false, Gate, nullptr);
	TestTrue(TEXT("Bout asks"), Asked.bNeedsConfirm);
	TestEqual(TEXT("Arena forfeit"), Asked.Forfeit, FName(TEXT("Arena")));
	TestTrue(TEXT("Still in the bout"), SameSpot(Pawn->GetActorLocation(), Before));
	TestEqual(TEXT("Still fighting"), Director->Phase, EGFArenaPhase::Fighting);
	const FGFReturnResult Left = Gate->TryReturn(Pawn, true);
	TestTrue(TEXT("Gate leaves"), Left.bMoved);
	TestEqual(TEXT("Bout ended"), Director->Phase, EGFArenaPhase::Finished);
	TestTrue(TEXT("On the stand"), SameSpot(Pawn->GetActorLocation(), FGFReturnToHub::HubStand(Gate->GetRimeCenter())));
	if (bWatched)
	{
		TestEqual(TEXT("Balcony released"), PC->GetViewTarget(), static_cast<AActor*>(Pawn));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnDepthsTest, "GFGame.Return.Depths", GFReturnGateTests::Flags)
bool FGFReturnDepthsTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld W(TEXT("GFReturnDepths"));
	if (!TestNotNull(TEXT("World"), W.World) || !TestTrue(TEXT("Place"), PlaceMode(W.World, TEXT("/Game/Maps/L_Hub"))))
	{
		return false;
	}
	if (!CheckHome(*this, W.World, TEXT("Depths"), false, TEXT("Depths")))
	{
		return false;
	}
	AGFDepthsDirector* Director = AGFDepthsDirector::Get(W.World, false);
	AGFReturnGate* Gate = AGFReturnGate::Find(W.World, TEXT("Depths"));
	AGFPlayerState* PS = SpawnPlayer(W.World, TEXT("Diver"));
	ADefaultPawn* Pawn = SpawnPawn(W.World, Gate ? Gate->GetActorLocation() : FVector::ZeroVector);
	if (!TestNotNull(TEXT("Director"), Director) || !TestNotNull(TEXT("Gate"), Gate) || !TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn))
	{
		return false;
	}
	Pawn->SetPlayerState(PS);
	const FGFModeResult Entered = Director->HandleModeRequest(PS, FGFModeRequest::Make(AGFDepthsDirector::ModeName, TEXT("Enter")));
	if (!TestTrue(TEXT("Entered the night"), Entered.bOk))
	{
		AddInfo(Entered.Code.ToString());
		return false;
	}
	const FGFReturnResult Asked = FGFReturnToHub::FromKey(W.World, Pawn, false, Gate, nullptr);
	TestTrue(TEXT("Night asks"), Asked.bNeedsConfirm);
	TestNotNull(TEXT("Still in the run"), Director->FindPlayer(PS->PlayerKey));
	const FGFReturnResult Left = FGFReturnToHub::FromMenu(W.World, Pawn, true, Gate, nullptr);
	TestTrue(TEXT("Menu leaves"), Left.bMoved);
	TestEqual(TEXT("Forfeit"), Left.Forfeit, FName(TEXT("Depths")));
	TestNull(TEXT("Left the run"), Director->FindPlayer(PS->PlayerKey));
	TestTrue(TEXT("On the stand"), SameSpot(Pawn->GetActorLocation(), FGFReturnToHub::HubStand(Gate->GetRimeCenter())));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnMineTest, "GFGame.Return.PebbleMine", GFReturnGateTests::Flags)
bool FGFReturnMineTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld W(TEXT("GFReturnMine"));
	if (!TestNotNull(TEXT("World"), W.World) || !TestTrue(TEXT("Place"), PlaceMode(W.World, TEXT("/Game/Maps/L_Hub"))))
	{
		return false;
	}
	return CheckHome(*this, W.World, TEXT("Mine"), false, TEXT("Pebble Mine"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnOvertimeTest, "GFGame.Return.Overtime", GFReturnGateTests::Flags)
bool FGFReturnOvertimeTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld W(TEXT("GFReturnLab"));
	if (!TestNotNull(TEXT("World"), W.World) || !TestTrue(TEXT("Place"), PlaceMode(W.World, TEXT("/Game/Maps/L_Hub"))))
	{
		return false;
	}
	if (!CheckHome(*this, W.World, TEXT("Lab"), false, TEXT("Overtime")))
	{
		return false;
	}
	AGFLabDirector* Lab = AGFLabDirector::Get(W.World, false);
	AGFReturnGate* Gate = AGFReturnGate::Find(W.World, TEXT("Lab"));
	AGFPlayerState* PS = SpawnPlayer(W.World, TEXT("Tech"));
	ADefaultPawn* Pawn = SpawnPawn(W.World, Gate ? Gate->GetActorLocation() + FVector(0.f, 0.f, 30.f) : FVector::ZeroVector);
	if (!TestNotNull(TEXT("Lab"), Lab) || !TestNotNull(TEXT("Gate"), Gate) || !TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn))
	{
		return false;
	}
	Pawn->SetPlayerState(PS);
	const FGFModeResult Entered = Lab->HandleModeRequest(PS, FGFModeRequest::Make(AGFLabDirector::ModeName, TEXT("Enter")));
	if (!TestTrue(TEXT("Shift starts"), Entered.bOk))
	{
		AddInfo(Entered.Code.ToString());
		return false;
	}
	const FVector Before = Pawn->GetActorLocation();
	const FGFReturnResult Asked = FGFReturnToHub::FromKey(W.World, Pawn, false, Gate, nullptr);
	TestTrue(TEXT("Shift asks"), Asked.bNeedsConfirm);
	TestEqual(TEXT("Prompt"), Asked.Prompt, FString(FGFReturnToHub::LabPrompt()));
	TestTrue(TEXT("Prompt names 75"), Asked.Prompt.Contains(TEXT("75%")));
	TestTrue(TEXT("Still on the pad"), SameSpot(Pawn->GetActorLocation(), Before));
	TestNotNull(TEXT("Still in the shift"), Lab->FindRun(PS->PlayerKey));
	const FGFReturnResult Left = FGFReturnToHub::FromMenu(W.World, Pawn, true, Gate, nullptr);
	TestTrue(TEXT("Menu evacuates"), Left.bMoved);
	TestEqual(TEXT("Forfeit is the shift"), Left.Forfeit, FName(TEXT("Lab")));
	TestEqual(TEXT("Evacuate"), Lab->LastEnd.FindRef(PS->PlayerKey), EGFLabEnd::Evacuate);
	TestNull(TEXT("Shift closed"), Lab->FindRun(PS->PlayerKey));
	TestTrue(TEXT("On the stand"), SameSpot(Pawn->GetActorLocation(), FGFReturnToHub::HubStand(Gate->GetRimeCenter())));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFReturnFerryTest, "GFGame.Return.Ferry", GFReturnGateTests::Flags)
bool FGFReturnFerryTest::RunTest(const FString&)
{
	using namespace GFReturnGateTests;
	FReturnWorld W(TEXT("GFReturnFerry"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFFerryDirector* Director = W.World->SpawnActor<AGFFerryDirector>();
	if (!TestNotNull(TEXT("Ferry"), Director))
	{
		return false;
	}
	const TCHAR* Stops[] = { TEXT("RimeLanding"), TEXT("CinderLanding"), TEXT("VerdantLanding") };
	const TCHAR* Anchors[] = { TEXT("Rime"), TEXT("Cinderwrights"), TEXT("Verdant") };
	TestTrue(TEXT("Hub pad stays inside the board"), FGFFerryStandLayout::InsideBoard(FGFFerryStandLayout::HomeLocal(), Director->Tuning.BoardRadius));
	const FVector Away(0.f, 0.f, 8000.f);
	ADefaultPawn* Host = SpawnPawn(W.World, Away);
	if (!TestNotNull(TEXT("Host"), Host))
	{
		return false;
	}
	for (int32 i = 0; i < 3; ++i)
	{
		const FName StopId(Stops[i]);
		AGFFerryStand* Stand = AGFFerryStand::Find(W.World, StopId);
		AGFReturnGate* Gate = AGFReturnGate::Find(W.World, StopId);
		if (!TestNotNull(*FString::Printf(TEXT("%s stand"), Anchors[i]), Stand) || !TestNotNull(*FString::Printf(TEXT("%s gate"), Anchors[i]), Gate))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%s sign"), Anchors[i]), Gate->GetSignText(), FString(TEXT("HUB")));
		TestFalse(*FString::Printf(TEXT("%s stays on this map"), Anchors[i]), Gate->IsMapTravel());
		TestEqual(*FString::Printf(TEXT("%s menu"), Anchors[i]), FString(AGFHUD::PauseEntryLabel(0)), FString(TEXT("Return to hub")));
		const FVector Want = Stand->GetActorLocation() + FGFFerryStandLayout::HomeLocal();
		TestTrue(*FString::Printf(TEXT("%s pad"), Anchors[i]), Gate->GetActorLocation().Equals(Want, 1.f));
		const FVector StandPoint = FGFReturnToHub::HubStand(Gate->GetRimeCenter());
		ADefaultPawn* Walker = SpawnPawn(W.World, Gate->GetActorLocation() + FVector(0.f, 0.f, 40.f));
		const FGFReturnResult Walk = Gate->TryReturn(Walker, true);
		TestTrue(*FString::Printf(TEXT("%s gate"), Anchors[i]), Walk.bMoved && !Walk.bClientTravel && !Walk.bServerTravel);
		TestTrue(*FString::Printf(TEXT("%s on the spawn stand"), Anchors[i]), Walker && SameSpot(Walker->GetActorLocation(), StandPoint));
		ADefaultPawn* KeyPawn = SpawnPawn(W.World, Gate->GetActorLocation() + FVector(15.f, 0.f, 40.f));
		const FGFReturnResult Key = FGFReturnToHub::FromKey(W.World, KeyPawn, true, Gate, nullptr);
		TestTrue(*FString::Printf(TEXT("%s key"), Anchors[i]), Key.bMoved && KeyPawn && SameSpot(KeyPawn->GetActorLocation(), StandPoint));
		ADefaultPawn* MenuPawn = SpawnPawn(W.World, Gate->GetActorLocation() + FVector(-15.f, 0.f, 40.f));
		const FGFReturnResult Menu = FGFReturnToHub::FromMenu(W.World, MenuPawn, true, Gate, nullptr);
		TestTrue(*FString::Printf(TEXT("%s menu"), Anchors[i]), Menu.bMoved && MenuPawn && SameSpot(MenuPawn->GetActorLocation(), StandPoint));
		TestTrue(*FString::Printf(TEXT("%s host stays"), Anchors[i]), SameSpot(Host->GetActorLocation(), Away));
	}
	return true;
}

#endif
