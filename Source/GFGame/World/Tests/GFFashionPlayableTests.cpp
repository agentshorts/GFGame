// Fashion show, playable from the hub gate: L_Runway already places the stage, the site adds the
// greybox loop (theme, look, start, finish, scores, hub). No new map.

#include "Misc/AutomationTest.h"
#include "GFFashionTypes.h"
#include "GFGameState.h"
#include "GFHubGateDirector.h"
#include "GFInventoryComponent.h"
#include "GFMiniGameCatalog.h"
#include "GFMiniGameGate.h"
#include "GFPlayerState.h"
#include "GFRunwaySite.h"
#include "GFRunwayStage.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFFashionPlayableTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FRunwayWorld
	{
		UWorld* World = nullptr;

		explicit FRunwayWorld(const TCHAR* Name)
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

		~FRunwayWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	template <typename T>
	static int32 Count(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<T> It(World); It; ++It)
		{
			++N;
		}
		return N;
	}

	static bool OverlapsPawns(const UBoxComponent* Box)
	{
		return Box && Box->GetGenerateOverlapEvents() && Box->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap;
	}

	static FString JoinLines(const TArray<FString>& Lines)
	{
		FString Joined;
		for (const FString& Line : Lines)
		{
			Joined += Line;
			Joined += TEXT("\n");
		}
		return Joined;
	}

	static int32 CountCurrentSteps(const TArray<FString>& Lines)
	{
		int32 N = 0;
		for (const FString& Line : Lines)
		{
			N += Line.StartsWith(TEXT("> ")) ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionSiteOnMapTest, "GFGame.Fashion.Playable.SiteOnMap", GFFashionPlayableTests::Flags)
bool FGFFashionSiteOnMapTest::RunTest(const FString&)
{
	using namespace GFFashionPlayableTests;
	const FGFMiniGameEntry* Fashion = FGFMiniGameCatalog::Find(TEXT("Fashion"));
	if (!TestNotNull(TEXT("Fashion catalog row"), Fashion))
	{
		return false;
	}
	TestEqual(TEXT("Runway map stays L_Runway"), Fashion->Map.ToString(), FString(TEXT("/Game/Maps/L_Runway")));
	TestFalse(TEXT("Fashion is not a hub sim pad"), Fashion->bHasPad);
	TestTrue(TEXT("Placed class is the runway stage"), Fashion->PlacedClass.Contains(TEXT("GFRunwayStage")));

	FRunwayWorld Hub(TEXT("GFFashionHubWorld"));
	if (!TestNotNull(TEXT("Hub world"), Hub.World))
	{
		return false;
	}
	AGFHubGateDirector* HubDirector = Hub.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Hub director"), HubDirector))
	{
		return false;
	}
	HubDirector->Place(TEXT("/Game/Maps/L_Hub"), FVector::ZeroVector);
	TestEqual(TEXT("Hub place does not spawn a runway site"), Count<AGFRunwaySite>(Hub.World), 0);
	TestEqual(TEXT("Hub place does not spawn a runway stage"), Count<AGFRunwayStage>(Hub.World), 0);

	FRunwayWorld W(TEXT("GFFashionSiteWorld"));
	if (!TestNotNull(TEXT("Runway world"), W.World))
	{
		return false;
	}
	AGFHubGateDirector* Director = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Runway director"), Director))
	{
		return false;
	}
	Director->Place(TEXT("/Game/Maps/L_Runway"), FVector::ZeroVector);

	AGFRunwayStage* Stage = AGFRunwayStage::Find(W.World);
	AGFRunwaySite* Site = AGFRunwaySite::Get(W.World);
	if (!TestNotNull(TEXT("L_Runway places the stage"), Stage) || !TestNotNull(TEXT("L_Runway builds a site"), Site))
	{
		return false;
	}
	TestEqual(TEXT("One stage"), Count<AGFRunwayStage>(W.World), 1);
	TestEqual(TEXT("One site"), Count<AGFRunwaySite>(W.World), 1);
	TestEqual(TEXT("A theme spot per catalog theme"), Site->NumThemeSpots(), FGFFashionCatalog::Themes().Num());
	TestEqual(TEXT("Two looks"), Site->NumLookSpots(), 2);
	TestEqual(TEXT("Stage timers stay at the show defaults until Start"), Stage->DressingSeconds, 75.f);
	TestEqual(TEXT("Default theme"), Site->GetSelectedTheme().ToString(), FString(TEXT("GardenParty")));
	TestEqual(TEXT("Default look"), Site->GetSelectedLook().ToString(), FString(TEXT("Garden")));
	TestFalse(TEXT("Suggested theme is not chosen yet"), Site->HasChosenTheme());
	TestFalse(TEXT("Suggested look is not worn yet"), Site->HasChosenLook());
	TestEqual(TEXT("Start arch asks for a theme"), Site->GetStartCue(), FString(TEXT("Pick a theme first")));
	TestTrue(TEXT("Wardrobe names both starter looks"), Site->GetWearingCue().Contains(TEXT("Garden")) && Site->GetWearingCue().Contains(TEXT("Explorer")));

	const FName Verbs[] = { TEXT("Start"), TEXT("Finish"), TEXT("Return") };
	for (const FName Verb : Verbs)
	{
		UBoxComponent* Box = Site->GetVerbTrigger(Verb);
		TestTrue(*FString::Printf(TEXT("%s spot overlaps pawns"), *Verb.ToString()), OverlapsPawns(Box));
	}
	TestTrue(TEXT("Garden Party spot"), OverlapsPawns(Site->GetThemeTrigger(TEXT("GardenParty"))));
	TestTrue(TEXT("Garden look spot"), OverlapsPawns(Site->GetLookTrigger(TEXT("Garden"))));
	TestTrue(TEXT("Explorer look spot"), OverlapsPawns(Site->GetLookTrigger(TEXT("Explorer"))));

	UBoxComponent* Start = Site->GetVerbTrigger(TEXT("Start"));
	UBoxComponent* Finish = Site->GetVerbTrigger(TEXT("Finish"));
	UBoxComponent* Return = Site->GetVerbTrigger(TEXT("Return"));
	if (TestNotNull(TEXT("Start"), Start) && TestNotNull(TEXT("Finish"), Finish) && TestNotNull(TEXT("Return"), Return))
	{
		TestTrue(TEXT("Start is down the catwalk from the map sign"), Start->GetComponentLocation().X > -500.f);
		TestTrue(TEXT("Finish is past Start"), Finish->GetComponentLocation().X > Start->GetComponentLocation().X);
		TestTrue(TEXT("Hub arch is past Finish"), Return->GetComponentLocation().X > Finish->GetComponentLocation().X);
		UBoxComponent* GardenSpot = Site->GetLookTrigger(TEXT("Garden"));
		if (TestNotNull(TEXT("Garden spot"), GardenSpot))
		{
			TestTrue(TEXT("Wardrobe is before the start arch"), GardenSpot->GetComponentLocation().X < Start->GetComponentLocation().X);
		}
	}

	TArray<FString> Lines;
	Site->GetPromptLines(nullptr, Lines);
	FString Joined;
	for (const FString& Line : Lines)
	{
		Joined += Line;
		Joined += TEXT("\n");
	}
	const TCHAR* Words[] = { TEXT("Start"), TEXT("Finish"), TEXT("Hub"), TEXT("Garden") };
	for (const TCHAR* Word : Words)
	{
		TestTrue(*FString::Printf(TEXT("Prompt names %s"), Word), Joined.Contains(Word));
	}
	TestTrue(TEXT("Prompt highlights the theme step"), Joined.Contains(TEXT("> Pick a theme.")));
	TestTrue(TEXT("Prompt says to pick a look"), Joined.Contains(TEXT("Pick a look first")));

	AGFMiniGameGate* Sign = nullptr;
	for (TActorIterator<AGFMiniGameGate> It(W.World); It; ++It)
	{
		if (It->EntryId == FName(TEXT("Fashion")) && It->GateRole == EGFMiniGameGateRole::Marker)
		{
			Sign = *It;
			break;
		}
	}
	if (TestNotNull(TEXT("Runway sign"), Sign))
	{
		TestEqual(TEXT("Sign still says Runway"), Sign->GetLabel(), FString(TEXT("Runway")));
	}

	Director->Place(TEXT("/Game/Maps/UEDPIE_0_L_Runway"), FVector::ZeroVector);
	TestEqual(TEXT("Placing again keeps a single site"), Count<AGFRunwaySite>(W.World), 1);
	TestEqual(TEXT("Placing again keeps a single stage"), Count<AGFRunwayStage>(W.World), 1);
	TestEqual(TEXT("Theme spots not doubled"), Site->NumThemeSpots(), FGFFashionCatalog::Themes().Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionPlayableLoopTest, "GFGame.Fashion.Playable.Loop", GFFashionPlayableTests::Flags)
bool FGFFashionPlayableLoopTest::RunTest(const FString&)
{
	using namespace GFFashionPlayableTests;
	FRunwayWorld W(TEXT("GFFashionLoopWorld"));
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

	AGFHubGateDirector* Director = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Director"), Director))
	{
		return false;
	}
	Director->Place(TEXT("/Game/Maps/L_Runway"), FVector::ZeroVector);

	AGFRunwayStage* Stage = AGFRunwayStage::Find(W.World);
	AGFRunwaySite* Site = AGFRunwaySite::Get(W.World);
	if (!TestNotNull(TEXT("Stage"), Stage) || !TestNotNull(TEXT("Site"), Site))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(80.f, 0.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Inventory"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Model");
	PS->SetPlayerId(9);
	PS->bCharacterReady = true;
	GS->AddPlayerState(PS);
	Pawn->SetPlayerState(PS);

	// The arch is the return. Hold the automatic results travel so a world tick cannot leave first.
	Site->ResultsSeconds = 10000.f;
	FString Travelled;
	int32 TravelCount = 0;
	Site->SetTravelOverride([&Travelled, &TravelCount](const FString& Map)
	{
		Travelled = Map;
		++TravelCount;
	});

	TestFalse(TEXT("Finish before the show"), Site->TryFinish(Pawn));
	TestTrue(TEXT("Still idle"), Stage->GetPhase() == EGFRunwayPhase::Idle);
	TestFalse(TEXT("Unknown theme"), Site->TrySelectTheme(Pawn, TEXT("Nope")));
	TestTrue(TEXT("Pick Explorer"), Site->TrySelectLook(Pawn, TEXT("Explorer")));
	TestEqual(TEXT("Explorer sticks"), Site->GetSelectedLook().ToString(), FString(TEXT("Explorer")));
	TestTrue(TEXT("Pick Garden"), Site->TrySelectLook(Pawn, TEXT("Garden")));
	TestTrue(TEXT("Pick Garden Party"), Site->TrySelectTheme(Pawn, TEXT("GardenParty")));

	if (!TestTrue(TEXT("Start"), Site->TryStart(Pawn)))
	{
		return false;
	}
	TestTrue(TEXT("Dressing"), Stage->GetPhase() == EGFRunwayPhase::Dressing);
	TestEqual(TEXT("Playable dressing is short"), Stage->DressingSeconds, AGFRunwaySite::PlayDressingSeconds);
	TestEqual(TEXT("Playable walk is short"), Stage->WalkSeconds, AGFRunwaySite::PlayWalkSeconds);
	TestEqual(TEXT("Playable judging is short"), Stage->JudgingSeconds, AGFRunwaySite::PlayJudgingSeconds);
	const FGFRunwayContestant* Dressed = Stage->FindContestant(TEXT("Model"));
	if (!TestNotNull(TEXT("Contestant"), Dressed))
	{
		return false;
	}
	TestEqual(TEXT("Garden look is five pieces"), Dressed->Outfit.NumFilled(), 5);
	TestFalse(TEXT("A second start is refused"), Site->TryStart(Pawn));

	if (!TestTrue(TEXT("Finish"), Site->TryFinish(Pawn)))
	{
		return false;
	}
	TestTrue(TEXT("Results"), Stage->GetPhase() == EGFRunwayPhase::Results);
	const FGFRunwayContestant* Scored = Stage->FindContestant(TEXT("Model"));
	if (!TestNotNull(TEXT("Scored"), Scored))
	{
		return false;
	}
	TestEqual(TEXT("On-theme"), Scored->Score.Theme, 10.f);
	TestEqual(TEXT("Complete"), Scored->Score.Completeness, 10.f);
	TestEqual(TEXT("Rank 1"), Scored->Rank, 1);
	TestTrue(TEXT("A full look earns a purse"), Scored->Rewards.Num() > 0);
	TestEqual(TEXT("Purse paid once"), Stage->GetPendingRewards().Num(), 0);
	for (const FGFItemStack& Stack : Scored->Rewards)
	{
		TestEqual(*FString::Printf(TEXT("Bag holds %s"), *Stack.ItemId.ToString()), PS->Inventory->GetCount(Stack.ItemId), Stack.Count);
	}

	TArray<FString> Lines;
	Site->GetPromptLines(PS, Lines);
	FString Joined;
	for (const FString& Line : Lines)
	{
		Joined += Line;
		Joined += TEXT("\n");
	}
	TestTrue(TEXT("Results name the model"), Joined.Contains(TEXT("Model")));
	TestTrue(TEXT("Results name the hub"), Joined.Contains(TEXT("Hub")));

	TestEqual(TEXT("No travel yet"), TravelCount, 0);
	TestTrue(TEXT("Hub arch"), Site->TryReturn(Pawn));
	TestEqual(TEXT("Travelled once"), TravelCount, 1);
	TestEqual(TEXT("Back to the hub map"), Travelled, FGFMiniGameCatalog::HubMap().ToString());
	TestTrue(TEXT("Hub arch again does not travel twice"), Site->TryReturn(Pawn));
	TestEqual(TEXT("Still one travel"), TravelCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionDiscoverTest, "GFGame.Fashion.Playable.Discover", GFFashionPlayableTests::Flags)
bool FGFFashionDiscoverTest::RunTest(const FString&)
{
	using namespace GFFashionPlayableTests;
	FRunwayWorld W(TEXT("GFFashionDiscoverWorld"));
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

	AGFHubGateDirector* Director = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Director"), Director))
	{
		return false;
	}
	Director->Place(TEXT("/Game/Maps/L_Runway"), FVector::ZeroVector);

	AGFRunwayStage* Stage = AGFRunwayStage::Find(W.World);
	AGFRunwaySite* Site = AGFRunwaySite::Get(W.World);
	if (!TestNotNull(TEXT("Stage"), Stage) || !TestNotNull(TEXT("Site"), Site))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	const FVector Stand = Site->GetActorTransform().TransformPosition(FVector(80.f, 0.f, 140.f));
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Stand), Params);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Inventory"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Fresh");
	PS->SetPlayerId(4);
	PS->bCharacterReady = true;
	GS->AddPlayerState(PS);
	Pawn->SetPlayerState(PS);

	// No test kit and no crafted pieces. The wardrobe lends a catalog look.
	TestEqual(TEXT("Bag starts empty"), PS->Inventory->GetInventory().Items.Num(), 0);
	TestEqual(TEXT("Nothing has been granted"), PS->Inventory->GetRevision(), 0);
	TestTrue(TEXT("Dressing end is inside the runway HUD"), Site->IsPlayerNear(Pawn));
	Pawn->SetActorLocation(Site->GetActorLocation() + FVector(8000.f, 0.f, 140.f));
	TestFalse(TEXT("The hint stays near the runway"), Site->IsPlayerNear(Pawn));
	Pawn->SetActorLocation(Stand);

	Site->ResultsSeconds = 10000.f;
	Site->SetTravelOverride([](const FString&) {});

	TArray<FString> IdleLines;
	Site->GetPromptLines(PS, IdleLines);
	const FString Idle = JoinLines(IdleLines);
	TestEqual(TEXT("One current step at arrival"), CountCurrentSteps(IdleLines), 1);
	TestTrue(TEXT("Arrival step is the theme"), Idle.Contains(TEXT("> Pick a theme.")));
	TestTrue(TEXT("Arrival explains the empty closet"), Idle.Contains(TEXT("Pick a look first")));
	TestEqual(TEXT("Arch asks for a theme"), Site->GetStartCue(), FString(TEXT("Pick a theme first")));

	TestFalse(TEXT("Start before a theme"), Site->TryStart(Pawn));
	TestTrue(TEXT("Show stays idle"), Stage->GetPhase() == EGFRunwayPhase::Idle);
	TestTrue(TEXT("Refused start names the theme"), Site->LastPrompt.Contains(TEXT("Pick a theme first")));

	if (!TestTrue(TEXT("Pick Garden Party"), Site->TrySelectTheme(Pawn, TEXT("GardenParty"))))
	{
		return false;
	}
	TestTrue(TEXT("Theme sticks"), Site->HasChosenTheme());
	TArray<FString> ThemedLines;
	Site->GetPromptLines(PS, ThemedLines);
	const FString Themed = JoinLines(ThemedLines);
	TestNotEqual(TEXT("Prompt changes after the theme"), Themed, Idle);
	TestEqual(TEXT("Still one current step"), CountCurrentSteps(ThemedLines), 1);
	TestTrue(TEXT("Next step is the wardrobe"), Themed.Contains(TEXT("> Pick a look on the wardrobe.")));
	TestFalse(TEXT("Theme is no longer the current step"), Themed.Contains(TEXT("> Pick a theme.")));
	TestEqual(TEXT("Arch asks for a look"), Site->GetStartCue(), FString(TEXT("Pick a look first")));

	TestFalse(TEXT("Start before a look"), Site->TryStart(Pawn));
	TestTrue(TEXT("Show still idle"), Stage->GetPhase() == EGFRunwayPhase::Idle);
	TestTrue(TEXT("Refused start names the look"), Site->LastPrompt.Contains(TEXT("Pick a look first")));
	TestEqual(TEXT("Bag still empty"), PS->Inventory->GetInventory().Items.Num(), 0);

	UBoxComponent* GardenSpot = Site->GetLookTrigger(TEXT("Garden"));
	UBoxComponent* StartSpot = Site->GetVerbTrigger(TEXT("Start"));
	UBoxComponent* FinishSpot = Site->GetVerbTrigger(TEXT("Finish"));
	if (!TestNotNull(TEXT("Garden spot"), GardenSpot) || !TestNotNull(TEXT("Start spot"), StartSpot) || !TestNotNull(TEXT("Finish spot"), FinishSpot))
	{
		return false;
	}
	Pawn->SetActorLocation(GardenSpot->GetComponentLocation());
	if (!TestTrue(TEXT("Pick Garden from the wardrobe"), Site->TrySelectLook(Pawn, TEXT("Garden"))))
	{
		return false;
	}
	TestTrue(TEXT("Look was chosen"), Site->HasChosenLook());
	TestTrue(TEXT("Wardrobe says Garden is on"), Site->GetWearingCue().Contains(TEXT("Wearing")) && Site->GetWearingCue().Contains(TEXT("Garden")));
	TestTrue(TEXT("Grant lists the pieces"), Site->LastPrompt.Contains(TEXT("Granted")) && Site->LastPrompt.Contains(TEXT("Petal Blouse")));
	TestEqual(TEXT("Wardrobe did not fill the bag"), PS->Inventory->GetInventory().Items.Num(), 0);
	TestEqual(TEXT("Arch can start the show"), Site->GetStartCue(), FString(TEXT("Walk through to start the show")));

	TArray<FString> DressedLines;
	Site->GetPromptLines(PS, DressedLines);
	const FString Dressed = JoinLines(DressedLines);
	TestNotEqual(TEXT("Prompt changes after the look"), Dressed, Themed);
	TestEqual(TEXT("One step after the look"), CountCurrentSteps(DressedLines), 1);
	TestTrue(TEXT("Next step is the start arch"), Dressed.Contains(TEXT("> Walk through the Start arch.")));
	TestFalse(TEXT("Look is no longer the missing step"), Dressed.Contains(TEXT("Pick a look first")));

	Pawn->SetActorLocation(StartSpot->GetComponentLocation());
	if (!TestTrue(TEXT("Walk the start arch"), Site->TryStart(Pawn)))
	{
		return false;
	}
	TestTrue(TEXT("Dressing"), Stage->GetPhase() == EGFRunwayPhase::Dressing);
	TestEqual(TEXT("Playable dressing is short"), Stage->DressingSeconds, AGFRunwaySite::PlayDressingSeconds);
	const FGFRunwayContestant* Contestant = Stage->FindContestant(TEXT("Fresh"));
	if (!TestNotNull(TEXT("Contestant"), Contestant))
	{
		return false;
	}
	TestEqual(TEXT("Starter look is five pieces"), Contestant->Outfit.NumFilled(), 5);
	TestEqual(TEXT("Still no kit in the bag"), PS->Inventory->GetInventory().Items.Num(), 0);
	TestEqual(TEXT("Arch points at the finish"), Site->GetStartCue(), FString(TEXT("Show is on. Walk to Finish.")));

	TArray<FString> WalkLines;
	Site->GetPromptLines(PS, WalkLines);
	const FString Walk = JoinLines(WalkLines);
	TestNotEqual(TEXT("Prompt changes once the show is on"), Walk, Dressed);
	TestTrue(TEXT("Walk to the finish"), Walk.Contains(TEXT("> Walk to the Finish arch.")));

	Pawn->SetActorLocation(FinishSpot->GetComponentLocation());
	if (!TestTrue(TEXT("Walk the finish arch"), Site->TryFinish(Pawn)))
	{
		return false;
	}
	TestTrue(TEXT("Results"), Stage->GetPhase() == EGFRunwayPhase::Results);
	const FGFRunwayContestant* Scored = Stage->FindContestant(TEXT("Fresh"));
	if (!TestNotNull(TEXT("Scored"), Scored))
	{
		return false;
	}
	TestEqual(TEXT("On-theme"), Scored->Score.Theme, 10.f);
	TestTrue(TEXT("A lent look still earns a purse"), Scored->Rewards.Num() > 0);
	TestEqual(TEXT("Purse paid once"), Stage->GetPendingRewards().Num(), 0);

	TArray<FString> ResultLines;
	Site->GetPromptLines(PS, ResultLines);
	const FString Results = JoinLines(ResultLines);
	TestNotEqual(TEXT("Prompt changes at the scores"), Results, Walk);
	TestTrue(TEXT("Results name the player"), Results.Contains(TEXT("Fresh")));
	TestTrue(TEXT("Results name the Hub"), Results.Contains(TEXT("Hub")));
	return true;
}

#endif
