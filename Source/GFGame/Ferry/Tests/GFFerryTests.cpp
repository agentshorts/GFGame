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
#include "GFInventoryComponent.h"
#include "GFTamingComponent.h"
#include "GFGameMode.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFLandTypes.h"
#include "GFProtectedActor.h"
#include "GFDataTables.h"
#include "GFTerritorySubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
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
	static const FName DirtId(TEXT("Dirt"));
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
	TestTrue(TEXT("Dirt is a fare block"), Tuning.FareItems.Contains(DirtId));

	const FGuid TxId(0xF3220001, 0xF3220002, 0xF3220003, 0xF3220004);
	FGFMapLedgerAccounts Accounts;
	FGFTransferLedger Ledger;
	Give(Accounts, RiderKey, DirtId, Tuning.FareCount);

	const FGFFerryResult Paid = FGFFerryRules::ApplyFare(Accounts, Ledger, TxId, RiderKey, true, false, Home, Home->Location, Tuning);
	TestTrue(TEXT("Fare applied"), Paid.bOk);
	TestEqual(TEXT("Fare code"), Paid.Code, FGFFerryCodes::Ok);
	TestEqual(TEXT("Burns FareCount once"), CountOf(Accounts, RiderKey, DirtId), 0);
	TestEqual(TEXT("Ledger burned the fare"), int32(Ledger.GetBurned(DirtId)), Tuning.FareCount);

	const FGFFerryResult Replay = FGFFerryRules::ApplyFare(Accounts, Ledger, TxId, RiderKey, true, false, Home, Home->Location, Tuning);
	TestTrue(TEXT("Replay is ok"), Replay.bOk);
	TestEqual(TEXT("Replay does not charge again"), CountOf(Accounts, RiderKey, DirtId), 0);
	TestEqual(TEXT("Burned total stays FareCount"), int32(Ledger.GetBurned(DirtId)), Tuning.FareCount);

	FGFMapLedgerAccounts Short;
	FGFTransferLedger ShortLedger;
	const int32 TooFew = FMath::Max(0, Tuning.FareCount - 1);
	Give(Short, RiderKey, DirtId, TooFew);
	const FGuid ShortTx(0xF3220011, 0xF3220012, 0xF3220013, 0xF3220014);
	const FGFFerryResult Denied = FGFFerryRules::ApplyFare(Short, ShortLedger, ShortTx, RiderKey, true, false, Home, Home->Location, Tuning);
	TestFalse(TEXT("Too few is refused"), Denied.bOk);
	TestEqual(TEXT("CantAfford"), Denied.Code, FGFFerryCodes::CantAfford);
	TestEqual(TEXT("Short inventory unchanged"), CountOf(Short, RiderKey, DirtId), TooFew);
	TestEqual(TEXT("Short ledger burned nothing"), int32(ShortLedger.GetBurned(DirtId)), 0);

	FGFMapLedgerAccounts PassHolder;
	FGFTransferLedger PassLedger;
	Give(PassHolder, RiderKey, DirtId, Tuning.FareCount);
	Give(PassHolder, RiderKey, Tuning.PassItem, 1);
	const FGuid OutTx(0xF3220021, 0xF3220022, 0xF3220023, 0xF3220024);
	const FGuid BackTx(0xF3220031, 0xF3220032, 0xF3220033, 0xF3220034);
	const FGFFerryResult Out = FGFFerryRules::ApplyFare(PassHolder, PassLedger, OutTx, RiderKey, true, false, Home, Home->Location, Tuning);
	const FGFFerryResult Back = FGFFerryRules::ApplyFare(PassHolder, PassLedger, BackTx, RiderKey, true, false, Far, Far->Location, Tuning);
	TestTrue(TEXT("Pass outbound"), Out.bOk);
	TestTrue(TEXT("Pass return"), Back.bOk);
	TestEqual(TEXT("Quote is 0 with a pass"), FGFFerryRules::Quote(PassHolder.Accounts[RiderKey], Tuning), 0);
	TestEqual(TEXT("Pass holder keeps the fare item"), CountOf(PassHolder, RiderKey, DirtId), Tuning.FareCount);
	TestEqual(TEXT("Pass holder burned no fare"), int32(PassLedger.GetBurned(DirtId)), 0);
	TestEqual(TEXT("Pass still held"), CountOf(PassHolder, RiderKey, Tuning.PassItem), 1);

	const FVector Away = Home->Location + FVector(Tuning.BoardRadius + 50.f, 0.f, 0.f);
	FGFMapLedgerAccounts FarAccounts;
	FGFTransferLedger FarLedger;
	Give(FarAccounts, RiderKey, DirtId, Tuning.FareCount);
	const FGuid AwayTx(0xF3220041, 0xF3220042, 0xF3220043, 0xF3220044);
	const FGFFerryResult NotThere = FGFFerryRules::ApplyFare(FarAccounts, FarLedger, AwayTx, RiderKey, true, false, Home, Away, Tuning);
	TestFalse(TEXT("Outside the disc is refused"), NotThere.bOk);
	TestEqual(TEXT("NotAtStop"), NotThere.Code, FGFFerryCodes::NotAtStop);
	TestEqual(TEXT("Away inventory unchanged"), CountOf(FarAccounts, RiderKey, DirtId), Tuning.FareCount);

	const FVector Peak = FGFFerryRules::FlightPosition(Home->Location, Far->Location, 0.5f, Tuning.CruiseAltitude);
	const FVector Mid = (Home->Location + Far->Location) * 0.5f;
	const float MidZ = Mid.Z + Tuning.CruiseAltitude;
	TestEqual(TEXT("Flight starts at the stop"), FGFFerryRules::FlightPosition(Home->Location, Far->Location, 0.f, Tuning.CruiseAltitude), Home->Location);
	TestEqual(TEXT("Flight ends at the far stop"), FGFFerryRules::FlightPosition(Home->Location, Far->Location, 1.f, Tuning.CruiseAltitude), Far->Location);
	TestTrue(TEXT("Arc peaks at cruise altitude"), FMath::IsNearlyEqual(Peak.Z, MidZ, 1.f));
	TestTrue(TEXT("Peak is above either stop"), Peak.Z > Home->Location.Z && Peak.Z > Far->Location.Z);
	const FVector Lateral(Peak.X - Mid.X, Peak.Y - Mid.Y, 0.f);
	TestTrue(TEXT("Peak leaves the chord"), Lateral.Size() > 100.f);
	TestTrue(TEXT("Peak is about 800 cm off the chord"), FMath::IsNearlyEqual(Lateral.Size(), FGFFerryArt::RouteBulgeCm * 0.5f, 1.f));
	TestTrue(TEXT("Bulge is toward +Y"), Lateral.Y > 0.f);
	const FVector ReturnPeak = FGFFerryRules::FlightPosition(Far->Location, Home->Location, 0.5f, Tuning.CruiseAltitude);
	const FVector ReturnLateral(ReturnPeak.X - Mid.X, ReturnPeak.Y - Mid.Y, 0.f);
	TestTrue(TEXT("Return bulge stays on +Y"), ReturnLateral.Y > 0.f);
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
	TestEqual(TEXT("Pass material is Fizzberry"), Tuning.PassMaterialItem, FName(TEXT("Fizzberry")));
	TestEqual(TEXT("Pass price is 12"), Tuning.PassPrice, 12);

	const FGuid RequestId(0xF3220101, 0xF3220102, 0xF3220103, 0xF3220104);
	FGFMapLedgerAccounts Accounts;
	FGFTransferLedger Ledger;
	Give(Accounts, RiderKey, Tuning.PassMaterialItem, Tuning.PassPrice);
	Give(Accounts, RiderKey, DirtId, Tuning.FareCount);

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
	TestEqual(TEXT("Fare item untouched after the pass"), CountOf(Accounts, RiderKey, DirtId), Tuning.FareCount);
	TestEqual(TEXT("No fare burned after the pass"), int32(Ledger.GetBurned(DirtId)), 0);

	TestTrue(TEXT("Pass conservation"), Conserves(Accounts, Ledger, RiderKey, Tuning.PassItem, 0));
	TestTrue(TEXT("Material conservation"), Conserves(Accounts, Ledger, RiderKey, Tuning.PassMaterialItem, Tuning.PassPrice));
	TestTrue(TEXT("Fare item conservation"), Conserves(Accounts, Ledger, RiderKey, DirtId, Tuning.FareCount));
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
	if (USkeletalMeshComponent* Bird = Parked->GetBird(); Bird && Bird->GetSkeletalMeshAsset())
	{
		// The ferry MI lives in Ripwing/Materials. A wrong path loads nothing and leaves the wild look on.
		UMaterialInterface* Mat = Bird->GetMaterial(0);
		TestTrue(TEXT("Ferry bird wears MI_Dino_Ripwing_Ferry"), Mat && Mat->GetName() == TEXT("MI_Dino_Ripwing_Ferry"));
	}
	TestNull(TEXT("No taming component"), Parked->FindComponentByClass<UGFTamingComponent>());
	TestEqual(TEXT("Ripwing placeholder"), Parked->BodyAssetKey, FName(TEXT("Dino.Ripwing")));
	TestEqual(TEXT("Perch anim"), Parked->AnimKey, FGFFerryArt::PerchAnim());
	const FVector Perch = FGFFerryArt::PerchWorld(Home->Location);
	TestTrue(TEXT("Parked at the perch point"), Parked->GetActorLocation().Equals(Perch, 1.f));
	TestTrue(TEXT("Park yaw is -90"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(Parked->GetActorRotation().Yaw), FGFFerryArt::ParkYawDegrees, 1.f));

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
	TestTrue(TEXT("Fare items"), PS->Inventory->AddItem(DirtId, Director->Tuning.FareCount));

	AGFCharacter* Pawn = World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), Home->Location, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("Rider pawn"), Pawn) || !TestNotNull(TEXT("Rider component"), Pawn->FerryRider.Get()))
	{
		return false;
	}
	Pawn->SetPlayerState(PS);

	const FGuid TripId(0xF3220101, 0xF3220102, 0xF3220103, 0xF3220104);
	const FGFFerryResult Trip = Director->RequestTrip(Pawn, Home->Id, TripId);
	TestTrue(TEXT("Trip accepted"), Trip.bOk);
	TestEqual(TEXT("Trip code"), Trip.Code, FGFFerryCodes::Ok);
	Director->Tick(0.05f);
	TestTrue(TEXT("Rider is in flight"), Pawn->FerryRider->IsInFlight());
	TestTrue(TEXT("Rider is faction protected"), FGFProtectedActor::Is(Pawn));
	TestFalse(TEXT("Rider cannot be damaged in flight"), Pawn->CanBeDamaged());
	Hunter->SetAttackTarget(Pawn);
	TestNull(TEXT("Dino cannot target a rider in flight"), Hunter->GetAttackTarget());

	AGFFerryDino* Transit = nullptr;
	for (TActorIterator<AGFFerryDino> TransitIt(World); TransitIt; ++TransitIt)
	{
		if (TransitIt->bTransit)
		{
			Transit = *TransitIt;
			break;
		}
	}
	if (!TestNotNull(TEXT("Transit copy"), Transit))
	{
		return false;
	}
	TestEqual(TEXT("Takeoff anim"), Transit->AnimKey, FGFFerryArt::TakeoffAnim());
	TestTrue(TEXT("Transit still on the perch at frame 0"), Transit->GetActorLocation().Equals(Perch, 1.f));
	TestTrue(TEXT("Rider parent is the craft"), Pawn->GetAttachParentActor() == Transit);
	TestTrue(TEXT("Rider sits at the seat"), Pawn->GetRootComponent() && Pawn->GetRootComponent()->GetRelativeLocation().Equals(FGFFerryArt::SeatLocal(), 1.f));
	TestEqual(TEXT("Seat emote"), Pawn->FerryRider->GetSeatAnim(), FGFFerryArt::SeatAnim());

	Director->Tick(2.f);
	TestEqual(TEXT("Carry anim"), Transit->AnimKey, FGFFerryArt::CarryAnim());
	TestTrue(TEXT("Still flying during the carry"), Pawn->FerryRider->IsInFlight());

	Director->Tick(Director->Tuning.FlightSeconds);
	TestFalse(TEXT("Flight ended"), Pawn->FerryRider->IsInFlight());
	TestFalse(TEXT("Protection tag cleared"), FGFProtectedActor::Is(Pawn));
	TestTrue(TEXT("Damage restored"), Pawn->CanBeDamaged());
	TestEqual(TEXT("Stop ferry stayed parked"), Parked->GetActorLocation(), ParkedAt);
	TestFalse(TEXT("Parked ferry is not the transit copy"), Parked->bTransit);
	TestEqual(TEXT("Fare was burned"), PS->Inventory->GetCount(DirtId), 0);

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

	// Four Rime members cover the forest box. One member leaves its center wild.
	TestTrue(TEXT("Forest check members"), Map.SetMembers(Rime, 1) && Map.SetMembers(Cinder, 1));
	const FGFTerritoryHazard* Forest = Map.Hazards.FindByPredicate([](const FGFTerritoryHazard& Hazard) { return Hazard.Id == DeadlyForest; });
	if (!TestNotNull(TEXT("DeadlyForest"), Forest))
	{
		return false;
	}
	TestEqual(TEXT("Forest center"), HazardAt(Forest->Center), DeadlyForest);
	TestFalse(TEXT("Forest is not a lava spawn"), FGFFerryRouteSpawnRules::IsLegalSpawnPoint(Forest->Center, LavaBridge, HazardAt));

	TArray<FGFFerryRoutePerson> Nobody;
	TestFalse(TEXT("Nobody near"), FGFFerryRouteSpawnRules::IsRouteActive(Box, Spawn.ActivationMargin, Nobody));
	TestFalse(TEXT("Inactive does not spawn"), FGFFerryRouteSpawnRules::CanSpawn(0, Spawn.MaxAlivePerBox, false));
	FGFFerryRoutePerson Flying;
	Flying.bFlying = true;
	Flying.Location = Box.Center;
	Flying.Key = TEXT("Air");
	TArray<FGFFerryRoutePerson> OnlyFlying;
	OnlyFlying.Add(Flying);
	TestFalse(TEXT("A flying rider does not activate the route"), FGFFerryRouteSpawnRules::IsRouteActive(Box, Spawn.ActivationMargin, OnlyFlying));

	TestFalse(TEXT("Cap blocks another spawn"), FGFFerryRouteSpawnRules::CanSpawn(Spawn.MaxAlivePerBox, Spawn.MaxAlivePerBox, true));
	TestTrue(TEXT("Under the cap can spawn"), FGFFerryRouteSpawnRules::CanSpawn(Spawn.MaxAlivePerBox - 1, Spawn.MaxAlivePerBox, true));

	const FVector Left = Box.Center + FVector(-(Box.HalfExtent.X + Spawn.LeashMargin + 10.f), 0.f, 0.f);
	TestTrue(TEXT("Past the leash"), FGFFerryRouteSpawnRules::ShouldDespawn(Box, Left, Spawn.LeashMargin, 0.f, Spawn.IdleDespawnSeconds, true));
	TestTrue(TEXT("Idle with nobody near"), FGFFerryRouteSpawnRules::ShouldDespawn(Box, Box.Center, Spawn.LeashMargin, Spawn.IdleDespawnSeconds, Spawn.IdleDespawnSeconds, false));
	TestFalse(TEXT("Idle while someone is near"), FGFFerryRouteSpawnRules::ShouldDespawn(Box, Box.Center, Spawn.LeashMargin, Spawn.IdleDespawnSeconds, Spawn.IdleDespawnSeconds, true));

	const int32 Level = FGFFerryRouteSpawnRules::MobLevelForParty(1, Spawn.LevelBonus);
	TestEqual(TEXT("Empty party is level 1 plus the bonus"), Level, 6);
	const float Scaled = FGFFerryRouteSpawnRules::ScaledAttackDamage(10.f, Level, Spawn.MobDamagePerLevel);
	const float Expected = 10.f * (1.f + Spawn.MobDamagePerLevel * float(Level - 1));
	TestEqual(TEXT("Level scale"), Scaled, Expected);
	TestEqual(TEXT("A level 8 party is 13"), FGFFerryRouteSpawnRules::MobLevelForParty(8, Spawn.LevelBonus), 13);

	TestEqual(TEXT("Three lava boxes"), Director->MobBoxes.Num(), 3);
	TestEqual(TEXT("Spawn interval"), Spawn.SpawnIntervalSeconds, 6.f);
	TestEqual(TEXT("Cap per box"), Spawn.MaxAlivePerBox, 4);
	TestEqual(TEXT("Activation is 40 m"), Spawn.ActivationMargin, 4000.f);
	TestEqual(TEXT("Leash is 8 m"), Spawn.LeashMargin, 800.f);
	TestEqual(TEXT("Idle look is 60 m"), Spawn.DespawnRange, 6000.f);
	TestEqual(TEXT("Idle wait"), Spawn.IdleDespawnSeconds, 60.f);
	TestEqual(TEXT("Level bonus"), Spawn.LevelBonus, 5);

	struct FBoxExpect
	{
		const TCHAR* Id;
		const TCHAR* Species;
		float X;
	};
	const FBoxExpect Expect[] = {
		{ TEXT("LavaWest"), TEXT("Raptor"), 2400.f },
		{ TEXT("LavaMid"), TEXT("Scythepaw"), 3700.f },
		{ TEXT("LavaEast"), TEXT("Lanternmaw"), 5000.f },
	};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FGFFerryMobBox& Mob = Director->MobBoxes[Index];
		TestEqual(*FString::Printf(TEXT("Box %d id"), Index), Mob.Id, FName(Expect[Index].Id));
		TestEqual(*FString::Printf(TEXT("Box %d species"), Index), Mob.Species, FName(Expect[Index].Species));
		TestEqual(*FString::Printf(TEXT("Box %d hazard"), Index), Mob.HazardId, LavaBridge);
		TestTrue(*FString::Printf(TEXT("Box %d centre"), Index), Mob.Center.Equals(FVector(Expect[Index].X, 0.f, 0.f), 1.f));
		TestTrue(*FString::Printf(TEXT("Box %d half extent"), Index), Mob.HalfExtent.Equals(FVector2D(500.f, 300.f), 1.f));
		TestTrue(*FString::Printf(TEXT("Box %d sits in LavaBridge"), Index), FGFFerryRouteSpawnRules::ContainsHorizontal(Box, Mob.Center));
		const FVector Corner(Mob.Center.X + Mob.HalfExtent.X, Mob.Center.Y + Mob.HalfExtent.Y, 0.f);
		TestTrue(*FString::Printf(TEXT("Box %d stays inside LavaBridge"), Index), FGFFerryRouteSpawnRules::ContainsHorizontal(Box, Corner));
		TestNotEqual(*FString::Printf(TEXT("Box %d is not the forest"), Index), Mob.HazardId, DeadlyForest);
	}

	TestTrue(TEXT("One member on each lava neighbour"), Map.SetMembers(Rime, 1) && Map.SetMembers(Cinder, 1));
	for (const FGFFerryMobBox& Mob : Director->MobBoxes)
	{
		const FGFTerritoryHazard Bounds = FGFFerryRules::BoxBounds(Mob);
		FRandomStream Rng(17);
		FVector Point;
		TestTrue(*FString::Printf(TEXT("%s can spawn while wild"), *Mob.Id.ToString()), FGFFerryRouteSpawnRules::PickSpawnPoint(Bounds, LavaBridge, Rng, HazardAt, Point));
		TestEqual(*FString::Printf(TEXT("%s spawn is LavaBridge"), *Mob.Id.ToString()), HazardAt(Point), LavaBridge);
	}

	TestTrue(TEXT("Four members cover the end boxes"), Map.SetMembers(Rime, 4) && Map.SetMembers(Cinder, 4));
	for (const FGFFerryMobBox& Mob : Director->MobBoxes)
	{
		const bool bEnd = Mob.Id == FName(TEXT("LavaWest")) || Mob.Id == FName(TEXT("LavaEast"));
		if (!bEnd)
		{
			continue;
		}
		const FGFTerritoryHazard Bounds = FGFFerryRules::BoxBounds(Mob);
		bool bAny = false;
		for (int32 Seed = 1; Seed <= 8; ++Seed)
		{
			FRandomStream Rng(Seed);
			FVector Point;
			bAny |= FGFFerryRouteSpawnRules::PickSpawnPoint(Bounds, LavaBridge, Rng, HazardAt, Point);
		}
		TestFalse(*FString::Printf(TEXT("%s is owned land"), *Mob.Id.ToString()), bAny);
		TestFalse(*FString::Printf(TEXT("%s centre is not a spawn"), *Mob.Id.ToString()), FGFFerryRouteSpawnRules::IsLegalSpawnPoint(Mob.Center, LavaBridge, HazardAt));
	}

	const FGFTerritoryHazard West = FGFFerryRules::BoxBounds(Director->MobBoxes[0]);
	FGFFerryRoutePerson Walker;
	Walker.Key = TEXT("Walk");
	Walker.Location = West.Center;
	Walker.PartyLevel = 8;
	TArray<FGFFerryRoutePerson> Walking;
	Walking.Add(Walker);
	TestTrue(TEXT("A walker on the box wakes it"), FGFFerryRouteSpawnRules::IsRouteActive(West, Spawn.ActivationMargin, Walking));
	TestEqual(TEXT("That party level is 8"), FGFFerryRouteSpawnRules::RoutePartyLevel(West, Spawn.ActivationMargin, Walking), 8);

	FGFFerryRoutePerson Beyond;
	Beyond.Key = TEXT("Far");
	Beyond.Location = West.Center + FVector(West.HalfExtent.X + Spawn.ActivationMargin + 50.f, 0.f, 0.f);
	TArray<FGFFerryRoutePerson> TooFar;
	TooFar.Add(Beyond);
	TestFalse(TEXT("Past 40 m does not wake the box"), FGFFerryRouteSpawnRules::IsRouteActive(West, Spawn.ActivationMargin, TooFar));

	FGFFerryRoutePerson IdleNear;
	IdleNear.Key = TEXT("Idle");
	IdleNear.Location = West.Center + FVector(West.HalfExtent.X + Spawn.DespawnRange - 50.f, 0.f, 0.f);
	TArray<FGFFerryRoutePerson> WithinSixty;
	WithinSixty.Add(IdleNear);
	TestTrue(TEXT("Within 60 m counts as near"), FGFFerryRouteSpawnRules::IsRouteActive(West, Spawn.DespawnRange, WithinSixty));
	TestFalse(TEXT("Idle while that player is near"), FGFFerryRouteSpawnRules::ShouldDespawn(West, West.Center, Spawn.LeashMargin, Spawn.IdleDespawnSeconds, Spawn.IdleDespawnSeconds, true));

	TArray<FGFFerryRoutePerson> AirOnly;
	AirOnly.Add(Flying);
	TestFalse(TEXT("A flying player does not wake a box"), FGFFerryRouteSpawnRules::IsRouteActive(West, Spawn.ActivationMargin, AirOnly));
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
		const FGFTerritoryFaction* Anchor = Map.FindFaction(Stop.AnchorId);
		if (!TestNotNull(*FString::Printf(TEXT("%s anchor is a faction"), *Stop.Id.ToString()), Anchor))
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s members"), *Stop.AnchorId.ToString()), Map.SetMembers(Stop.AnchorId, 1));
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
	TestEqual(TEXT("Anchor fallback is the Rime center plus FallbackZ"), Fallback, Expected);
	TestEqual(TEXT("Shipped Rime empty-map spawn is the origin plus FallbackZ"), Fallback, FVector(0.f, 0.f, 120.f));
	const FTransform Perch = FGFPlayerStartRules::EmptyMapStart();
	TestEqual(TEXT("Ferry perch stays at (0, -300, 100)"), Perch.GetLocation(), FVector(0.f, -300.f, 100.f));
	TestTrue(TEXT("Ferry perch yaw is 36"), FMath::IsNearlyEqual(Perch.Rotator().Yaw, 36.f, 0.1f));
	TestFalse(TEXT("Empty-map spawn is not the ferry perch"), Fallback.Equals(Perch.GetLocation()));

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

