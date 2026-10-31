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

#endif
