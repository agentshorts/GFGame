// Hallow Night, playable from the hub pad: the gate drops you on the pad, Start opens a depth,
// Finish is the exit lift, Results lists what came home, and the arch puts you back on the hub stand.

#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "GFDepthsDirector.h"
#include "GFDepthsSite.h"
#include "GFDepthsTypes.h"
#include "GFGameState.h"
#include "GFHubGateDirector.h"
#include "GFInventoryComponent.h"
#include "GFItemTypes.h"
#include "GFMiniGameCatalog.h"
#include "GFMiniGameGate.h"
#include "GFPlayerState.h"
#include "GFRewards.h"
#include "Components/BoxComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFDepthsPlayableTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FDepthsWorld
	{
		UWorld* World = nullptr;

		explicit FDepthsWorld(const TCHAR* Name)
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

		~FDepthsWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static int32 CountSites(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AGFDepthsSite> It(World); It; ++It)
		{
			++N;
		}
		return N;
	}

	static AGFMiniGameGate* DepthsPortal(UWorld* World)
	{
		for (TActorIterator<AGFMiniGameGate> It(World); It; ++It)
		{
			if (It->EntryId == FName(TEXT("Depths")) && It->GateRole == EGFMiniGameGateRole::Portal)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static FString JoinPrompt(const AGFDepthsSite* Site, const AGFPlayerState* PS)
	{
		TArray<FString> Lines;
		Site->GetPromptLines(PS, Lines);
		FString Joined;
		for (const FString& Line : Lines)
		{
			Joined += Line;
			Joined += TEXT("\n");
		}
		return Joined;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDepthsSiteOnPadTest, "GFGame.Depths.Playable.SiteOnPad", GFDepthsPlayableTests::Flags)
bool FGFDepthsSiteOnPadTest::RunTest(const FString&)
{
	using namespace GFDepthsPlayableTests;
	FDepthsWorld W(TEXT("GFDepthsSiteWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFHubGateDirector* Director = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Hub director"), Director))
	{
		return false;
	}
	Director->Place(TEXT("/Game/Maps/L_Hub"), FVector::ZeroVector);

	AGFDepthsSite* Site = AGFDepthsSite::Get(W.World);
	AGFDepthsDirector* Depths = AGFDepthsDirector::Get(W.World, false);
	if (!TestNotNull(TEXT("Hub place builds a depths site"), Site) || !TestNotNull(TEXT("Depths director"), Depths))
	{
		return false;
	}
	TestEqual(TEXT("One site"), CountSites(W.World), 1);
	TestEqual(TEXT("Four spots"), Site->NumSpots(), 4);
	const FGFMiniGameEntry* Row = FGFMiniGameCatalog::Find(TEXT("Depths"));
	if (!TestNotNull(TEXT("Depths catalog row"), Row))
	{
		return false;
	}
	TestTrue(TEXT("Depths stays a sim"), Row->HasSim() && !Row->HasMap());
	TestTrue(TEXT("Site sits on the depths pad"), Site->GetActorLocation().Equals(FGFMiniGameCatalog::PadOrigin(*Row, FVector::ZeroVector), 5.f));
	TestTrue(TEXT("Director stays on that pad"), Depths->GetActorLocation().Equals(Site->GetActorLocation(), 5.f));

	const FName Verbs[] = { TEXT("Start"), TEXT("Finish"), TEXT("Results"), TEXT("Return") };
	for (const FName Verb : Verbs)
	{
		UBoxComponent* Box = Site->GetVerbTrigger(Verb);
		if (TestNotNull(*FString::Printf(TEXT("%s spot"), *Verb.ToString()), Box))
		{
			TestTrue(*FString::Printf(TEXT("%s overlaps"), *Verb.ToString()), Box->GetGenerateOverlapEvents());
			TestTrue(*FString::Printf(TEXT("%s hits pawns"), *Verb.ToString()), Box->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap);
		}
	}

	const FString Prompt = JoinPrompt(Site, nullptr);
	const TCHAR* Words[] = { TEXT("Start"), TEXT("Finish"), TEXT("Results"), TEXT("Hub") };
	for (const TCHAR* Word : Words)
	{
		TestTrue(*FString::Printf(TEXT("Prompt names %s"), Word), Prompt.Contains(Word));
	}

	const FGFDepthsRewardRow* Rewards = FGFDepthsRules::RewardRow(1);
	if (TestNotNull(TEXT("Depth 1 rewards"), Rewards))
	{
		const FGFRewardEntry* Moon = Rewards->Exit.FindByPredicate([](const FGFRewardEntry& E) { return E.ItemId == FName(TEXT("Moonstone")); });
		if (TestNotNull(TEXT("Exit still drops Moonstone"), Moon))
		{
			TestEqual(TEXT("Moonstone min stays 1"), Moon->Min, 1);
			TestEqual(TEXT("Moonstone max stays 2"), Moon->Max, 2);
			TestTrue(TEXT("Moonstone still always drops"), FMath::IsNearlyEqual(Moon->Chance, 1.f));
		}
	}

	AGFMiniGameGate* Portal = DepthsPortal(W.World);
	if (!TestNotNull(TEXT("Depths portal"), Portal))
	{
		return false;
	}
	TestEqual(TEXT("Portal says Depths"), Portal->GetLabel(), FString(TEXT("Depths")));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Walker = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(0.f, 0.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Walker"), Walker))
	{
		return false;
	}
	TestFalse(TEXT("Hub stand is not on the depths pad"), Site->IsPlayerNear(Walker));
	TestTrue(TEXT("Walking into the portal enters the depths"), Portal->EnterFrom(Walker));
	const FVector Stand = FGFMiniGameCatalog::SimStand(*Row, FVector::ZeroVector);
	TestTrue(TEXT("Stands on the depths pad"), Walker->GetActorLocation().Equals(Stand, 5.f));
	TestTrue(TEXT("The stand is inside the night"), Site->IsPlayerNear(Walker));

	Director->Place(TEXT("/Game/Maps/UEDPIE_0_L_Hub"), FVector::ZeroVector);
	TestEqual(TEXT("Placing again keeps a single site"), CountSites(W.World), 1);
	TestEqual(TEXT("Spots not doubled"), Site->NumSpots(), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDepthsPlayableLoopTest, "GFGame.Depths.Playable.Loop", GFDepthsPlayableTests::Flags)
bool FGFDepthsPlayableLoopTest::RunTest(const FString&)
{
	using namespace GFDepthsPlayableTests;
	FDepthsWorld W(TEXT("GFDepthsLoopWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFGameState* GS = W.World->SpawnActor<AGFGameState>();
	if (!TestNotNull(TEXT("Game state"), GS))
	{
		return false;
	}
	W.World->SetGameState(GS);

	AGFHubGateDirector* Hub = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Hub director"), Hub))
	{
		return false;
	}
	Hub->Place(TEXT("/Game/Maps/L_Hub"), FVector::ZeroVector);

	AGFDepthsDirector* Depths = AGFDepthsDirector::Get(W.World, false);
	AGFDepthsSite* Site = AGFDepthsSite::Get(W.World);
	if (!TestNotNull(TEXT("Depths director"), Depths) || !TestNotNull(TEXT("Depths site"), Site))
	{
		return false;
	}
	Depths->ForcedSeed = 7;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(400.f, 15000.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Inventory"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Runner");
	PS->SetPlayerId(9);
	PS->bCharacterReady = true;
	PS->Progress.DepthsReached = 0;
	GS->AddPlayerState(PS);
	Pawn->SetPlayerState(PS);

	const FGFModeResult Early = Site->TryFinish(Pawn);
	TestFalse(TEXT("Finish before a run does nothing"), Early.bOk);
	TestEqual(TEXT("Not in a run"), Early.Code.ToString(), FString(TEXT("NotInRun")));
	const FGFModeResult NoBoard = Site->TryResults(Pawn);
	TestFalse(TEXT("Results wait for a finish"), NoBoard.bOk);
	TestEqual(TEXT("No results yet"), NoBoard.Code.ToString(), FString(TEXT("NoResults")));
	TestTrue(TEXT("The board says so"), JoinPrompt(Site, PS).Contains(TEXT("finish the night first")));

	const FGFModeResult Started = Site->TryStart(Pawn);
	if (!TestTrue(TEXT("Start depth 1"), Started.bOk))
	{
		return false;
	}
	TestEqual(TEXT("The night is depth 1"), Depths->Depth, 1);
	const FGFDepthsPlayerView* InRun = Depths->FindPlayer(PS->PlayerKey);
	TestTrue(TEXT("Standing at the entrance"), InRun && FMath::Abs(InRun->Body.Pos.X) < 5.f);
	TestEqual(TEXT("Nothing banked yet"), PS->Progress.DepthsReached, 0);
	TestEqual(TEXT("Bag empty"), PS->Inventory->GetCount(TEXT("Moonstone")), 0);

	// Walking home mid-run must close the night, or the next Start stays AlreadyIn.
	TestTrue(TEXT("Hub arch"), Site->TryReturn(Pawn));
	TestTrue(TEXT("Back on the hub stand"), Pawn->GetActorLocation().Equals(Site->GetHubReturnLocation(), 5.f));
	TestFalse(TEXT("The hub stand is outside the night HUD"), Site->IsPlayerNear(Pawn));
	TestNull(TEXT("Leave closed the run"), Depths->FindPlayer(PS->PlayerKey));
	TestEqual(TEXT("Run over"), Depths->Depth, 0);
	TestEqual(TEXT("Leaving early does not clear the depth"), PS->Progress.DepthsReached, 0);
	TestEqual(TEXT("No exit drop from a leave"), PS->Inventory->GetCount(TEXT("Moonstone")), 0);

	const FGFModeResult Again = Site->TryStart(Pawn);
	if (!TestTrue(TEXT("Start again after leaving"), Again.bOk))
	{
		return false;
	}
	TestEqual(TEXT("Depth 1 once more"), Depths->Depth, 1);

	const FGFModeResult Finished = Site->TryFinish(Pawn);
	if (!TestTrue(TEXT("Finish is the exit lift"), Finished.bOk))
	{
		return false;
	}
	TestEqual(TEXT("Depth 1 cleared"), PS->Progress.DepthsReached, 1);
	TestEqual(TEXT("Run closed"), Depths->Depth, 0);
	TestNull(TEXT("Out of the run"), Depths->FindPlayer(PS->PlayerKey));
	TestTrue(TEXT("Results are up"), Site->bResultsReady);
	TestEqual(TEXT("Results name depth 1"), Site->LastDepthFinished, 1);
	TestEqual(TEXT("Deepest open is 1"), Site->LastDepthsReached, 1);
	TestTrue(TEXT("Exit Moonstone came home"), PS->Inventory->GetCount(TEXT("Moonstone")) >= 1);
	TestTrue(TEXT("Exit Crystal came home"), PS->Inventory->GetCount(TEXT("Crystal")) >= 1);
	TestEqual(TEXT("Bag matches the lift"), PS->Inventory->GetCount(TEXT("Moonstone")), FGFRewards::CountOf(Finished.Stacks, TEXT("Moonstone")));
	TestEqual(TEXT("Crystal matches the lift"), PS->Inventory->GetCount(TEXT("Crystal")), FGFRewards::CountOf(Finished.Stacks, TEXT("Crystal")));
	for (const FGFItemStack& Stack : PS->Inventory->GetInventory().Items)
	{
		const bool bExitDrop = Stack.ItemId == FName(TEXT("Moonstone")) || Stack.ItemId == FName(TEXT("Crystal"));
		TestTrue(*FString::Printf(TEXT("Only the depth 1 exit drop (%s)"), *Stack.ItemId.ToString()), bExitDrop);
	}
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (TestNotNull(TEXT("DT_Items"), Items))
	{
		TestTrue(TEXT("The lift is a Hallow Night drop"), FGFRewards::ContainsExclusive(Items, Finished.Stacks, EGFExclusive::HallowNight));
	}

	const FGFModeResult Board = Site->TryResults(Pawn);
	TestTrue(TEXT("Results spot"), Board.bOk);
	const FString After = JoinPrompt(Site, PS);
	TestTrue(TEXT("Board names Moonstone"), After.Contains(TEXT("Moonstone")));
	TestTrue(TEXT("Board names the finish"), After.Contains(TEXT("depth 1 finished")));
	TestTrue(TEXT("Next start is depth 2"), After.Contains(TEXT("Start opens depth 2")));
	const TCHAR* Words[] = { TEXT("Start"), TEXT("Finish"), TEXT("Results"), TEXT("Hub") };
	for (const TCHAR* Word : Words)
	{
		TestTrue(*FString::Printf(TEXT("Prompt still names %s"), Word), After.Contains(Word));
	}

	TestTrue(TEXT("Hub arch after the night"), Site->TryReturn(Pawn));
	TestTrue(TEXT("Still the Rime stand"), Pawn->GetActorLocation().Equals(FVector(0.f, 0.f, AGFDepthsSite::HubStandZCm), 1.f));
	TestEqual(TEXT("The clear sticks"), PS->Progress.DepthsReached, 1);
	TestTrue(TEXT("Results stay up on the way out"), Site->bResultsReady);
	return true;
}

#endif