namespace GFFerryTests
{
	struct FClosedAccounts : public FGFMapLedgerAccounts
	{
		virtual bool ReadAccount(const FString&, FGFInventory&) const override
		{
			return false;
		}
	};

	struct FFailThirdRead : public FGFMapLedgerAccounts
	{
		mutable int32 Reads = 0;
		virtual bool ReadAccount(const FString& Account, FGFInventory& Out) const override
		{
			++Reads;
			if (Reads == 3)
			{
				return false;
			}
			return FGFMapLedgerAccounts::ReadAccount(Account, Out);
		}
	};

	struct FTripWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		AGFFerryDirector* Director = nullptr;
		AGFPlayerState* PS = nullptr;
		AGFCharacter* Pawn = nullptr;

		bool Open(const TCHAR* Name, int32 FareMultiplier)
		{
			if (!GEngine)
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World)
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
			GS = World->SpawnActor<AGFGameState>();
			if (GS)
			{
				World->SetGameState(GS);
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Director = World->SpawnActor<AGFFerryDirector>(AGFFerryDirector::StaticClass(), FTransform::Identity, Params);
			PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
			if (!GS || !Director || !PS || !PS->Inventory || Director->Stops.Num() == 0)
			{
				return false;
			}
			PS->PlayerKey = RiderKey;
			PS->bCharacterReady = true;
			PS->SetPlayerId(77);
			if (GS->PlayerArray.Find(PS) == INDEX_NONE)
			{
				GS->AddPlayerState(PS);
			}
			const FGFFerryStop* Home = HomeStop(*Director);
			if (!Home)
			{
				return false;
			}
			Pawn = World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), Home->Location, FRotator::ZeroRotator, Params);
			if (!Pawn || !Pawn->FerryRider)
			{
				return false;
			}
			Pawn->SetPlayerState(PS);
			if (FareMultiplier > 0)
			{
				PS->Inventory->AddItem(DirtId, Director->Tuning.FareCount * FareMultiplier);
			}
			return true;
		}

		~FTripWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryDuplicateChargeTest, "GFGame.Ferry.DuplicateCharge", GFFerryTests::Flags)
