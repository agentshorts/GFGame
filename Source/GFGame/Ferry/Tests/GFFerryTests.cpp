// Ferry fare, pass, protection, land-route spawns, stop layout, and the default spawn anchor.
// Pure cases name factions and species. Production ferry code does not.

#include "Misc/AutomationTest.h"
#include "GFFerryDirector.h"
#include "GFFerryDino.h"
#include "GFFerryRiderComponent.h"
#include "GFFerryRules.h"
#include "GFFerrySpawn.h"
#include "GFCharacter.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GFGameMode.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFProtectedActor.h"
#include "GFTerritorySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFFerryTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Rime(TEXT("Rime"));
	static const FName Cinder(TEXT("Cinderwrights"));
	static const FName LavaBridge(TEXT("LavaBridge"));
	static const FName DeadlyForest(TEXT("DeadlyForest"));
	static const FString RiderKey(TEXT("Rider"));

	static bool LoadTerritory(FGFTerritoryMap& Map)
	{
		const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
		if (!Defaults)
		{
			return false;
		}
		Map = FGFTerritoryMap();
		Map.Curve = Defaults->Curve;
		Map.Neutral = Defaults->NeutralZone;
		Map.Factions = Defaults->FactionLayout;
		Map.Hazards = Defaults->Hazards;
		return Map.FindFaction(Rime) && Map.FindFaction(Cinder);
	}

	static bool LoadFerry(const AGFFerryDirector*& OutDirector)
	{
		OutDirector = GetDefault<AGFFerryDirector>();
		return OutDirector && OutDirector->Stops.Num() > 0 && OutDirector->Routes.Num() > 0;
	}

	static const FGFFerryStop* HomeStop(const AGFFerryDirector& Director)
	{
		const FGFFerryRoute& Route = Director.Routes[0];
		return FGFFerryRules::FindStop(Director.Stops, Route.StopA);
	}

	static const FGFFerryStop* FarStop(const AGFFerryDirector& Director)
	{
		const FGFFerryRoute& Route = Director.Routes[0];
		return FGFFerryRules::FindStop(Director.Stops, Route.StopB);
	}

	static FGFTerritoryHazard LavaBox(const FGFTerritoryMap& Map)
	{
		if (const FGFTerritoryHazard* Found = Map.Hazards.FindByPredicate([](const FGFTerritoryHazard& Hazard) { return Hazard.Id == LavaBridge; }))
		{
			return *Found;
		}
		return FGFTerritoryHazard();
	}

	static void Give(FGFMapLedgerAccounts& Accounts, const FString& Key, FName Item, int32 Count)
	{
		Accounts.Accounts.FindOrAdd(Key).Add(Item, Count);
	}

	static int32 CountOf(const FGFMapLedgerAccounts& Accounts, const FString& Key, FName Item)
	{
		FGFInventory Inventory;
		return Accounts.ReadAccount(Key, Inventory) ? Inventory.GetCount(Item) : -1;
	}

	static bool Conserves(const FGFMapLedgerAccounts& Accounts, const FGFTransferLedger& Ledger, const FString& Key, FName Item, int32 Start)
	{
		const int64 Expected = int64(Start) + Ledger.GetMinted(Item) - Ledger.GetBurned(Item);
		return int64(CountOf(Accounts, Key, Item)) == Expected;
	}

	struct FWorldScope
	{
		UWorld* World = nullptr;
		explicit FWorldScope(UWorld* InWorld) : World(InWorld) {}
		~FWorldScope()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryFareTest, "GFGame.Ferry.Fare", GFFerryTests::Flags)
