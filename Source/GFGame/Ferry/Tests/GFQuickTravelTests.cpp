// Quick travel: one ferry stand per safe zone. Walk a pad, pay a few blocks (or ride a pass), fly, arrive.
// The bird does not follow, and the faction deal still refuses damage. Rime stays the default spawn.

#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFDino.h"
#include "GFFerryDino.h"
#include "GFFerryDirector.h"
#include "GFFerryRiderComponent.h"
#include "GFFerryRules.h"
#include "GFFerrySpawn.h"
#include "GFFerryStand.h"
#include "GFGameMode.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "GFProtectedActor.h"
#include "GFTerritorySubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFQuickTravelTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Rime(TEXT("Rime"));
	static const FName Cinder(TEXT("Cinderwrights"));
	static const FName Verdant(TEXT("Verdant"));

	static bool LoadMap(FGFTerritoryMap& Map)
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
		return Map.FindFaction(Rime) && Map.FindFaction(Cinder) && Map.FindFaction(Verdant);
	}

	// The drop is exact on the ground plane. Walking movement then settles the capsule onto the floor, so Z gets a little slack.
	static bool LandedAt(const FVector& Actual, const FVector& Drop)
	{
		return FVector::Dist2D(Actual, Drop) <= 1.f && FMath::Abs(Actual.Z - Drop.Z) <= 15.f;
	}

	static const FGFFerryStop* StopByAnchor(const AGFFerryDirector& Director, FName Anchor)
	{
		for (const FGFFerryStop& Stop : Director.Stops)
		{
			if (Stop.AnchorId == Anchor)
			{
				return &Stop;
			}
		}
		return nullptr;
	}

	static void CheckDropMissesPads(FAutomationTestBase& Test, const AGFFerryDirector& Director, const FGFFerryStop& From, const FGFFerryStop& To)
	{
		const FVector Rider = From.Location + FVector(0.f, 0.f, 100.f);
		const FVector Drop = FGFFerryRules::DropPoint(From.Location, To.Location, Rider, Director.Tuning.BoardRadius);
		const FVector Local = Drop - To.Location;
		TArray<FName> DestIds;
		FGFFerryRules::CollectDestinations(Director.Stops, Director.Routes, To.Id, DestIds);
		for (int32 Index = 0; Index < DestIds.Num(); ++Index)
		{
			Test.TestFalse(
				*FString::Printf(TEXT("Drop onto %s misses the %s pad"), *To.Id.ToString(), *DestIds[Index].ToString()),
				FGFFerryStandLayout::ContainsHorizontal(FGFFerryStandLayout::DestLocal(Index), Local));
		}
		Test.TestFalse(
			*FString::Printf(TEXT("Drop onto %s misses the pass pad"), *To.Id.ToString()),
			FGFFerryStandLayout::ContainsHorizontal(FGFFerryStandLayout::PassLocal(), Local));
	}

	struct FTripWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		AGFFerryDirector* Director = nullptr;
		AGFPlayerState* PS = nullptr;
		AGFCharacter* Pawn = nullptr;

		bool Open(const TCHAR* Name)
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
			const FGFFerryStop* Home = Director ? StopByAnchor(*Director, Rime) : nullptr;
			if (!GS || !Director || !PS || !PS->Inventory || !Home)
			{
				return false;
			}
			PS->PlayerKey = TEXT("Rider");
			PS->bCharacterReady = true;
			PS->SetPlayerId(77);
			if (GS->PlayerArray.Find(PS) == INDEX_NONE)
			{
				GS->AddPlayerState(PS);
			}
			Pawn = World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), Home->Location, FRotator::ZeroRotator, Params);
			if (!Pawn || !Pawn->FerryRider)
			{
				return false;
			}
			Pawn->SetPlayerState(PS);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuickTravelLinksTest, "GFGame.Ferry.QuickTravel.Links", GFQuickTravelTests::Flags)