bool FGFFerryDuplicateChargeTest::RunTest(const FString&)
{
	using namespace GFFerryTests;
	FTripWorld Trip;
	if (!TestTrue(TEXT("World"), Trip.Open(TEXT("GFFerryDuplicate"), 2)))
	{
		return false;
	}
	const FGFFerryStop* Home = HomeStop(*Trip.Director);
	if (!TestNotNull(TEXT("Home"), Home))
	{
		return false;
	}
	const int32 Start = Trip.PS->Inventory->GetCount(DirtId);
	const FGuid Id(0xF3220201, 0xF3220202, 0xF3220203, 0xF3220204);
	const FGFFerryResult First = Trip.Director->RequestTrip(Trip.Pawn, Home->Id, Id);
	TestTrue(TEXT("First trip"), First.bOk);
	Trip.Director->Tick(Trip.Director->Tuning.FlightSeconds);
	TestFalse(TEXT("First flight ended"), Trip.Pawn->FerryRider->IsInFlight());
	TestEqual(TEXT("Charged once"), Trip.PS->Inventory->GetCount(DirtId), Start - Trip.Director->Tuning.FareCount);

	const FGFFerryResult Second = Trip.Director->RequestTrip(Trip.Pawn, Home->Id, Id);
	TestTrue(TEXT("Duplicate is ok"), Second.bOk);
	TestFalse(TEXT("Duplicate does not fly"), Trip.Pawn->FerryRider->IsInFlight());
	TestEqual(TEXT("Duplicate does not charge"), Trip.PS->Inventory->GetCount(DirtId), Start - Trip.Director->Tuning.FareCount);
	TestEqual(TEXT("Ledger burned one fare"), int32(Trip.GS->Ledger.GetBurned(DirtId)), Trip.Director->Tuning.FareCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryFareRefundTest, "GFGame.Ferry.FareRefund", GFFerryTests::Flags)
bool FGFFerryFareRefundTest::RunTest(const FString&)
{
	using namespace GFFerryTests;
	FTripWorld Trip;
	if (!TestTrue(TEXT("World"), Trip.Open(TEXT("GFFerryRefund"), 1)))
	{
		return false;
	}
	const FGFFerryStop* Home = HomeStop(*Trip.Director);
	if (!TestNotNull(TEXT("Home"), Home))
	{
		return false;
	}
	const int32 Start = Trip.PS->Inventory->GetCount(DirtId);
	const FGuid Id(0xF3220301, 0xF3220302, 0xF3220303, 0xF3220304);
	Trip.Director->bFailNextFlight = true;
	const FGFFerryResult Failed = Trip.Director->RequestTrip(Trip.Pawn, Home->Id, Id);
	TestFalse(TEXT("Failed start"), Failed.bOk);
	TestEqual(TEXT("FlightFailed"), Failed.Code, FGFFerryCodes::FlightFailed);
	TestFalse(TEXT("Not in flight"), Trip.Pawn->FerryRider->IsInFlight());
	TestEqual(TEXT("Fare restored"), Trip.PS->Inventory->GetCount(DirtId), Start);
	TestEqual(TEXT("Fare was burned"), int32(Trip.GS->Ledger.GetBurned(DirtId)), Trip.Director->Tuning.FareCount);
	TestEqual(TEXT("Fare was minted back"), int32(Trip.GS->Ledger.GetMinted(DirtId)), Trip.Director->Tuning.FareCount);

	const FGFFerryResult Again = Trip.Director->RequestTrip(Trip.Pawn, Home->Id, Id);
	TestFalse(TEXT("Same id stays failed"), Again.bOk);
	TestEqual(TEXT("Same id is FlightFailed"), Again.Code, FGFFerryCodes::FlightFailed);
	TestFalse(TEXT("Same id does not fly"), Trip.Pawn->FerryRider->IsInFlight());
	TestEqual(TEXT("Same id does not charge"), Trip.PS->Inventory->GetCount(DirtId), Start);
	TestEqual(TEXT("Refund is not minted twice"), int32(Trip.GS->Ledger.GetMinted(DirtId)), Trip.Director->Tuning.FareCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryPassRollbackTest, "GFGame.Ferry.PassRollback", GFFerryTests::Flags)
bool FGFFerryPassRollbackTest::RunTest(const FString&)
{
	using namespace GFFerryTests;
	const AGFFerryDirector* Director = nullptr;
	if (!TestTrue(TEXT("Shipped ferry config"), LoadFerry(Director)))
	{
		return false;
	}
	const FGFFerryTuning& Tuning = Director->Tuning;
	const FGFFerryStop* Home = HomeStop(*Director);
	if (!TestNotNull(TEXT("Home"), Home))
	{
		return false;
	}
	FFailThirdRead Accounts;
	FGFTransferLedger Ledger;
	Give(Accounts, RiderKey, Tuning.PassMaterialItem, Tuning.PassPrice);
	const FGuid Id(0xF3220401, 0xF3220402, 0xF3220403, 0xF3220404);
	const FGFFerryResult Failed = FGFFerryRules::ApplyPass(Accounts, Ledger, Id, RiderKey, true, false, Home, Home->Location, Tuning);
	TestFalse(TEXT("Mint failure"), Failed.bOk);
	TestEqual(TEXT("CantAfford"), Failed.Code, FGFFerryCodes::CantAfford);
	TestEqual(TEXT("Materials restored"), CountOf(Accounts, RiderKey, Tuning.PassMaterialItem), Tuning.PassPrice);
	TestEqual(TEXT("No pass"), CountOf(Accounts, RiderKey, Tuning.PassItem), 0);

	const FGFFerryResult Again = FGFFerryRules::ApplyPass(Accounts, Ledger, Id, RiderKey, true, false, Home, Home->Location, Tuning);
	TestFalse(TEXT("Rolled-back id does not mint"), Again.bOk);
	TestEqual(TEXT("Still no pass"), CountOf(Accounts, RiderKey, Tuning.PassItem), 0);
	TestEqual(TEXT("Materials stay"), CountOf(Accounts, RiderKey, Tuning.PassMaterialItem), Tuning.PassPrice);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryPassBoundTest, "GFGame.Ferry.PassBound", GFFerryTests::Flags)
bool FGFFerryPassBoundTest::RunTest(const FString&)
{
	using namespace GFFerryTests;
	FTripWorld Trip;
	if (!TestTrue(TEXT("World"), Trip.Open(TEXT("GFFerryPassBound"), 0)))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* Other = Trip.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Other"), Other) || !TestNotNull(TEXT("Other inventory"), Other->Inventory.Get()))
	{
		return false;
	}
	Other->PlayerKey = TEXT("Other");
	Other->bCharacterReady = true;
	Other->SetPlayerId(78);
	if (Trip.GS->PlayerArray.Find(Other) == INDEX_NONE)
	{
		Trip.GS->AddPlayerState(Other);
	}
	const FName PassId(TEXT("FerryPass"));
	TestTrue(TEXT("Has a pass"), Trip.PS->Inventory->AddItem(PassId, 1));
	TestTrue(TEXT("Has dirt"), Trip.PS->Inventory->AddItem(DirtId, 3));

	const EGFTxResult PassMove = Trip.GS->ServerTransfer(FGuid(0xF3220501, 0xF3220502, 0xF3220503, 0xF3220504), RiderKey, Other->PlayerKey, { FGFItemStack(PassId, 1) }, NAME_None);
	TestTrue(TEXT("Pass transfer is account-bound"), PassMove == EGFTxResult::AccountBound);
	TestEqual(TEXT("Pass stayed"), Trip.PS->Inventory->GetCount(PassId), 1);

	const EGFTxResult DirtMove = Trip.GS->ServerTransfer(FGuid(0xF3220505, 0xF3220506, 0xF3220507, 0xF3220508), RiderKey, Other->PlayerKey, { FGFItemStack(DirtId, 3) }, NAME_None);
	TestTrue(TEXT("Dirt still transfers"), DirtMove == EGFTxResult::Applied);
	TestEqual(TEXT("Dirt left"), Trip.PS->Inventory->GetCount(DirtId), 0);

	EGFLandResult Mail = EGFLandResult::Ok;
	Trip.GS->ServerSendLetter(EGFLetterKind::Gift, RiderKey, Other->PlayerKey, TEXT("pass"), { FGFItemStack(PassId, 1) }, NAME_None, Mail);
	TestTrue(TEXT("Pass mail is not allowed"), Mail == EGFLandResult::NotAllowed);
	TestEqual(TEXT("Pass still held after mail"), Trip.PS->Inventory->GetCount(PassId), 1);

	const EGFTxResult Stake = Trip.GS->ServerDeposit(
		FGuid(0xF3220509, 0xF322050A, 0xF322050B, 0xF322050C),
		FGuid(0xF322050D, 0xF322050E, 0xF322050F, 0xF3220510),
		FName(TEXT("Wager")),
		RiderKey,
		{ FGFItemStack(PassId, 1) },
		NAME_None);
	TestTrue(TEXT("Pass cannot be staked"), Stake == EGFTxResult::AccountBound);
	TestEqual(TEXT("Pass still held after a stake"), Trip.PS->Inventory->GetCount(PassId), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFerryReceiptTest, "GFGame.Ferry.Receipt", GFFerryTests::Flags)
bool FGFFerryReceiptTest::RunTest(const FString&)
{
	using namespace GFFerryTests;

	const AGFFerryDirector* Director = nullptr;
	if (!TestTrue(TEXT("Shipped ferry config"), LoadFerry(Director)))
	{
		return false;
	}
	const FGFFerryTuning& Tuning = Director->Tuning;
	const FGFFerryStop* Home = HomeStop(*Director);
	if (!TestNotNull(TEXT("Home stop"), Home))
	{
		return false;
	}

	TestEqual(TEXT("Pass material"), Tuning.PassMaterialItem, FName(TEXT("Fizzberry")));
	TestEqual(TEXT("Pass price"), Tuning.PassPrice, 12);
	TestEqual(TEXT("Pass item"), Tuning.PassItem, FName(TEXT("FerryPass")));
	TestTrue(TEXT("Flight is 13.4 s"), FMath::IsNearlyEqual(Tuning.FlightSeconds, 13.4f, 0.05f));

	const float Takeoff = FGFFerryArt::FrameSeconds(FGFFerryArt::TakeoffFrames);
	const float Land = FGFFerryArt::FrameSeconds(FGFFerryArt::LandFrames);
	TestTrue(TEXT("Takeoff clip"), FMath::IsNearlyEqual(Takeoff, 40.f / 30.f, 0.01f));
	TestTrue(TEXT("Land clip"), FMath::IsNearlyEqual(Land, 48.f / 30.f, 0.01f));
	const float Carry = FGFFerryArt::CarrySeconds(13.4f);
	TestTrue(TEXT("Carry fills the rest"), FMath::IsNearlyEqual(Carry, 13.4f - Takeoff - Land, 0.01f));
	TestTrue(TEXT("Carry is about 10.5 s"), FMath::IsNearlyEqual(Carry, 10.5f, 0.15f));
	TestEqual(TEXT("Phase at 0 is takeoff"), FGFFerryArt::FlightPhase(0.f, 13.4f), FGFFerryArt::TakeoffAnim());
	TestEqual(TEXT("Phase at 2 s is carry"), FGFFerryArt::FlightPhase(2.f, 13.4f), FGFFerryArt::CarryAnim());
	TestEqual(TEXT("Phase at the end is land"), FGFFerryArt::FlightPhase(13.4f - 0.05f, 13.4f), FGFFerryArt::LandAnim());

	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	TArray<FName> Basic;
	Items->ForeachRow<FGFItemRow>(TEXT("FerryReceipt"), [&Basic](const FName& Id, const FGFItemRow& Row)
	{
		if (FGFFerryRules::IsBasicBlock(Row))
		{
			Basic.Add(Id);
		}
	});
	auto ByName = [](const FName& A, const FName& B) { return A.Compare(B) < 0; };
	Basic.Sort(ByName);
	TArray<FName> Config = Tuning.FareItems;
	Config.Sort(ByName);
	TestEqual(TEXT("Fare list matches the basic blocks"), Config.Num(), Basic.Num());
	for (int32 Index = 0; Index < FMath::Min(Config.Num(), Basic.Num()); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Fare item %d"), Index), Config[Index], Basic[Index]);
	}
	const FGFItemRow* Stone = FGFEconomy::FindItem(Items, TEXT("Stone"));
	const FGFItemRow* Fabric = FGFEconomy::FindItem(Items, TEXT("Fabric"));
	if (TestNotNull(TEXT("Stone"), Stone) && TestNotNull(TEXT("Fabric"), Fabric))
	{
		TestFalse(TEXT("Stone is not a basic block"), FGFFerryRules::IsBasicBlock(*Stone));
		TestFalse(TEXT("Fabric is not a basic block"), FGFFerryRules::IsBasicBlock(*Fabric));
	}
	TestNull(TEXT("FerryTame is retired"), FGFEconomy::FindItem(Items, TEXT("FerryTame")));
	const FGFItemRow* Pass = FGFEconomy::FindItem(Items, TEXT("FerryPass"));
	if (!TestNotNull(TEXT("FerryPass row"), Pass))
	{
		return false;
	}
	TestEqual(TEXT("Ferry Pass display"), Pass->DisplayName.ToString(), FString(TEXT("Ferry Pass")));
	TestTrue(TEXT("FerryPass is a material"), Pass->Category == EGFItemCategory::Material);
	TestEqual(TEXT("FerryPass stack"), Pass->StackMax, 1);
	TestEqual(TEXT("FerryPass tier"), Pass->Tier, 4);
	TestEqual(TEXT("FerryPass value"), Pass->Value, 20);
	TestTrue(TEXT("FerryPass rarity"), Pass->Rarity == EGFRarity::Rare);
	TestTrue(TEXT("FerryPass material category"), Pass->MaterialCategory == EGFMaterialCategory::Rare);
	TestTrue(TEXT("FerryPass is account-bound"), Pass->bAccountBound);
	TestFalse(TEXT("FerryPass is not wagerable"), Pass->bWagerable);
	TestFalse(TEXT("FerryPass is not rentable"), Pass->bRentable);
	TestFalse(TEXT("FerryTame is not the pass material"), Tuning.PassMaterialItem == FName(TEXT("FerryTame")));

	FGFInventory MixBag;
	MixBag.Add(FName(TEXT("Glass")), 2);
	MixBag.Add(FName(TEXT("WoodPlank")), 1);
	const TArray<FGFItemStack> Paid = FGFFerryRules::ChargeFare(MixBag, Tuning);
	if (TestEqual(TEXT("Glass then WoodPlank"), Paid.Num(), 2))
	{
		TestEqual(TEXT("Glass is consumed first"), Paid[0].ItemId, FName(TEXT("Glass")));
		TestEqual(TEXT("Two glass"), Paid[0].Count, 2);
		TestEqual(TEXT("Then a plank"), Paid[1].ItemId, FName(TEXT("WoodPlank")));
		TestEqual(TEXT("One plank"), Paid[1].Count, 1);
	}

	FGFMapLedgerAccounts Short;
	FGFTransferLedger ShortLedger;
	Give(Short, RiderKey, FName(TEXT("Glass")), 1);
	Give(Short, RiderKey, FName(TEXT("WoodPlank")), 1);
	Give(Short, RiderKey, FName(TEXT("Stone")), 9);
	Give(Short, RiderKey, FName(TEXT("Fabric")), 9);
	Give(Short, RiderKey, FName(TEXT("Fizzberry")), 9);
	const FGuid ShortTx(0xF3220801, 0xF3220802, 0xF3220803, 0xF3220804);
	const FGFFerryResult Denied = FGFFerryRules::ApplyFare(Short, ShortLedger, ShortTx, RiderKey, true, false, Home, Home->Location, Tuning);
	TestFalse(TEXT("Two blocks do not pay"), Denied.bOk);
	TestEqual(TEXT("Short fare is CantAfford"), Denied.Code, FGFFerryCodes::CantAfford);
	TestEqual(TEXT("Glass kept"), CountOf(Short, RiderKey, FName(TEXT("Glass"))), 1);
	TestEqual(TEXT("Plank kept"), CountOf(Short, RiderKey, FName(TEXT("WoodPlank"))), 1);
	TestEqual(TEXT("Stone kept"), CountOf(Short, RiderKey, FName(TEXT("Stone"))), 9);
	TestEqual(TEXT("Fabric kept"), CountOf(Short, RiderKey, FName(TEXT("Fabric"))), 9);
	TestEqual(TEXT("Fizzberry kept"), CountOf(Short, RiderKey, FName(TEXT("Fizzberry"))), 9);
	TestEqual(TEXT("Nothing burned"), int32(ShortLedger.GetBurned(FName(TEXT("Glass")))), 0);

	FGFMapLedgerAccounts Mixed;
	FGFTransferLedger MixedLedger;
	Give(Mixed, RiderKey, FName(TEXT("Glass")), 2);
	Give(Mixed, RiderKey, FName(TEXT("WoodPlank")), 1);
	Give(Mixed, RiderKey, FName(TEXT("Stone")), 9);
	Give(Mixed, RiderKey, FName(TEXT("Fabric")), 9);
	Give(Mixed, RiderKey, FName(TEXT("Fizzberry")), 9);
	const FGuid MixTx(0xF3220811, 0xF3220812, 0xF3220813, 0xF3220814);
	const FGFFerryResult MixedFare = FGFFerryRules::ApplyFare(Mixed, MixedLedger, MixTx, RiderKey, true, false, Home, Home->Location, Tuning);
	TestTrue(TEXT("A mix of basic blocks pays"), MixedFare.bOk);
	TestEqual(TEXT("Glass spent"), CountOf(Mixed, RiderKey, FName(TEXT("Glass"))), 0);
	TestEqual(TEXT("Plank spent"), CountOf(Mixed, RiderKey, FName(TEXT("WoodPlank"))), 0);
	TestEqual(TEXT("Stone untouched"), CountOf(Mixed, RiderKey, FName(TEXT("Stone"))), 9);
	TestEqual(TEXT("Fabric untouched"), CountOf(Mixed, RiderKey, FName(TEXT("Fabric"))), 9);
	TestEqual(TEXT("Fizzberry untouched"), CountOf(Mixed, RiderKey, FName(TEXT("Fizzberry"))), 9);

	TestEqual(TEXT("Fare reason"), FGFFerryRules::FareReason(), FName(TEXT("Ferry.Fare")));
	TestEqual(TEXT("Fare refund reason"), FGFFerryRules::FareRefundReason(), FName(TEXT("Ferry.FareRefund")));
	TestEqual(TEXT("Pass burn reason"), FGFFerryRules::PassBurnReason(), FName(TEXT("Ferry.Pass")));
	TestEqual(TEXT("Pass mint reason"), FGFFerryRules::PassGrantReason(), FName(TEXT("Ferry.PassGrant")));
	TestEqual(TEXT("Pass rollback reason"), FGFFerryRules::PassRollbackReason(), FName(TEXT("Ferry.PassRollback")));

	const FGuid Request(0xF3220821, 0xF3220822, 0xF3220823, 0xF3220824);
	const FGuid FareId = FGFFerryRules::FareTxId(Request);
	TestEqual(TEXT("Fare tx A"), FareId.A, Request.A);
	TestEqual(TEXT("Fare tx B"), FareId.B, Request.B);
	TestEqual(TEXT("Fare tx C"), FareId.C, Request.C);
	TestEqual(TEXT("Fare tx D"), FareId.D, Request.D);
	const FGuid Refund = FGFFerryRules::FareRefundTx(Request);
	const FGuid Rollback = FGFFerryRules::PassRollbackTx(Request);
	TestEqual(TEXT("Refund salt"), Refund.D, Request.D ^ 0x46524652u);
	TestEqual(TEXT("Rollback salt"), Rollback.D, Request.D ^ 0x50535242u);
	TestEqual(TEXT("Refund keeps A"), Refund.A, Request.A);
	TestEqual(TEXT("Rollback keeps A"), Rollback.A, Request.A);
	TestFalse(TEXT("Invalid fare id stays invalid"), FGFFerryRules::FareTxId(FGuid()).IsValid());
	TestFalse(TEXT("Invalid refund id stays invalid"), FGFFerryRules::FareRefundTx(FGuid()).IsValid());

	FGFInventory Holding;
	Holding.Add(Tuning.PassItem, 1);
	const FGFFerryPassPlan Early = FGFFerryRules::PlanPass(false, false, true, true, false, Request, nullptr, Home->Location, Holding, Tuning);
	TestEqual(TEXT("Authority before the pass short circuit"), Early.Result.Code, FGFFerryCodes::NotAuthority);
	TestEqual(TEXT("Pass burn salt"), Early.BurnTx.D, Request.D ^ 0x50535331u);
	TestEqual(TEXT("Pass mint salt"), Early.MintTx.D, Request.D ^ 0x50535332u);
	const FGFFerryFarePlan Replay = FGFFerryRules::PlanFare(false, false, true, nullptr, Home->Location, Holding, Tuning);
	TestEqual(TEXT("Authority before the fare short circuit"), Replay.Result.Code, FGFFerryCodes::NotAuthority);
	const FGFFerryResult Missing = FGFFerryRules::CanBoard(true, false, nullptr, Home->Location, Holding, Tuning);
	TestEqual(TEXT("A stop that is not on a route"), Missing.Code, FGFFerryCodes::UnknownStop);

	FGFMapLedgerAccounts Readable;
	Give(Readable, RiderKey, Tuning.PassMaterialItem, Tuning.PassPrice);
	FGFTransferLedger ReadableLedger;
	const FGFFerryResult BadId = FGFFerryRules::ApplyPass(Readable, ReadableLedger, FGuid(), RiderKey, true, false, Home, Home->Location, Tuning);
	TestEqual(TEXT("Invalid request id"), BadId.Code, FGFFerryCodes::NotAuthority);
	TestEqual(TEXT("Invalid id spends nothing"), CountOf(Readable, RiderKey, Tuning.PassMaterialItem), Tuning.PassPrice);

	FClosedAccounts Closed;
	FGFTransferLedger ClosedLedger;
	const FGFFerryResult Unread = FGFFerryRules::ApplyPass(Closed, ClosedLedger, Request, RiderKey, true, false, Home, Home->Location, Tuning);
	TestEqual(TEXT("Unreadable account"), Unread.Code, FGFFerryCodes::NotAuthority);
	const FGFFerryResult UnreadFare = FGFFerryRules::ApplyFare(Closed, ClosedLedger, Request, RiderKey, true, false, Home, Home->Location, Tuning);
	TestEqual(TEXT("Unreadable fare account"), UnreadFare.Code, FGFFerryCodes::NotAuthority);
	return true;
}

#endif
