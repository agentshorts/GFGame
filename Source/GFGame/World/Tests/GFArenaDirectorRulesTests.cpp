// Director wiring for the arena rules: elders, species cap, snacks, purse, wary rentals,
// a hostile champion, spectator side bets and the hourly PvP wager limit.

#include "Misc/AutomationTest.h"
#include "GFArenaDirector.h"
#include "GFArenaRules.h"
#include "GFArenaTypes.h"
#include "GFCompanionRoster.h"
#include "GFEconomy.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFArenaDirectorRulesTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FRulesWorld
	{
		UWorld* World = nullptr;

		explicit FRulesWorld(const TCHAR* Name)
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

		~FRulesWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaDirectorRulesTest, "GFGame.Arena.Playable.Rules", GFArenaDirectorRulesTests::Flags)
bool FGFArenaDirectorRulesTest::RunTest(const FString&)
{
	using namespace GFArenaDirectorRulesTests;
	FRulesWorld W(TEXT("GFArenaRulesWorld"));
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

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto MakePlayer = [&](const TCHAR* Key, int32 PlayerId) -> AGFPlayerState*
	{
		AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
		if (!PS || !PS->Inventory)
		{
			return nullptr;
		}
		PS->PlayerKey = Key;
		PS->SetPlayerId(PlayerId);
		PS->bCharacterReady = true;
		GS->AddPlayerState(PS);
		return PS;
	};
	AGFPlayerState* PS = MakePlayer(TEXT("Sand"), 21);
	AGFPlayerState* Other = MakePlayer(TEXT("Rail"), 22);
	AGFPlayerState* Watcher = MakePlayer(TEXT("Stand"), 23);
	AGFArenaDirector* Arena = AGFArenaDirector::Get(W.World);
	if (!TestNotNull(TEXT("Sand"), PS) || !TestNotNull(TEXT("Rail"), Other) || !TestNotNull(TEXT("Watcher"), Watcher) || !TestNotNull(TEXT("Director"), Arena))
	{
		return false;
	}
	Arena->ForcedSeed = 901;
	Arena->bApplyWorldDilation = false;

	FGFCompanionEntry& Elder = PS->Companions.AddDefaulted_GetRef();
	Elder.Id = FGuid::NewGuid();
	Elder.Species = TEXT("Rex");
	Elder.Name = TEXT("Elder");
	Elder.bElder = true;
	Elder.bInParty = false;
	FGFModeRequest PlaceElder = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Place"));
	PlaceElder.Ids = { Elder.Id };
	PlaceElder.Ints = { 0 };
	const FGFModeResult ElderRes = Arena->HandleModeRequest(PS, PlaceElder);
	TestFalse(TEXT("An elder does not take a spot"), ElderRes.bOk);
	TestEqual(TEXT("Elder"), ElderRes.Code.ToString(), FString(TEXT("Elder")));

	const FGuid RexA = FGFCompanions::AddTamed(PS->Companions, TEXT("Rex"), TEXT("A"));
	const FGuid RexB = FGFCompanions::AddTamed(PS->Companions, TEXT("Rex"), TEXT("B"));
	const FGuid RexC = FGFCompanions::AddTamed(PS->Companions, TEXT("Rex"), TEXT("C"));
	FGFModeRequest PlaceSame = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Place"));
	PlaceSame.Ids = { RexA, RexB, RexC };
	PlaceSame.Ints = { 0, 1, 2 };
	const FGFModeResult SpeciesRes = Arena->HandleModeRequest(PS, PlaceSame);
	TestFalse(TEXT("A third Rex is over the species cap"), SpeciesRes.bOk);
	TestEqual(TEXT("SpeciesCap"), SpeciesRes.Code.ToString(), FString(TEXT("SpeciesCap")));

	FGFModeRequest PlaceOne = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Place"));
	PlaceOne.Ids = { RexA };
	PlaceOne.Ints = { 1 };
	TestTrue(TEXT("One Rex places"), Arena->HandleModeRequest(PS, PlaceOne).bOk);

	TestTrue(TEXT("Bag has a berry and a roast"), PS->Inventory->AddItem(TEXT("Berry"), 1) && PS->Inventory->AddItem(TEXT("EmberRoast"), 1));
	FGFModeRequest BadSnack = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Snack"));
	BadSnack.Ids = { RexA };
	BadSnack.Names = { TEXT("Berry") };
	const FGFModeResult Refused = Arena->HandleModeRequest(PS, BadSnack);
	TestEqual(TEXT("Wrong food is refused"), Refused.Code.ToString(), FString(TEXT("Refused")));
	TestEqual(TEXT("Wrong food stays in the bag"), PS->Inventory->GetCount(TEXT("Berry")), 1);

	FGFModeRequest GoodSnack = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Snack"));
	GoodSnack.Ids = { RexA };
	GoodSnack.Names = { TEXT("EmberRoast") };
	TestTrue(TEXT("Favourite snack"), Arena->HandleModeRequest(PS, GoodSnack).bOk);
	TestEqual(TEXT("Roast was eaten"), PS->Inventory->GetCount(TEXT("EmberRoast")), 0);
	const FGFArenaEntry* Fed = Arena->FindEntry(PS->PlayerKey);
	TestTrue(TEXT("Fed once"), Fed && Fed->FedIds.Contains(RexA));
	TestEqual(TEXT("Second snack"), Arena->HandleModeRequest(PS, GoodSnack).Code.ToString(), FString(TEXT("AlreadyFed")));

	FGFModeRequest Loadout = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Loadout"));
	Loadout.Names = { TEXT("SparkBolt"), TEXT("BloomHeal") };
	TestTrue(TEXT("Loadout"), Arena->HandleModeRequest(PS, Loadout).bOk);

	TestTrue(TEXT("Watcher has amber"), Watcher->Inventory->AddItem(TEXT("Amber"), 8));
	const int32 AmberBefore = Watcher->Inventory->GetCount(TEXT("Amber"));
	FGFModeRequest Bet = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("SideBet"));
	Bet.Stacks = { FGFItemStack(TEXT("Amber"), 1) };
	Bet.Ints = { 0 };
	TestTrue(TEXT("Spectator side bet"), Arena->HandleModeRequest(Watcher, Bet).bOk);
	TestEqual(TEXT("Side bet left the bag"), Watcher->Inventory->GetCount(TEXT("Amber")), AmberBefore - 1);
	Arena->OnModePlayerLeft(Watcher);
	TestEqual(TEXT("Leaving refunds the side bet"), Watcher->Inventory->GetCount(TEXT("Amber")), AmberBefore);

	FGFModeRequest NoWager = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("NoWager"));
	TestTrue(TEXT("No wager"), Arena->HandleModeRequest(PS, NoWager).bOk);
	FGFModeRequest Fight = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Fight"));
	if (!TestTrue(TEXT("Fight"), Arena->HandleModeRequest(PS, Fight).bOk))
	{
		return false;
	}
	const FGFArenaUnit* FedUnit = nullptr;
	for (const FGFArenaUnit& U : Arena->GetSim().Units)
	{
		if (U.CompanionId == RexA)
		{
			FedUnit = &U;
			break;
		}
	}
	TestTrue(TEXT("Snack arrives well fed"), FedUnit && FedUnit->bWellFed && !FedUnit->bControlBlocked);
	if (!TestTrue(TEXT("Finish"), Arena->ResolvePlayableBout()))
	{
		return false;
	}
	const FGFArenaEntry* Done = Arena->FindEntry(PS->PlayerKey);
	const FGFCompanionEntry* Grown = FGFCompanions::Find(PS->Companions, RexA);
	if (!TestNotNull(TEXT("Finished entry"), Done) || !TestNotNull(TEXT("Rex still owned"), Grown))
	{
		return false;
	}
	const int32 RewardValue = static_cast<int32>(FGFEconomy::StacksValue(GS->GetItemTable(), Done->Rewards));
	TestEqual(TEXT("Purse is 5 percent of the reward"), Done->PurseCredited, FGFArenaPurse::Purse(RewardValue));
	TestEqual(TEXT("Purse landed on the dino"), Grown->LifetimeEarnings, Done->PurseCredited);
	TestEqual(TEXT("Stars recorded"), PS->Progress.ArenaStars, Done->Stars);
	TestEqual(TEXT("One match"), PS->Progress.ArenaMatches, 1);

	// A wary rental cannot be steered, and a hostile family makes the rival a champion.
	PS->Companions.Reset();
	Arena->Entries.Reset();
	const FGuid Rental = FGFCompanions::AddTamed(PS->Companions, TEXT("Raptor"), TEXT("Hired"));
	FGFCompanionEntry* Hired = FGFCompanions::Find(PS->Companions, Rental);
	if (!TestNotNull(TEXT("Rental"), Hired))
	{
		return false;
	}
	Hired->bRental = true;
	Hired->Family = TEXT("Scale");
	Arena->SetFamilyRep(TEXT("Scale"), -10);
	FGFModeRequest PlaceRental = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Place"));
	PlaceRental.Ids = { Rental };
	PlaceRental.Ints = { 0 };
	TestTrue(TEXT("Wary rental places"), Arena->HandleModeRequest(PS, PlaceRental).bOk);
	TestTrue(TEXT("Wary fight"), Arena->HandleModeRequest(PS, Fight).bOk);
	const FGFArenaUnit* Blocked = nullptr;
	for (const FGFArenaUnit& U : Arena->GetSim().Units)
	{
		if (U.CompanionId == Rental)
		{
			Blocked = &U;
			break;
		}
	}
	TestTrue(TEXT("Wary rental cannot be steered"), Blocked && Blocked->bControlBlocked && !Blocked->bWellFed);
	if (Blocked)
	{
		TestEqual(TEXT("Possess says Wary"), Arena->EditSimForTests().Possess(PS->PlayerKey, Blocked->Id).ToString(), FString(TEXT("Wary")));
	}
	Arena->AbortFight();

	Arena->SetFamilyRep(TEXT("Scale"), -40);
	TestTrue(TEXT("Hostile rental places"), Arena->HandleModeRequest(PS, PlaceRental).bOk);
	TestTrue(TEXT("Hostile fight"), Arena->HandleModeRequest(PS, Fight).bOk);
	TestEqual(TEXT("Hostile family upgrades the rival"), static_cast<int32>(Arena->GetSim().TeamAI[1]), static_cast<int32>(EGFArenaAI::Champion));
	Arena->AbortFight();

	// Three bouts where both players stake, then the next stake between them is refused.
	PS->Companions.Reset();
	Other->Companions.Reset();
	Arena->Entries.Reset();
	const FGuid Left = FGFCompanions::AddTamed(PS->Companions, TEXT("Nipper"), TEXT("Left"));
	const FGuid Right = FGFCompanions::AddTamed(Other->Companions, TEXT("Sailback"), TEXT("Right"));
	TestTrue(TEXT("Stake amber"), PS->Inventory->AddItem(TEXT("Amber"), 40) && Other->Inventory->AddItem(TEXT("Amber"), 40));
	for (int32 Bout = 0; Bout < 3; ++Bout)
	{
		FGFModeRequest PlaceL = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Place"));
		PlaceL.Ids = { Left };
		PlaceL.Ints = { 0 };
		FGFModeRequest PlaceR = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Place"));
		PlaceR.Ids = { Right };
		PlaceR.Ints = { 1 };
		FGFModeRequest Stake = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Wager"));
		Stake.Stacks = { FGFItemStack(TEXT("Amber"), 1) };
		if (!TestTrue(*FString::Printf(TEXT("Bout %d place"), Bout), Arena->HandleModeRequest(PS, PlaceL).bOk && Arena->HandleModeRequest(Other, PlaceR).bOk))
		{
			return false;
		}
		if (!TestTrue(*FString::Printf(TEXT("Bout %d stakes"), Bout), Arena->HandleModeRequest(PS, Stake).bOk && Arena->HandleModeRequest(Other, Stake).bOk))
		{
			return false;
		}
		if (!TestTrue(*FString::Printf(TEXT("Bout %d fight"), Bout), Arena->HandleModeRequest(PS, Fight).bOk && Arena->ResolvePlayableBout()))
		{
			return false;
		}
	}
	FGFModeRequest AgainL = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Place"));
	AgainL.Ids = { Left };
	AgainL.Ints = { 0 };
	FGFModeRequest AgainStake = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Wager"));
	AgainStake.Stacks = { FGFItemStack(TEXT("Amber"), 1) };
	TestTrue(TEXT("Fourth stake from one side still opens"), Arena->HandleModeRequest(PS, AgainL).bOk && Arena->HandleModeRequest(PS, AgainStake).bOk);
	FGFModeRequest AgainR = FGFModeRequest::Make(AGFArenaDirector::ModeName, TEXT("Place"));
	AgainR.Ids = { Right };
	AgainR.Ints = { 3 };
	TestTrue(TEXT("Other side places"), Arena->HandleModeRequest(Other, AgainR).bOk);
	const FGFModeResult Limited = Arena->HandleModeRequest(Other, AgainStake);
	TestFalse(TEXT("The fourth PvP wager is blocked"), Limited.bOk);
	TestEqual(TEXT("WagerLimit"), Limited.Code.ToString(), FString(TEXT("WagerLimit")));
	return true;
}

#endif
