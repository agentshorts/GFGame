// Arena greybox, playable from the hub pad: the gate drops you on the pad, Start / Finish / Results
// run a real no-wager bout, and the arch puts you back on the hub stand. No new map.

#include "Misc/AutomationTest.h"
#include "GFArenaDirector.h"
#include "GFArenaSite.h"
#include "GFArenaTypes.h"
#include "GFCompanionRoster.h"
#include "GFGameState.h"
#include "GFHubGateDirector.h"
#include "GFInventoryComponent.h"
#include "GFMiniGameCatalog.h"
#include "GFMiniGameGate.h"
#include "GFPlayerState.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFArenaPlayableTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FArenaWorld
	{
		UWorld* World = nullptr;

		explicit FArenaWorld(const TCHAR* Name)
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

		~FArenaWorld()
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
		for (TActorIterator<AGFArenaSite> It(World); It; ++It)
		{
			++N;
		}
		return N;
	}

	static AGFMiniGameGate* ArenaPortal(UWorld* World)
	{
		for (TActorIterator<AGFMiniGameGate> It(World); It; ++It)
		{
			if (It->EntryId == FName(TEXT("Arena")) && It->GateRole == EGFMiniGameGateRole::Portal)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static bool OutcomeWord(const FString& Text)
	{
		return Text.Contains(TEXT("Win")) || Text.Contains(TEXT("Loss")) || Text.Contains(TEXT("Draw"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaSiteOnPadTest, "GFGame.Arena.Playable.SiteOnPad", GFArenaPlayableTests::Flags)
bool FGFArenaSiteOnPadTest::RunTest(const FString&)
{
	using namespace GFArenaPlayableTests;
	FArenaWorld W(TEXT("GFArenaSiteWorld"));
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

	AGFArenaSite* Site = AGFArenaSite::Get(W.World);
	if (!TestNotNull(TEXT("Hub place builds an arena site"), Site))
	{
		return false;
	}
	TestEqual(TEXT("One site"), CountSites(W.World), 1);

	const FGFMiniGameEntry* ArenaRow = FGFMiniGameCatalog::Find(TEXT("Arena"));
	AGFArenaDirector* Arena = AGFArenaDirector::Get(W.World, false);
	if (!TestNotNull(TEXT("Arena catalog row"), ArenaRow) || !TestNotNull(TEXT("Arena director"), Arena))
	{
		return false;
	}
	TestFalse(TEXT("Arena stays a sim, not a map"), ArenaRow->HasMap());
	TestFalse(TEXT("No L_Arena package"), ArenaRow->Map.ToString().Contains(TEXT("L_Arena")));
	TestTrue(TEXT("Director on the arena pad"), Arena->GetActorLocation().Equals(FVector(0.f, 9000.f, 0.f), 1.f));
	TestTrue(TEXT("Site follows the director"), Site->GetActorLocation().Equals(Arena->GetActorLocation(), 1.f));
	TestFalse(TEXT("Hub pad does not dilate the world"), Arena->bApplyWorldDilation);

	const FName Verbs[] = { TEXT("Start"), TEXT("Finish"), TEXT("Results"), TEXT("Return") };
	for (const FName Verb : Verbs)
	{
		TestTrue(*FString::Printf(TEXT("Has %s"), *Verb.ToString()), Site->HasVerb(Verb));
		UBoxComponent* Box = Site->GetVerbTrigger(Verb);
		if (TestNotNull(*FString::Printf(TEXT("%s spot"), *Verb.ToString()), Box))
		{
			TestTrue(*FString::Printf(TEXT("%s overlaps"), *Verb.ToString()), Box->GetGenerateOverlapEvents());
			TestTrue(*FString::Printf(TEXT("%s hits pawns"), *Verb.ToString()), Box->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap);
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
	const TCHAR* Words[] = { TEXT("Start"), TEXT("Finish"), TEXT("Results"), TEXT("Return"), TEXT("Balcony") };
	for (const TCHAR* Word : Words)
	{
		TestTrue(*FString::Printf(TEXT("Prompt names %s"), Word), Joined.Contains(Word));
	}

	AGFMiniGameGate* Portal = ArenaPortal(W.World);
	if (!TestNotNull(TEXT("Arena portal"), Portal))
	{
		return false;
	}
	TestEqual(TEXT("Portal says Arena"), Portal->GetLabel(), FString(TEXT("Arena")));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Walker = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(0.f, 0.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Walker"), Walker))
	{
		return false;
	}
	ACameraActor* Camera = Site->GetBalconyCamera();
	if (!TestNotNull(TEXT("Balcony camera"), Camera))
	{
		return false;
	}
	const FVector CamWorld = Site->GetActorTransform().TransformPosition(AGFArenaSite::BalconyCameraLocal());
	const FVector LookWorld = Site->GetActorTransform().TransformPosition(AGFArenaSite::BalconyLookLocal());
	TestTrue(TEXT("Camera sits on the south balcony"), Camera->GetActorLocation().Equals(CamWorld, 2.f));
	const FVector Toward = (LookWorld - Camera->GetActorLocation()).GetSafeNormal();
	TestTrue(TEXT("Camera looks at the sand"), Camera->GetActorForwardVector().Dot(Toward) > 0.95f);
	TestTrue(TEXT("Camera is above the pad"), Camera->GetActorLocation().Z > Site->GetActorLocation().Z + 400.f);
	TestFalse(TEXT("No controller, so the view stays put"), Site->ApplyBalconyView(Walker));

	TestFalse(TEXT("Hub stand is not on the arena pad"), Site->IsPlayerNear(Walker));
	TestTrue(TEXT("Walking into the portal enters the arena"), Portal->EnterFrom(Walker));
	const FVector Stand = FGFMiniGameCatalog::SimStand(*ArenaRow, FVector::ZeroVector);
	TestTrue(TEXT("Stands on the arena pad"), Walker->GetActorLocation().Equals(Stand, 5.f));
	TestTrue(TEXT("The stand is inside the arena"), Site->IsPlayerNear(Walker));

	Director->Place(TEXT("/Game/Maps/UEDPIE_0_L_Hub"), FVector::ZeroVector);
	TestEqual(TEXT("Placing again keeps a single site"), CountSites(W.World), 1);
	TestTrue(TEXT("Director stays on the pad"), Arena->GetActorLocation().Equals(FVector(0.f, 9000.f, 0.f), 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaPlayableLoopTest, "GFGame.Arena.Playable.Loop", GFArenaPlayableTests::Flags)
bool FGFArenaPlayableLoopTest::RunTest(const FString&)
{
	using namespace GFArenaPlayableTests;
	FArenaWorld W(TEXT("GFArenaLoopWorld"));
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

	AGFArenaDirector* Arena = AGFArenaDirector::Get(W.World, false);
	AGFArenaSite* Site = AGFArenaSite::Get(W.World);
	if (!TestNotNull(TEXT("Arena director"), Arena) || !TestNotNull(TEXT("Arena site"), Site))
	{
		return false;
	}
	TestFalse(TEXT("Dilation stays off"), Arena->bApplyWorldDilation);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(400.f, 9000.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Inventory"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Bout");
	PS->SetPlayerId(11);
	PS->bCharacterReady = true;
	GS->AddPlayerState(PS);
	Pawn->SetPlayerState(PS);
	TestTrue(TEXT("Bag starts with some wood"), PS->Inventory->AddItem(TEXT("Wood"), 4));
	const int32 Wood = PS->Inventory->GetCount(TEXT("Wood"));

	const FGFModeResult EarlyFinish = Site->TryFinish(Pawn);
	TestFalse(TEXT("Finish before a bout"), EarlyFinish.bOk);
	TestEqual(TEXT("NotFighting"), EarlyFinish.Code.ToString(), FString(TEXT("NotFighting")));
	const FGFModeResult EarlyResults = Site->TryResults(Pawn);
	TestFalse(TEXT("Results before a bout"), EarlyResults.bOk);
	TestEqual(TEXT("NotFinished"), EarlyResults.Code.ToString(), FString(TEXT("NotFinished")));

	FGFCompanions::AddTamed(PS->Companions, TEXT("Rex"), TEXT("Rex"));
	const FGFModeResult Kept = Site->TryStart(Pawn);
	if (!TestTrue(TEXT("Start with a roster"), Kept.bOk))
	{
		return false;
	}
	TestEqual(TEXT("Roster was not given a sparring dino"), PS->Companions.Num(), 1);
	TestEqual(TEXT("Still Rex"), PS->Companions[0].Species.ToString(), FString(TEXT("Rex")));
	TestTrue(TEXT("Bout is fighting"), Arena->Phase == EGFArenaPhase::Fighting);
	const FGFModeResult SecondStart = Site->TryStart(Pawn);
	TestFalse(TEXT("Start again is refused"), SecondStart.bOk);
	TestEqual(TEXT("Fighting"), SecondStart.Code.ToString(), FString(TEXT("Fighting")));
	TestEqual(TEXT("Still one dino"), PS->Companions.Num(), 1);

	const FGFModeResult Finished = Site->TryFinish(Pawn);
	if (!TestTrue(TEXT("Finish resolves the bout"), Finished.bOk))
	{
		return false;
	}
	TestTrue(TEXT("Bout is over"), Arena->Phase == EGFArenaPhase::Finished);
	TestTrue(TEXT("A real outcome"), Arena->Outcome != EGFArenaOutcome::None);
	TestEqual(TEXT("One match"), PS->Progress.ArenaMatches, 1);
	TestTrue(TEXT("Wood was not the stake"), PS->Inventory->GetCount(TEXT("Wood")) >= Wood);
	const FGFArenaEntry* KeptEntry = Arena->FindEntry(PS->PlayerKey);
	if (!TestNotNull(TEXT("Entry"), KeptEntry))
	{
		return false;
	}
	TestTrue(TEXT("No stake was put up"), KeptEntry->LastStake.Num() == 0 && KeptEntry->Stake.Num() == 0);
	TestTrue(TEXT("No wager result"), KeptEntry->WagerResult.IsNone());
	TestFalse(TEXT("Escrow stayed closed"), KeptEntry->bEscrowOpen);
	TestTrue(TEXT("Stars are in range"), KeptEntry->Stars >= 0 && KeptEntry->Stars <= 3);

	const FGFModeResult Read = Site->TryResults(Pawn);
	TestTrue(TEXT("Results"), Read.bOk && OutcomeWord(Site->LastPrompt) && Site->LastPrompt.Contains(TEXT("star")));
	TestTrue(TEXT("Finish again does not roll another bout"), Site->TryFinish(Pawn).bOk && PS->Progress.ArenaMatches == 1);

	PS->Companions.Reset();
	Arena->Entries.Reset();
	const FGFModeResult Loaned = Site->TryStart(Pawn);
	if (!TestTrue(TEXT("Empty roster can start"), Loaned.bOk))
	{
		return false;
	}
	TestEqual(TEXT("One sparring dino"), PS->Companions.Num(), 1);
	TestEqual(TEXT("Nipper"), PS->Companions[0].Species.ToString(), FString(TEXT("Nipper")));
	TestEqual(TEXT("Named Sparring"), PS->Companions[0].Name, FString(TEXT("Sparring")));
	TestTrue(TEXT("Second bout fighting"), Arena->Phase == EGFArenaPhase::Fighting);

	const FGFModeResult LoanedFinish = Site->TryFinish(Pawn);
	if (!TestTrue(TEXT("Second finish"), LoanedFinish.bOk))
	{
		return false;
	}
	TestEqual(TEXT("Two matches"), PS->Progress.ArenaMatches, 2);
	TestTrue(TEXT("Still finished"), Arena->Phase == EGFArenaPhase::Finished);
	const FGFModeResult LoanedRead = Site->TryResults(Pawn);
	TestTrue(TEXT("Second results"), LoanedRead.bOk && OutcomeWord(Site->LastPrompt));
	TestTrue(TEXT("Wood still not spent"), PS->Inventory->GetCount(TEXT("Wood")) >= Wood);

	const int32 Owned = PS->Companions.Num();
	const FGFModeResult Again = Site->TryStart(Pawn);
	TestTrue(TEXT("A finished bout can start again"), Again.bOk);
	TestEqual(TEXT("Sparring dino is not duplicated"), PS->Companions.Num(), Owned);
	TestTrue(TEXT("Third bout fighting"), Arena->Phase == EGFArenaPhase::Fighting);
	TestTrue(TEXT("Third finish"), Site->TryFinish(Pawn).bOk);

	TestTrue(TEXT("Return arch"), Site->TryReturn(Pawn));
	TestTrue(TEXT("Back on the hub stand"), Pawn->GetActorLocation().Equals(Site->GetHubReturnLocation(), 5.f));
	TestFalse(TEXT("The hub stand is outside the arena HUD"), Site->IsPlayerNear(Pawn));
	TestTrue(TEXT("Hub stand is the Rime anchor plus 120"), Site->GetHubReturnLocation().Equals(FVector(0.f, 0.f, AGFArenaSite::HubStandZCm), 1.f));
	TestFalse(TEXT("Dilation still off after the bouts"), Arena->bApplyWorldDilation);
	return true;
}

#endif