bool FGFQuickTravelLinksTest::RunTest(const FString&)
{
	using namespace GFQuickTravelTests;

	TestFalse(TEXT("The ferry does not follow"), FGFFerryFollow::WantsToFollow());
	const FVector Perch(12.f, 34.f, 56.f);
	TestEqual(TEXT("Parked goal is the perch"), FGFFerryFollow::ParkedGoal(Perch), Perch);

	const AGFGameMode* GameMode = GetDefault<AGFGameMode>();
	const UGFTerritorySubsystem* Territory = GetDefault<UGFTerritorySubsystem>();
	const AGFFerryDirector* Director = GetDefault<AGFFerryDirector>();
	if (!TestNotNull(TEXT("Game mode"), GameMode) || !TestNotNull(TEXT("Territory"), Territory) || !TestNotNull(TEXT("Ferry"), Director))
	{
		return false;
	}
	TestEqual(TEXT("Rime is the default spawn"), GameMode->DefaultSpawnAnchor, Rime);
	TestEqual(TEXT("Rime is the starter faction"), GameMode->StarterFaction, Rime);

	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped territory"), LoadMap(Map)))
	{
		return false;
	}
	TestEqual(TEXT("A stand for each safe zone"), Director->Stops.Num(), Map.Factions.Num());
	const int32 ZoneCount = Map.Factions.Num();
	TestEqual(TEXT("A route between each pair"), Director->Routes.Num(), ZoneCount * (ZoneCount - 1) / 2);
	TestTrue(TEXT("Fare is a few blocks"), Director->Tuning.FareCount > 0 && Director->Tuning.FareCount <= 5);
	TestEqual(TEXT("Fare item"), Director->Tuning.FareItem, FName(TEXT("Dirt")));
	TestEqual(TEXT("Fare count"), Director->Tuning.FareCount, 3);
	TestEqual(TEXT("Pass burns the taming material"), Director->Tuning.PassMaterialItem, FName(TEXT("FerryTame")));
	TestEqual(TEXT("Pass price"), Director->Tuning.PassPrice, 5);
	TestEqual(TEXT("Pass item"), Director->Tuning.PassItem, FName(TEXT("FerryPass")));

	if (!TestTrue(TEXT("First route still exists"), Director->Routes.Num() > 0))
	{
		return false;
	}
	const FGFFerryRoute& HomeRoute = Director->Routes[0];
	const FGFFerryStop* Home = FGFFerryRules::FindStop(Director->Stops, HomeRoute.StopA);
	const FGFFerryStop* FirstFar = FGFFerryRules::FindStop(Director->Stops, HomeRoute.StopB);
	if (!TestNotNull(TEXT("Home stop"), Home) || !TestNotNull(TEXT("First far stop"), FirstFar))
	{
		return false;
	}
	TestEqual(TEXT("A destination-less trip still leaves Rime"), Home->AnchorId, Rime);
	TestEqual(TEXT("...for Cinderwrights"), FirstFar->AnchorId, Cinder);
	TestEqual(TEXT("That route still crosses the lava bridge"), HomeRoute.LandRouteHazardId, FName(TEXT("LavaBridge")));

	const FVector Fallback = FGFPlayerStartRules::FallbackLocation(GameMode->DefaultSpawnAnchor, Territory->FactionLayout);
	for (const FGFTerritoryFaction& Faction : Map.Factions)
	{
		int32 Count = 0;
		const FGFFerryStop* Stand = nullptr;
		for (const FGFFerryStop& Stop : Director->Stops)
		{
			if (Stop.AnchorId == Faction.Id)
			{
				++Count;
				Stand = &Stop;
			}
		}
		TestEqual(*FString::Printf(TEXT("One stand for %s"), *Faction.Id.ToString()), Count, 1);
		if (!Stand)
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("%s stand is not the spawn"), *Faction.Id.ToString()), Stand->Location.Equals(Fallback, 1.f));
		TestTrue(*FString::Printf(TEXT("%s has land at one member"), *Faction.Id.ToString()), Map.SetMembers(Faction.Id, 1));
		const FGFTerritoryFaction* Anchor = Map.FindFaction(Faction.Id);
		if (!TestNotNull(*FString::Printf(TEXT("%s anchor"), *Faction.Id.ToString()), Anchor))
		{
			return false;
		}
		const float Radius = Map.RadiusOf(*Anchor);
		const float Dist = FVector::Dist2D(Stand->Location, Anchor->Center);
		TestTrue(*FString::Printf(TEXT("%s stand is inside the 1-member disc"), *Faction.Id.ToString()), Radius > 0.f && Dist <= Radius);

		TArray<FName> DestIds;
		FGFFerryRules::CollectDestinations(Director->Stops, Director->Routes, Stand->Id, DestIds);
		TestEqual(*FString::Printf(TEXT("%s reaches the other zones"), *Faction.Id.ToString()), DestIds.Num(), Director->Stops.Num() - 1);
		TestTrue(*FString::Printf(TEXT("%s pass pad is in the board disc"), *Faction.Id.ToString()), FGFFerryStandLayout::InsideBoard(FGFFerryStandLayout::PassLocal(), Director->Tuning.BoardRadius));
		for (int32 Index = 0; Index < DestIds.Num(); ++Index)
		{
			TestTrue(
				*FString::Printf(TEXT("%s pad %d is in the board disc"), *Faction.Id.ToString(), Index),
				FGFFerryStandLayout::InsideBoard(FGFFerryStandLayout::DestLocal(Index), Director->Tuning.BoardRadius));
			const FGFFerryStop* Dest = FGFFerryRules::FindStop(Director->Stops, DestIds[Index]);
			if (TestNotNull(*FString::Printf(TEXT("%s dest"), *DestIds[Index].ToString()), Dest))
			{
				TestNotNull(
					*FString::Printf(TEXT("Route %s to %s"), *Stand->Id.ToString(), *Dest->Id.ToString()),
					FGFFerryRules::FindRouteBetween(Director->Routes, Stand->Id, Dest->Id));
				CheckDropMissesPads(*this, *Director, *Stand, *Dest);
			}
		}
	}

	const FGFFerryRoute* Forest = FGFFerryRules::FindRouteBetween(Director->Routes, TEXT("RimeLanding"), TEXT("VerdantLanding"));
	if (TestNotNull(TEXT("Rime to Verdant"), Forest))
	{
		TestEqual(TEXT("That route crosses the forest"), Forest->LandRouteHazardId, FName(TEXT("DeadlyForest")));
	}
	const FGFFerryRoute* Open = FGFFerryRules::FindRouteBetween(Director->Routes, TEXT("VerdantLanding"), TEXT("CinderLanding"));
	if (TestNotNull(TEXT("Verdant to Cinderwrights"), Open))
	{
		TestTrue(TEXT("No hazard box on that flight"), Open->LandRouteHazardId.IsNone());
	}
	TestNull(TEXT("No route from a stop to itself"), FGFFerryRules::FindRouteBetween(Director->Routes, TEXT("RimeLanding"), TEXT("RimeLanding")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuickTravelTripTest, "GFGame.Ferry.QuickTravel.Trip", GFQuickTravelTests::Flags)
bool FGFQuickTravelTripTest::RunTest(const FString&)
{
	using namespace GFQuickTravelTests;

	FTripWorld Trip;
	if (!TestTrue(TEXT("World"), Trip.Open(TEXT("GFFerryQuickTrip"))))
	{
		return false;
	}
	AGFFerryDirector* Director = Trip.Director;
	AGFCharacter* Pawn = Trip.Pawn;
	AGFPlayerState* PS = Trip.PS;
	const FGFFerryStop* Home = StopByAnchor(*Director, Rime);
	const FGFFerryStop* Green = StopByAnchor(*Director, Verdant);
	const FGFFerryStop* Ember = StopByAnchor(*Director, Cinder);
	AGFFerryStand* HomeStand = AGFFerryStand::Find(Trip.World, Home ? Home->Id : NAME_None);
	AGFFerryStand* GreenStand = AGFFerryStand::Find(Trip.World, Green ? Green->Id : NAME_None);
	if (!TestNotNull(TEXT("Rime stop"), Home) || !TestNotNull(TEXT("Verdant stop"), Green) || !TestNotNull(TEXT("Cinder stop"), Ember)
		|| !TestNotNull(TEXT("Rime stand"), HomeStand) || !TestNotNull(TEXT("Verdant stand"), GreenStand))
	{
		return false;
	}

	int32 Stands = 0;
	for (TActorIterator<AGFFerryStand> It(Trip.World); It; ++It)
	{
		++Stands;
		const FGFFerryStop* At = StopByAnchor(*Director, It->GetAnchorId());
		if (TestNotNull(*FString::Printf(TEXT("Stop for %s"), *It->GetAnchorId().ToString()), At))
		{
			TestTrue(TEXT("Stand sits on its stop"), It->GetActorLocation().Equals(At->Location, 1.f));
		}
	}
	TestEqual(TEXT("One stand actor per stop"), Stands, Director->Stops.Num());

	UBoxComponent* FlyPad = HomeStand->GetFlyTrigger(Green->Id);
	UBoxComponent* PassPad = HomeStand->GetPassTrigger();
	if (!TestNotNull(TEXT("Verdant pad"), FlyPad) || !TestNotNull(TEXT("Pass pad"), PassPad))
	{
		return false;
	}
	TestTrue(TEXT("Pad overlaps"), FlyPad->GetGenerateOverlapEvents());
	TestTrue(TEXT("Pad hits pawns"), FlyPad->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap);

	TArray<FString> Lines;
	HomeStand->GetPromptLines(Lines);
	FString Joined;
	for (const FString& Line : Lines)
	{
		Joined += Line;
		Joined += TEXT("\n");
	}
	TestTrue(TEXT("Prompt says it does not follow"), Joined.Contains(TEXT("does not follow")));
	TestTrue(TEXT("Prompt names the fare"), Joined.Contains(TEXT("Dirt")));
	TestTrue(TEXT("Prompt names Verdant"), Joined.Contains(TEXT("Verdant")));
	TestTrue(TEXT("Prompt names the tame material"), Joined.Contains(TEXT("FerryTame")));

	TArray<TPair<AGFFerryDino*, FVector>> Parked;
	for (TActorIterator<AGFFerryDino> It(Trip.World); It; ++It)
	{
		if (It->bTransit)
		{
			continue;
		}
		TestFalse(TEXT("Ferry is not a companion dino"), It->IsA(AGFDino::StaticClass()));
		TestFalse(TEXT("Ferry cannot be damaged"), It->CanBeDamaged());
		TestTrue(TEXT("Faction deal tag"), FGFProtectedActor::Is(*It));
		TestEqual(TEXT("Tag name"), FGFProtectedActor::Tag, FName(TEXT("GF.FactionProtected")));
		TestEqual(TEXT("A hit does nothing"), It->TakeDamage(40.f, FDamageEvent(), nullptr, Pawn), 0.f);
		Parked.Add(TPair<AGFFerryDino*, FVector>(*It, It->GetActorLocation()));
	}
	TestEqual(TEXT("One parked bird per stand"), Parked.Num(), Director->Stops.Num());

	Pawn->SetActorLocation(FVector(0.f, 0.f, 5000.f), false, nullptr, ETeleportType::TeleportPhysics);
	Director->Tick(1.f);
	for (const TPair<AGFFerryDino*, FVector>& Bird : Parked)
	{
		if (TestNotNull(TEXT("Parked bird"), Bird.Key))
		{
			TestTrue(TEXT("Walking away leaves the bird on its perch"), Bird.Key->GetActorLocation().Equals(FGFFerryFollow::ParkedGoal(Bird.Value), 1.f));
		}
	}

	const int32 Dirt = Director->Tuning.FareCount;
	const FName DirtId = Director->Tuning.FareItem;
	Pawn->SetActorLocation(FlyPad->GetComponentLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	HomeStand->Tick(0.1f);
	TestFalse(TEXT("No blocks, no flight"), Pawn->FerryRider->IsInFlight());
	TestEqual(TEXT("Nothing spent"), PS->Inventory->GetCount(DirtId), 0);
	TestTrue(TEXT("Told why"), HomeStand->LastPrompt.Contains(TEXT("CantAfford")));
	HomeStand->Tick(0.1f);
	TestFalse(TEXT("Standing still does not ask again"), Pawn->FerryRider->IsInFlight());

	Pawn->SetActorLocation(Home->Location, false, nullptr, ETeleportType::TeleportPhysics);
	HomeStand->Tick(0.1f);
	TestTrue(TEXT("Has the fare"), PS->Inventory->AddItem(DirtId, Dirt));
	Pawn->SetActorLocation(FlyPad->GetComponentLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	const FVector RiderAt = Pawn->GetActorLocation();
	HomeStand->Tick(0.1f);
	TestTrue(TEXT("Pad starts the flight"), Pawn->FerryRider->IsInFlight());
	TestEqual(TEXT("Fare burned"), PS->Inventory->GetCount(DirtId), 0);
	TestFalse(TEXT("Rider cannot be hit in flight"), Pawn->CanBeDamaged());
	TestTrue(TEXT("Rider shares the faction deal"), FGFProtectedActor::Is(Pawn));
	for (const TPair<AGFFerryDino*, FVector>& Bird : Parked)
	{
		if (Bird.Key)
		{
			TestTrue(TEXT("Parked bird stays while the copy flies"), Bird.Key->GetActorLocation().Equals(Bird.Value, 1.f));
		}
	}

	const FVector Expected = FGFFerryRules::DropPoint(Home->Location, Green->Location, RiderAt, Director->Tuning.BoardRadius);
	Director->Tick(Director->Tuning.FlightSeconds);
	TestFalse(TEXT("Flight ended"), Pawn->FerryRider->IsInFlight());
	TestTrue(TEXT("Arrived at the Verdant drop"), LandedAt(Pawn->GetActorLocation(), Expected));
	TestTrue(TEXT("Close enough to board again"), FVector::Dist2D(Pawn->GetActorLocation(), Green->Location) <= Director->Tuning.BoardRadius);
	TestTrue(TEXT("Can be hit again once down"), Pawn->CanBeDamaged());
	const FVector ArrivalLocal = Pawn->GetActorLocation() - Green->Location;
	TArray<FName> GreenDests;
	FGFFerryRules::CollectDestinations(Director->Stops, Director->Routes, Green->Id, GreenDests);
	for (int32 Index = 0; Index < GreenDests.Num(); ++Index)
	{
		TestFalse(TEXT("Arrival is not standing on a return pad"), FGFFerryStandLayout::ContainsHorizontal(FGFFerryStandLayout::DestLocal(Index), ArrivalLocal));
	}

	UBoxComponent* Buy = GreenStand->GetPassTrigger();
	UBoxComponent* Back = GreenStand->GetFlyTrigger(Home->Id);
	if (!TestNotNull(TEXT("Verdant pass pad"), Buy) || !TestNotNull(TEXT("Pad back to Rime"), Back))
	{
		return false;
	}
	TestTrue(TEXT("Has the tame material"), PS->Inventory->AddItem(Director->Tuning.PassMaterialItem, Director->Tuning.PassPrice));
	Pawn->SetActorLocation(Buy->GetComponentLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	GreenStand->Tick(0.1f);
	TestEqual(TEXT("Pass minted"), PS->Inventory->GetCount(Director->Tuning.PassItem), 1);
	TestEqual(TEXT("Tame material burned"), PS->Inventory->GetCount(Director->Tuning.PassMaterialItem), 0);
	TestEqual(TEXT("Pass did not spend blocks"), PS->Inventory->GetCount(DirtId), 0);

	Pawn->SetActorLocation(Back->GetComponentLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	const FVector BackAt = Pawn->GetActorLocation();
	GreenStand->Tick(0.1f);
	TestTrue(TEXT("Pass boards for free"), Pawn->FerryRider->IsInFlight());
	TestEqual(TEXT("Still no blocks spent"), PS->Inventory->GetCount(DirtId), 0);
	const FVector HomeDrop = FGFFerryRules::DropPoint(Green->Location, Home->Location, BackAt, Director->Tuning.BoardRadius);
	Director->Tick(Director->Tuning.FlightSeconds);
	TestFalse(TEXT("Back on the ground"), Pawn->FerryRider->IsInFlight());
	TestTrue(TEXT("Arrived at Rime"), LandedAt(Pawn->GetActorLocation(), HomeDrop));

	const FGuid LegacyId(0xF3220601, 0xF3220602, 0xF3220603, 0xF3220604);
	const FVector BeforeLegacy = Pawn->GetActorLocation();
	const FGFFerryResult Legacy = Director->RequestTrip(Pawn, Home->Id, LegacyId);
	TestTrue(TEXT("Destination-less trip still flies"), Legacy.bOk);
	const FVector CinderDrop = FGFFerryRules::DropPoint(Home->Location, Ember->Location, BeforeLegacy, Director->Tuning.BoardRadius);
	Director->Tick(Director->Tuning.FlightSeconds);
	TestTrue(TEXT("That trip still arrives at Cinderwrights"), LandedAt(Pawn->GetActorLocation(), CinderDrop));
	TestEqual(TEXT("Pass still spent nothing"), PS->Inventory->GetCount(DirtId), 0);

	const FGFFerryResult Loop = Director->RequestTripTo(Pawn, Home->Id, Home->Id, FGuid::NewGuid());
	TestFalse(TEXT("No trip to the same stand"), Loop.bOk);
	TestEqual(TEXT("NoRoute"), Loop.Code, FGFFerryCodes::NoRoute);
	const FGFFerryResult Missing = Director->RequestTripTo(Pawn, Home->Id, FName(TEXT("Nowhere")), FGuid::NewGuid());
	TestFalse(TEXT("Unknown stand"), Missing.bOk);
	TestEqual(TEXT("UnknownStop"), Missing.Code, FGFFerryCodes::UnknownStop);

	for (const TPair<AGFFerryDino*, FVector>& Bird : Parked)
	{
		if (Bird.Key)
		{
			TestTrue(TEXT("Parked bird never followed"), Bird.Key->GetActorLocation().Equals(Bird.Value, 1.f));
			TestEqual(TEXT("Still immune"), Bird.Key->TakeDamage(15.f, FDamageEvent(), nullptr, Pawn), 0.f);
		}
	}
	return true;
}

#endif