bool FGFFerryFareTest::RunTest(const FString&)
{
	using namespace GFFerryTests;

	const AGFFerryDirector* Director = nullptr;
	if (!TestTrue(TEXT("Shipped ferry config"), LoadFerry(Director)))
	{
		return false;
	}
	const FGFFerryTuning& Tuning = Director->Tuning;
	const FGFFerryStop* Home = HomeStop(*Director);
	const FGFFerryStop* Far = FarStop(*Director);
	if (!TestNotNull(TEXT("Home stop"), Home) || !TestNotNull(TEXT("Far stop"), Far))
	{
		return false;
	}

	const FGuid TxId(0xF3220001, 0xF3220002, 0xF3220003, 0xF3220004);
	FGFMapLedgerAccounts Accounts;
	FGFTransferLedger Ledger;
	Give(Accounts, RiderKey, Tuning.FareItem, Tuning.FareCount);

	const FGFFerryResult Paid = FGFFerryRules::ApplyFare(Accounts, Ledger, TxId, RiderKey, true, false, Home, Home->Location, Tuning);
	TestTrue(TEXT("Fare applied"), Paid.bOk);
	TestEqual(TEXT("Fare code"), Paid.Code, FGFFerryCodes::Ok);
	TestEqual(TEXT("Burns FareCount once"), CountOf(Accounts, RiderKey, Tuning.FareItem), 0);
	TestEqual(TEXT("Ledger burned the fare"), int32(Ledger.GetBurned(Tuning.FareItem)), Tuning.FareCount);

	const FGFFerryResult Replay = FGFFerryRules::ApplyFare(Accounts, Ledger, TxId, RiderKey, true, false, Home, Home->Location, Tuning);
	TestTrue(TEXT("Replay is ok"), Replay.bOk);
	TestEqual(TEXT("Replay does not charge again"), CountOf(Accounts, RiderKey, Tuning.FareItem), 0);
	TestEqual(TEXT("Burned total stays FareCount"), int32(Ledger.GetBurned(Tuning.FareItem)), Tuning.FareCount);

	FGFMapLedgerAccounts Short;
	FGFTransferLedger ShortLedger;
	const int32 TooFew = FMath::Max(0, Tuning.FareCount - 1);
	Give(Short, RiderKey, Tuning.FareItem, TooFew);
	const FGuid ShortTx(0xF3220011, 0xF3220012, 0xF3220013, 0xF3220014);
	const FGFFerryResult Denied = FGFFerryRules::ApplyFare(Short, ShortLedger, ShortTx, RiderKey, true, false, Home, Home->Location, Tuning);
	TestFalse(TEXT("Too few is refused"), Denied.bOk);
	TestEqual(TEXT("CantAfford"), Denied.Code, FGFFerryCodes::CantAfford);
	TestEqual(TEXT("Short inventory unchanged"), CountOf(Short, RiderKey, Tuning.FareItem), TooFew);
	TestEqual(TEXT("Short ledger burned nothing"), int32(ShortLedger.GetBurned(Tuning.FareItem)), 0);

	FGFMapLedgerAccounts PassHolder;
	FGFTransferLedger PassLedger;
	Give(PassHolder, RiderKey, Tuning.FareItem, Tuning.FareCount);
	Give(PassHolder, RiderKey, Tuning.PassItem, 1);
	const FGuid OutTx(0xF3220021, 0xF3220022, 0xF3220023, 0xF3220024);
	const FGuid BackTx(0xF3220031, 0xF3220032, 0xF3220033, 0xF3220034);
	const FGFFerryResult Out = FGFFerryRules::ApplyFare(PassHolder, PassLedger, OutTx, RiderKey, true, false, Home, Home->Location, Tuning);
	const FGFFerryResult Back = FGFFerryRules::ApplyFare(PassHolder, PassLedger, BackTx, RiderKey, true, false, Far, Far->Location, Tuning);
	TestTrue(TEXT("Pass outbound"), Out.bOk);
	TestTrue(TEXT("Pass return"), Back.bOk);
	TestEqual(TEXT("Quote is 0 with a pass"), FGFFerryRules::Quote(PassHolder.Accounts[RiderKey], Tuning), 0);
	TestEqual(TEXT("Pass holder keeps the fare item"), CountOf(PassHolder, RiderKey, Tuning.FareItem), Tuning.FareCount);
	TestEqual(TEXT("Pass holder burned no fare"), int32(PassLedger.GetBurned(Tuning.FareItem)), 0);
	TestEqual(TEXT("Pass still held"), CountOf(PassHolder, RiderKey, Tuning.PassItem), 1);

	const FVector Away = Home->Location + FVector(Tuning.BoardRadius + 50.f, 0.f, 0.f);
	FGFMapLedgerAccounts FarAccounts;
	FGFTransferLedger FarLedger;
	Give(FarAccounts, RiderKey, Tuning.FareItem, Tuning.FareCount);
	const FGuid AwayTx(0xF3220041, 0xF3220042, 0xF3220043, 0xF3220044);
	const FGFFerryResult NotThere = FGFFerryRules::ApplyFare(FarAccounts, FarLedger, AwayTx, RiderKey, true, false, Home, Away, Tuning);
	TestFalse(TEXT("Outside the disc is refused"), NotThere.bOk);
	TestEqual(TEXT("NotAtStop"), NotThere.Code, FGFFerryCodes::NotAtStop);
	TestEqual(TEXT("Away inventory unchanged"), CountOf(FarAccounts, RiderKey, Tuning.FareItem), Tuning.FareCount);

	const FVector Peak = FGFFerryRules::FlightPosition(Home->Location, Far->Location, 0.5f, Tuning.CruiseAltitude);
	const float MidZ = (Home->Location.Z + Far->Location.Z) * 0.5f + Tuning.CruiseAltitude;
	TestEqual(TEXT("Flight starts at the stop"), FGFFerryRules::FlightPosition(Home->Location, Far->Location, 0.f, Tuning.CruiseAltitude), Home->Location);
	TestEqual(TEXT("Flight ends at the far stop"), FGFFerryRules::FlightPosition(Home->Location, Far->Location, 1.f, Tuning.CruiseAltitude), Far->Location);
	TestTrue(TEXT("Arc peaks at cruise altitude"), FMath::IsNearlyEqual(Peak.Z, MidZ, 1.f));
	TestTrue(TEXT("Peak is above either stop"), Peak.Z > Home->Location.Z && Peak.Z > Far->Location.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryPassTest, "GFGame.Ferry.Pass", GFFerryTests::Flags)
bool FGFFerryPassTest::RunTest(const FString&)
{
	using namespace GFFerryTests;

	const AGFFerryDirector* Director = nullptr;
	if (!TestTrue(TEXT("Shipped ferry config"), LoadFerry(Director)))
	{
		return false;
	}
	const FGFFerryTuning& Tuning = Director->Tuning;
	const FGFFerryStop* Home = HomeStop(*Director);
	const FGFFerryStop* Far = FarStop(*Director);
	if (!TestNotNull(TEXT("Home stop"), Home) || !TestNotNull(TEXT("Far stop"), Far))
	{
		return false;
	}

	const FGuid RequestId(0xF3220101, 0xF3220102, 0xF3220103, 0xF3220104);
	FGFMapLedgerAccounts Accounts;
	FGFTransferLedger Ledger;
	Give(Accounts, RiderKey, Tuning.PassMaterialItem, Tuning.PassPrice);
	Give(Accounts, RiderKey, Tuning.FareItem, Tuning.FareCount);

	const FGFFerryResult Bought = FGFFerryRules::ApplyPass(Accounts, Ledger, RequestId, RiderKey, true, false, Home, Home->Location, Tuning);
	TestTrue(TEXT("Pass bought"), Bought.bOk);
	TestEqual(TEXT("Pass code"), Bought.Code, FGFFerryCodes::Ok);
	TestEqual(TEXT("Material burned"), CountOf(Accounts, RiderKey, Tuning.PassMaterialItem), 0);
	TestEqual(TEXT("Ledger burned the price"), int32(Ledger.GetBurned(Tuning.PassMaterialItem)), Tuning.PassPrice);
	TestEqual(TEXT("One pass minted"), CountOf(Accounts, RiderKey, Tuning.PassItem), 1);
	TestEqual(TEXT("Ledger minted one pass"), int32(Ledger.GetMinted(Tuning.PassItem)), 1);

	const int32 BurnedBefore = int32(Ledger.GetBurned(Tuning.PassMaterialItem));
	const int32 MintedBefore = int32(Ledger.GetMinted(Tuning.PassItem));
	const FGuid SecondId(0xF3220111, 0xF3220112, 0xF3220113, 0xF3220114);
	const FGFFerryResult Again = FGFFerryRules::ApplyPass(Accounts, Ledger, SecondId, RiderKey, true, false, Home, Home->Location, Tuning);
	TestFalse(TEXT("Second buy refused"), Again.bOk);
	TestEqual(TEXT("AlreadyHasPass"), Again.Code, FGFFerryCodes::AlreadyHasPass);
	TestEqual(TEXT("Second buy burns nothing"), int32(Ledger.GetBurned(Tuning.PassMaterialItem)), BurnedBefore);
	TestEqual(TEXT("Second buy mints nothing"), int32(Ledger.GetMinted(Tuning.PassItem)), MintedBefore);
	TestEqual(TEXT("Still one pass"), CountOf(Accounts, RiderKey, Tuning.PassItem), 1);
	TestEqual(TEXT("Material stays spent"), CountOf(Accounts, RiderKey, Tuning.PassMaterialItem), 0);

	FGFMapLedgerAccounts Poor;
	FGFTransferLedger PoorLedger;
	const int32 Short = FMath::Max(0, Tuning.PassPrice - 1);
	Give(Poor, RiderKey, Tuning.PassMaterialItem, Short);
	const FGuid PoorId(0xF3220121, 0xF3220122, 0xF3220123, 0xF3220124);
	const FGFFerryResult Cant = FGFFerryRules::ApplyPass(Poor, PoorLedger, PoorId, RiderKey, true, false, Home, Home->Location, Tuning);
	TestFalse(TEXT("Too little material"), Cant.bOk);
	TestEqual(TEXT("Pass CantAfford"), Cant.Code, FGFFerryCodes::CantAfford);
	TestEqual(TEXT("Poor material unchanged"), CountOf(Poor, RiderKey, Tuning.PassMaterialItem), Short);
	TestEqual(TEXT("Poor got no pass"), CountOf(Poor, RiderKey, Tuning.PassItem), 0);
	TestEqual(TEXT("Poor ledger burned nothing"), int32(PoorLedger.GetBurned(Tuning.PassMaterialItem)), 0);
	TestEqual(TEXT("Poor ledger minted nothing"), int32(PoorLedger.GetMinted(Tuning.PassItem)), 0);

	const FGuid TripTx(0xF3220131, 0xF3220132, 0xF3220133, 0xF3220134);
	const FGFFerryResult Free = FGFFerryRules::ApplyFare(Accounts, Ledger, TripTx, RiderKey, true, false, Far, Far->Location, Tuning);
	TestTrue(TEXT("Trip after the pass"), Free.bOk);
	TestEqual(TEXT("Fare item untouched after the pass"), CountOf(Accounts, RiderKey, Tuning.FareItem), Tuning.FareCount);
	TestEqual(TEXT("No fare burned after the pass"), int32(Ledger.GetBurned(Tuning.FareItem)), 0);

	TestTrue(TEXT("Pass conservation"), Conserves(Accounts, Ledger, RiderKey, Tuning.PassItem, 0));
	TestTrue(TEXT("Material conservation"), Conserves(Accounts, Ledger, RiderKey, Tuning.PassMaterialItem, Tuning.PassPrice));
	TestTrue(TEXT("Fare item conservation"), Conserves(Accounts, Ledger, RiderKey, Tuning.FareItem, Tuning.FareCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryNoAttackTest, "GFGame.Ferry.NoAttack", GFFerryTests::Flags)
bool FGFFerryNoAttackTest::RunTest(const FString&)
{
	using namespace GFFerryTests;

	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFFerryNoAttack"));
	FWorldScope Scope(World);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay() && World->GetWorldSettings())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	AGFGameState* GS = World->SpawnActor<AGFGameState>();
	if (GS)
	{
		World->SetGameState(GS);
	}
	if (!TestNotNull(TEXT("Game state"), GS))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFFerryDirector* Director = World->SpawnActor<AGFFerryDirector>(AGFFerryDirector::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Director"), Director) || !TestTrue(TEXT("Stops spawned"), Director->Stops.Num() > 0))
	{
		return false;
	}

	const FGFFerryStop* Home = HomeStop(*Director);
	if (!TestNotNull(TEXT("Home stop"), Home))
	{
		return false;
	}
	AGFFerryDino* Parked = nullptr;
	for (TActorIterator<AGFFerryDino> It(World); It; ++It)
	{
		if (!It->bTransit && It->StopId == Home->Id)
		{
			Parked = *It;
			break;
		}
	}
	if (!TestNotNull(TEXT("Parked ferry"), Parked))
	{
		return false;
	}
	const FVector ParkedAt = Parked->GetActorLocation();

	TestFalse(TEXT("Ferry cannot be damaged"), Parked->CanBeDamaged());
	TestTrue(TEXT("Faction protected tag"), FGFProtectedActor::Is(Parked));
	TestTrue(TEXT("Tag name"), Parked->ActorHasTag(FGFProtectedActor::Tag));
	TestNull(TEXT("No taming component"), Parked->FindComponentByClass<UGFTamingComponent>());
	TestEqual(TEXT("Ripwing placeholder"), Parked->BodyAssetKey, FName(TEXT("Dino.Ripwing")));
	TestEqual(TEXT("Perch anim"), Parked->AnimKey, FName(TEXT("Ripwing.Perch_Idle")));

	AGFDino* Hunter = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), ParkedAt + FVector(0.f, 300.f, 0.f), FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Hunter"), Hunter))
	{
		return false;
	}
	const float Dealt = UGameplayStatics::ApplyDamage(Parked, 40.f, nullptr, Hunter, nullptr);
	TestEqual(TEXT("ApplyDamage returns 0"), Dealt, 0.f);
	TestEqual(TEXT("Ferry did not move"), Parked->GetActorLocation(), ParkedAt);
	TestFalse(TEXT("Still undamageable"), Parked->CanBeDamaged());
	Hunter->SetAttackTarget(Parked);
	TestNull(TEXT("Dino cannot target the ferry"), Hunter->GetAttackTarget());

	AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Rider state"), PS) || !TestNotNull(TEXT("Inventory"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = RiderKey;
	PS->bCharacterReady = true;
	PS->SetPlayerId(41);
	if (GS->PlayerArray.Find(PS) == INDEX_NONE)
	{
		GS->AddPlayerState(PS);
	}
	TestTrue(TEXT("Fare items"), PS->Inventory->AddItem(Director->Tuning.FareItem, Director->Tuning.FareCount));

	AGFCharacter* Pawn = World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), Home->Location, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Rider pawn"), Pawn) || !TestNotNull(TEXT("Rider component"), Pawn->FerryRider.Get()))
	{
		return false;
	}
	Pawn->SetPlayerState(PS);

	const FGFFerryResult Trip = Director->RequestTrip(Pawn, Home->Id);
	TestTrue(TEXT("Trip accepted"), Trip.bOk);
	TestEqual(TEXT("Trip code"), Trip.Code, FGFFerryCodes::Ok);
	Director->Tick(0.05f);
	TestTrue(TEXT("Rider is in flight"), Pawn->FerryRider->IsInFlight());
	TestTrue(TEXT("Rider is faction protected"), FGFProtectedActor::Is(Pawn));
	TestFalse(TEXT("Rider cannot be damaged in flight"), Pawn->CanBeDamaged());
	Hunter->SetAttackTarget(Pawn);
	TestNull(TEXT("Dino cannot target a rider in flight"), Hunter->GetAttackTarget());

	Director->Tick(Director->Tuning.FlightSeconds);
	TestFalse(TEXT("Flight ended"), Pawn->FerryRider->IsInFlight());
	TestFalse(TEXT("Protection tag cleared"), FGFProtectedActor::Is(Pawn));
	TestTrue(TEXT("Damage restored"), Pawn->CanBeDamaged());
	TestEqual(TEXT("Stop ferry stayed parked"), Parked->GetActorLocation(), ParkedAt);
	TestFalse(TEXT("Parked ferry is not the transit copy"), Parked->bTransit);
	TestEqual(TEXT("Fare was burned"), PS->Inventory->GetCount(Director->Tuning.FareItem), 0);

	const FGFFerryStop* Far = FarStop(*Director);
	if (!TestNotNull(TEXT("Far stop"), Far))
	{
		return false;
	}
	const FVector Drop = FGFFerryRules::DropPoint(Home->Location, Far->Location, Home->Location, Director->Tuning.BoardRadius);
	TestTrue(TEXT("Rider set down past the far stop"), FVector::Dist(Pawn->GetActorLocation(), Drop) < 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerrySpawnerRouteOnlyTest, "GFGame.Ferry.SpawnerRouteOnly", GFFerryTests::Flags)
bool FGFFerrySpawnerRouteOnlyTest::RunTest(const FString&)
{
	using namespace GFFerryTests;

	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped territory"), LoadTerritory(Map)))
	{
		return false;
	}
	const AGFFerryDirector* Director = nullptr;
	if (!TestTrue(TEXT("Shipped ferry config"), LoadFerry(Director)))
	{
		return false;
	}
	const FGFFerryMobSpawn& Spawn = Director->MobSpawn;
	const FGFTerritoryHazard Box = LavaBox(Map);
	TestEqual(TEXT("LavaBridge box"), Box.Id, LavaBridge);

	auto HazardAt = [&Map](const FVector& Point) -> FName
	{
		const FGFTerritoryHazardHit Hit = Map.GetHazardAt(Point);
		return Hit.bHazard ? Hit.Id : NAME_None;
	};

	const int32 Counts[] = { 1, 3, 4 };
	for (int32 Members : Counts)
	{
		TestTrue(TEXT("Member counts"), Map.SetMembers(Rime, Members) && Map.SetMembers(Cinder, Members));
		int32 Produced = 0;
		for (int32 Seed = 1; Seed <= 40; ++Seed)
		{
			FRandomStream Rng(Seed + Members * 1000);
			FVector Point;
			if (!FGFFerryRouteSpawnRules::PickSpawnPoint(Box, LavaBridge, Rng, HazardAt, Point))
			{
				continue;
			}
			++Produced;
			const FGFTerritoryHazardHit Hit = Map.GetHazardAt(Point);
			TestTrue(*FString::Printf(TEXT("Seed %d members %d is a hazard"), Seed, Members), Hit.bHazard);
			TestEqual(*FString::Printf(TEXT("Seed %d members %d is LavaBridge"), Seed, Members), Hit.Id, LavaBridge);
			TestTrue(*FString::Printf(TEXT("Seed %d members %d is wild"), Seed, Members), Map.GetZoneAt(Point).Zone == EGFTerritoryZone::Wild);
			TestNotEqual(*FString::Printf(TEXT("Seed %d members %d is not the forest"), Seed, Members), Hit.Id, DeadlyForest);
			TestTrue(*FString::Printf(TEXT("Seed %d members %d is inside the box"), Seed, Members), FGFFerryRouteSpawnRules::ContainsHorizontal(Box, Point));
		}
		TestTrue(*FString::Printf(TEXT("Members %d produced a spawn"), Members), Produced > 0);
	}

	TestTrue(TEXT("Reset counts"), Map.SetMembers(Rime, 4) && Map.SetMembers(Cinder, 4));
	const FVector Owned(2000.f, 0.f, 0.f);
	TestTrue(TEXT("Owned sample is inside the lava box"), FGFFerryRouteSpawnRules::ContainsHorizontal(Box, Owned));
	TestFalse(TEXT("Owned land is not a spawn"), FGFFerryRouteSpawnRules::IsLegalSpawnPoint(Owned, LavaBridge, HazardAt));
	TestFalse(TEXT("Owned land is not LavaBridge"), Map.GetHazardAt(Owned).bHazard);

	const FGFTerritoryHazard* Forest = Map.Hazards.FindByPredicate([](const FGFTerritoryHazard& Hazard) { return Hazard.Id == DeadlyForest; });
	if (!TestNotNull(TEXT("DeadlyForest"), Forest))
	{
		return false;
	}
	TestEqual(TEXT("Forest center"), HazardAt(Forest->Center), DeadlyForest);
	TestFalse(TEXT("Forest is not a lava spawn"), FGFFerryRouteSpawnRules::IsLegalSpawnPoint(Forest->Center, LavaBridge, HazardAt));

	TArray<FGFFerryRoutePerson> Nobody;
	TestFalse(TEXT("Nobody near"), FGFFerryRouteSpawnRules::IsRouteActive(Box, Spawn.ActivationMargin, Nobody));
	TestFalse(TEXT("Inactive does not spawn"), FGFFerryRouteSpawnRules::CanSpawn(0, Spawn.MaxAlive, false));
	FGFFerryRoutePerson Flying;
	Flying.bFlying = true;
	Flying.Location = Box.Center;
	Flying.Key = TEXT("Air");
	TArray<FGFFerryRoutePerson> OnlyFlying;
	OnlyFlying.Add(Flying);
	TestFalse(TEXT("A flying rider does not activate the route"), FGFFerryRouteSpawnRules::IsRouteActive(Box, Spawn.ActivationMargin, OnlyFlying));

	TestFalse(TEXT("MaxAlive blocks another spawn"), FGFFerryRouteSpawnRules::CanSpawn(Spawn.MaxAlive, Spawn.MaxAlive, true));
	TestTrue(TEXT("Under MaxAlive can spawn"), FGFFerryRouteSpawnRules::CanSpawn(Spawn.MaxAlive - 1, Spawn.MaxAlive, true));

	const FVector Left = Box.Center + FVector(-(Box.HalfExtent.X + Spawn.ActivationMargin + 10.f), 0.f, 0.f);
	TestTrue(TEXT("Left the box"), FGFFerryRouteSpawnRules::ShouldDespawn(Box, Left, Spawn.ActivationMargin, 0.f, Spawn.IdleDespawnSeconds, true));
	TestTrue(TEXT("Idle with nobody near"), FGFFerryRouteSpawnRules::ShouldDespawn(Box, Box.Center, Spawn.ActivationMargin, Spawn.IdleDespawnSeconds, Spawn.IdleDespawnSeconds, false));
	TestFalse(TEXT("Idle while someone is near"), FGFFerryRouteSpawnRules::ShouldDespawn(Box, Box.Center, Spawn.ActivationMargin, Spawn.IdleDespawnSeconds, Spawn.IdleDespawnSeconds, true));

	const float Scaled = FGFFerryRouteSpawnRules::ScaledAttackDamage(10.f, Spawn.MobLevel, Spawn.MobDamagePerLevel);
	const float Expected = 10.f * (1.f + Spawn.MobDamagePerLevel * float(Spawn.MobLevel - 1));
	TestEqual(TEXT("Level scale"), Scaled, Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryLayoutTest, "GFGame.Ferry.Layout", GFFerryTests::Flags)
bool FGFFerryLayoutTest::RunTest(const FString&)
{
	using namespace GFFerryTests;

	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped territory"), LoadTerritory(Map)))
	{
		return false;
	}
	const AGFFerryDirector* Director = nullptr;
	if (!TestTrue(TEXT("Shipped ferry config"), LoadFerry(Director)))
	{
		return false;
	}

	for (const FGFFerryStop& Stop : Director->Stops)
	{
		TestTrue(*FString::Printf(TEXT("%s anchor is a faction"), *Stop.Id.ToString()), Map.FindFaction(Stop.AnchorId) != nullptr);
		TestTrue(*FString::Printf(TEXT("%s members"), *Stop.AnchorId.ToString()), Map.SetMembers(Stop.AnchorId, 1));
		const FGFTerritoryFaction* Anchor = Map.FindFaction(Stop.AnchorId);
		const float Radius = Map.RadiusOf(*Anchor);
		const float Dist = FVector::Dist2D(Stop.Location, Anchor->Center);
		TestTrue(*FString::Printf(TEXT("%s is inside the 1-member disc (%.0f <= %.0f)"), *Stop.Id.ToString(), Dist, Radius), Dist <= Radius);
	}

	const FGFFerryRoute& Route = Director->Routes[0];
	const FGFFerryStop* StopA = FGFFerryRules::FindStop(Director->Stops, Route.StopA);
	const FGFFerryStop* StopB = FGFFerryRules::FindStop(Director->Stops, Route.StopB);
	if (!TestNotNull(TEXT("StopA"), StopA) || !TestNotNull(TEXT("StopB"), StopB))
	{
		return false;
	}
	TestEqual(TEXT("StopA is the Rime stop"), StopA->AnchorId, Rime);

	const FGFTerritoryHazard* Hazard = Map.Hazards.FindByPredicate([&Route](const FGFTerritoryHazard& Entry) { return Entry.Id == Route.LandRouteHazardId; });
	if (!TestNotNull(TEXT("Land route hazard"), Hazard))
	{
		return false;
	}
	const bool bMatch =
		(Hazard->BetweenA == StopA->AnchorId && Hazard->BetweenB == StopB->AnchorId)
		|| (Hazard->BetweenA == StopB->AnchorId && Hazard->BetweenB == StopA->AnchorId);
	TestTrue(TEXT("Hazard sits between the two anchors"), bMatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerrySpawnDefaultAnchorTest, "GFGame.Spawn.DefaultAnchor", GFFerryTests::Flags)
bool FGFFerrySpawnDefaultAnchorTest::RunTest(const FString&)
{
	using namespace GFFerryTests;

	const AGFGameMode* GameMode = GetDefault<AGFGameMode>();
	const UGFTerritorySubsystem* Territory = GetDefault<UGFTerritorySubsystem>();
	if (!TestNotNull(TEXT("Game mode"), GameMode) || !TestNotNull(TEXT("Territory"), Territory))
	{
		return false;
	}
	TestEqual(TEXT("Config anchor is Rime"), GameMode->DefaultSpawnAnchor, Rime);
	TestEqual(TEXT("Fallback Z"), FGFPlayerStartRules::FallbackZ, 120.f);

	const FGFTerritoryFaction* RimeAnchor = nullptr;
	for (const FGFTerritoryFaction& Faction : Territory->FactionLayout)
	{
		if (Faction.Id == Rime)
		{
			RimeAnchor = &Faction;
			break;
		}
	}
	if (!TestNotNull(TEXT("Rime anchor"), RimeAnchor))
	{
		return false;
	}

	const FVector Fallback = FGFPlayerStartRules::FallbackLocation(GameMode->DefaultSpawnAnchor, Territory->FactionLayout);
	const FVector Expected(RimeAnchor->Center.X, RimeAnchor->Center.Y, RimeAnchor->Center.Z + 120.f);
	TestEqual(TEXT("No PlayerStart uses the Rime center"), Fallback, Expected);

	TArray<FGFMapPlayerStart> TaggedSecond;
	FGFMapPlayerStart Untagged;
	Untagged.Tag = NAME_None;
	Untagged.Location = FVector(10.f, 0.f, 0.f);
	FGFMapPlayerStart Tagged;
	Tagged.Tag = Rime;
	Tagged.Location = FVector(20.f, 0.f, 0.f);
	TaggedSecond.Add(Untagged);
	TaggedSecond.Add(Tagged);
	TestEqual(TEXT("Tagged start wins over an earlier untagged start"), FGFPlayerStartRules::ChooseStartIndex(TaggedSecond, Rime), 1);

	TArray<FGFMapPlayerStart> TaggedFirst;
	TaggedFirst.Add(Tagged);
	TaggedFirst.Add(Untagged);
	TestEqual(TEXT("Tagged start wins when it is first"), FGFPlayerStartRules::ChooseStartIndex(TaggedFirst, Rime), 0);

	TestEqual(TEXT("Unknown anchor"), FGFPlayerStartRules::FallbackLocation(FName(TEXT("NoSuchAnchor")), Territory->FactionLayout), FVector(0.f, 0.f, 120.f));
	TestEqual(TEXT("No starts"), FGFPlayerStartRules::ChooseStartIndex({}, Rime), INDEX_NONE);
	return true;
}

#endif
