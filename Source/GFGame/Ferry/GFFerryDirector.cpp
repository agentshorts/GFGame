#include "GFFerryDirector.h"
#include "GFFerryDino.h"
#include "GFFerryRiderComponent.h"
#include "GFDino.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFTamingComponent.h"
#include "GFTerritorySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"

namespace
{
	/** Capsule lift so a spawned mob's feet are near the hazard's ground Z. */
	constexpr float MobSpawnLift = 100.f;

	FGFFerryResult Fail(FName Code)
	{
		FGFFerryResult Result;
		Result.bOk = false;
		Result.Code = Code;
		return Result;
	}

	FGFFerryResult Succeed()
	{
		FGFFerryResult Result;
		Result.bOk = true;
		Result.Code = FGFFerryCodes::Ok;
		return Result;
	}

	bool ChargeApplied(EGFTxResult Result)
	{
		return Result == EGFTxResult::Applied || Result == EGFTxResult::AlreadyApplied;
	}

	const FGFTerritoryHazard* FindHazard(const TArray<FGFTerritoryHazard>& Hazards, FName Id)
	{
		if (Id.IsNone())
		{
			return nullptr;
		}
		return Hazards.FindByPredicate([Id](const FGFTerritoryHazard& Hazard) { return Hazard.Id == Id; });
	}
}

AGFFerryDirector::AGFFerryDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);
}

void AGFFerryDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}
	SpawnWait = MobSpawn.SpawnIntervalSeconds;
	MobRng.GenerateNewSeed();
	SpawnStopDinos();
}

void AGFFerryDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroySpawned();
	Super::EndPlay(EndPlayReason);
}

void AGFFerryDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	for (int32 Index = Flights.Num() - 1; Index >= 0; --Index)
	{
		if (AdvanceFlight(Flights[Index], DeltaSeconds))
		{
			Flights.RemoveAt(Index);
		}
	}
	TickMobs(DeltaSeconds);
}

FGFFerryResult AGFFerryDirector::RequestTrip(APawn* Rider, FName FromStop)
{
	if (!HasAuthority() || !Rider)
	{
		return Fail(FGFFerryCodes::NotAuthority);
	}
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	AGFPlayerState* PS = Rider->GetPlayerState<AGFPlayerState>();
	if (!GS || !PS || PS->PlayerKey.IsEmpty())
	{
		return Fail(FGFFerryCodes::NotAuthority);
	}
	UGFFerryRiderComponent* Ride = Rider->FindComponentByClass<UGFFerryRiderComponent>();
	const bool bInFlight = Ride && Ride->IsInFlight();
	const FGFFerryStop* From = FGFFerryRules::FindStop(Stops, FromStop);
	const FGFFerryRoute* Route = From ? FGFFerryRules::FindRouteFrom(Routes, FromStop) : nullptr;
	const FGFFerryStop* To = Route ? FGFFerryRules::FindStop(Stops, FGFFerryRules::OtherStop(*Route, FromStop)) : nullptr;
	const FGFFerryStop* BoardStop = (From && To) ? From : nullptr;

	FGFInventory Inventory;
	if (!GS->Accounts().ReadAccount(PS->PlayerKey, Inventory))
	{
		return Fail(FGFFerryCodes::NotAuthority);
	}
	const FGuid TxId = FGuid::NewGuid();
	const FGFFerryFarePlan Plan = FGFFerryRules::PlanFare(
		true, bInFlight, GS->Ledger.WasApplied(TxId), BoardStop, Rider->GetActorLocation(), Inventory, Tuning);
	if (!Plan.Result.bOk)
	{
		return Plan.Result;
	}
	if (Plan.bCharge)
	{
		const EGFTxResult Burned = GS->ServerBurn(TxId, PS->PlayerKey, Plan.Stacks, FGFFerryRules::FareReason());
		if (!ChargeApplied(Burned))
		{
			return Fail(FGFFerryCodes::CantAfford);
		}
	}
	StartFlight(Rider, *From, *To);
	return Succeed();
}

FGFFerryResult AGFFerryDirector::BuyPass(APawn* Rider, FName AtStop)
{
	if (!HasAuthority() || !Rider)
	{
		return Fail(FGFFerryCodes::NotAuthority);
	}
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	AGFPlayerState* PS = Rider->GetPlayerState<AGFPlayerState>();
	if (!GS || !PS || PS->PlayerKey.IsEmpty())
	{
		return Fail(FGFFerryCodes::NotAuthority);
	}
	UGFFerryRiderComponent* Ride = Rider->FindComponentByClass<UGFFerryRiderComponent>();
	const bool bInFlight = Ride && Ride->IsInFlight();
	const FGFFerryStop* Stop = FGFFerryRules::FindStop(Stops, AtStop);

	FGFInventory Inventory;
	if (!GS->Accounts().ReadAccount(PS->PlayerKey, Inventory))
	{
		return Fail(FGFFerryCodes::NotAuthority);
	}
	const FGuid RequestId = FGuid::NewGuid();
	const FGFFerryPassPlan Plan = FGFFerryRules::PlanPass(
		true, bInFlight, false, false, RequestId, Stop, Rider->GetActorLocation(), Inventory, Tuning);
	if (!Plan.Result.bOk)
	{
		return Plan.Result;
	}
	if (Plan.bBurn)
	{
		const EGFTxResult Burned = GS->ServerBurn(Plan.BurnTx, PS->PlayerKey, Plan.BurnStacks, FGFFerryRules::PassBurnReason());
		if (!ChargeApplied(Burned))
		{
			return Fail(FGFFerryCodes::CantAfford);
		}
	}
	if (Plan.bMint)
	{
		const EGFTxResult Minted = GS->ServerMint(Plan.MintTx, PS->PlayerKey, Plan.MintStacks, FGFFerryRules::PassGrantReason());
		if (!ChargeApplied(Minted))
		{
			return Fail(FGFFerryCodes::CantAfford);
		}
	}
	return Succeed();
}

void AGFFerryDirector::SpawnStopDinos()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FGFFerryStop& Stop : Stops)
	{
		if (Stop.Id.IsNone())
		{
			continue;
		}
		FRotator Face = FRotator::ZeroRotator;
		if (const FGFFerryRoute* Route = FGFFerryRules::FindRouteFrom(Routes, Stop.Id))
		{
			if (const FGFFerryStop* Other = FGFFerryRules::FindStop(Stops, FGFFerryRules::OtherStop(*Route, Stop.Id)))
			{
				const FVector Dir = (Other->Location - Stop.Location).GetSafeNormal2D();
				if (!Dir.IsNearlyZero())
				{
					Face = Dir.Rotation();
				}
			}
		}
		AGFFerryDino* Dino = World->SpawnActor<AGFFerryDino>(AGFFerryDino::StaticClass(), Stop.Location, Face, Params);
		if (!Dino)
		{
			continue;
		}
		Dino->SetStopId(Stop.Id);
		StopDinos.Add(Dino);
	}
}

void AGFFerryDirector::StartFlight(APawn* Rider, const FGFFerryStop& From, const FGFFerryStop& To)
{
	UWorld* World = GetWorld();
	if (!World || !Rider)
	{
		return;
	}
	const FVector Dir = (To.Location - From.Location).GetSafeNormal2D();
	const FRotator Face = Dir.IsNearlyZero() ? FRotator::ZeroRotator : Dir.Rotation();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFFerryDino* Craft = World->SpawnActor<AGFFerryDino>(AGFFerryDino::StaticClass(), From.Location, Face, Params);
	if (!Craft)
	{
		return;
	}
	Craft->SetTransit(true);

	UGFFerryRiderComponent* Ride = Rider->FindComponentByClass<UGFFerryRiderComponent>();
	if (Ride)
	{
		Ride->BeginFlightProtection();
	}
	bool bDisabledMovement = false;
	if (UCharacterMovementComponent* Move = Rider->FindComponentByClass<UCharacterMovementComponent>())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
		bDisabledMovement = true;
	}
	Rider->AttachToActor(Craft, FAttachmentTransformRules::KeepWorldTransform);

	FFlight Flight;
	Flight.Craft = Craft;
	Flight.Rider = Rider;
	Flight.Ride = Ride;
	Flight.From = From.Location;
	Flight.To = To.Location;
	Flight.Drop = FGFFerryRules::DropPoint(From.Location, To.Location, Rider->GetActorLocation(), Tuning.BoardRadius);
	Flight.Duration = Tuning.FlightSeconds;
	Flight.Altitude = Tuning.CruiseAltitude;
	Flight.RiderLift = Rider->GetActorLocation().Z - From.Location.Z;
	Flight.bSawController = Rider->GetController() != nullptr;
	Flight.bDisabledMovement = bDisabledMovement;
	Flights.Add(Flight);
}

bool AGFFerryDirector::AdvanceFlight(FFlight& Flight, float DeltaSeconds)
{
	AGFFerryDino* Craft = Flight.Craft.Get();
	APawn* Rider = Flight.Rider.Get();
	if (!Rider)
	{
		if (Craft)
		{
			Craft->Destroy();
		}
		return true;
	}
	if (!Craft)
	{
		FinishFlight(Flight);
		return true;
	}
	if (Flight.bSawController && Rider->GetController() == nullptr)
	{
		FinishFlight(Flight);
		return true;
	}

	Flight.Elapsed += FMath::Max(0.f, DeltaSeconds);
	const float Alpha = Flight.Duration <= 0.f ? 1.f : FMath::Clamp(Flight.Elapsed / Flight.Duration, 0.f, 1.f);
	const FVector Body = FGFFerryRules::FlightPosition(Flight.From, Flight.To, Alpha, Flight.Altitude);
	Craft->SetActorLocation(Body);
	const FVector Dir = (Flight.To - Flight.From).GetSafeNormal2D();
	if (!Dir.IsNearlyZero())
	{
		Craft->SetActorRotation(Dir.Rotation());
	}
	Rider->SetActorLocation(Body + FVector(0.f, 0.f, Flight.RiderLift));
	if (Alpha >= 1.f - KINDA_SMALL_NUMBER)
	{
		FinishFlight(Flight);
		return true;
	}
	return false;
}

void AGFFerryDirector::FinishFlight(FFlight& Flight)
{
	if (APawn* Rider = Flight.Rider.Get())
	{
		Rider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Rider->SetActorLocation(Flight.Drop);
		if (Flight.bDisabledMovement)
		{
			if (UCharacterMovementComponent* Move = Rider->FindComponentByClass<UCharacterMovementComponent>())
			{
				Move->SetMovementMode(MOVE_Walking);
			}
		}
	}
	if (UGFFerryRiderComponent* Ride = Flight.Ride.Get())
	{
		Ride->EndFlightProtection();
	}
	if (AGFFerryDino* Craft = Flight.Craft.Get())
	{
		Craft->Destroy();
		Flight.Craft = nullptr;
	}
}

TArray<FGFFerryRoutePerson> AGFFerryDirector::GatherPeople() const
{
	TArray<FGFFerryRoutePerson> People;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return People;
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		const AGFPlayerState* PS = Pawn->GetPlayerState<AGFPlayerState>();
		if (!PS || PS->bLoggedOut || PS->PlayerKey.IsEmpty())
		{
			continue;
		}
		FGFFerryRoutePerson Person;
		Person.Key = PS->PlayerKey;
		Person.Location = Pawn->GetActorLocation();
		if (const UGFFerryRiderComponent* Ride = Pawn->FindComponentByClass<UGFFerryRiderComponent>())
		{
			Person.bFlying = Ride->IsInFlight();
		}
		People.Add(Person);
	}
	return People;
}

void AGFFerryDirector::TickMobs(float DeltaSeconds)
{
	UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(this);
	const TArray<FGFTerritoryHazard> NoHazards;
	const TArray<FGFTerritoryHazard>& Hazards = Territory ? Territory->Hazards : NoHazards;
	const TArray<FGFFerryRoutePerson> People = GatherPeople();

	for (int32 Index = Mobs.Num() - 1; Index >= 0; --Index)
	{
		FRouteMob& Mob = Mobs[Index];
		Mob.Age += FMath::Max(0.f, DeltaSeconds);
		AGFDino* Dino = Mob.Dino.Get();
		if (!Dino)
		{
			Mobs.RemoveAt(Index);
			continue;
		}
		const FGFTerritoryHazard* Box = FindHazard(Hazards, Mob.HazardId);
		const bool bNear = Box && FGFFerryRouteSpawnRules::IsRouteActive(*Box, MobSpawn.ActivationMargin, People);
		if (!Box || FGFFerryRouteSpawnRules::ShouldDespawn(*Box, Dino->GetActorLocation(), MobSpawn.ActivationMargin, Mob.Age, MobSpawn.IdleDespawnSeconds, bNear))
		{
			Dino->Destroy();
			Mobs.RemoveAt(Index);
		}
	}

	bool bActive = false;
	for (const FGFFerryRoute& Route : Routes)
	{
		const FGFTerritoryHazard* Box = FindHazard(Hazards, Route.LandRouteHazardId);
		if (Box && FGFFerryRouteSpawnRules::IsRouteActive(*Box, MobSpawn.ActivationMargin, People))
		{
			bActive = true;
			break;
		}
	}
	if (!bActive)
	{
		SpawnWait = MobSpawn.SpawnIntervalSeconds;
		return;
	}

	SpawnWait -= DeltaSeconds;
	if (SpawnWait > 0.f)
	{
		return;
	}
	SpawnWait = MobSpawn.SpawnIntervalSeconds;

	int32 Alive = 0;
	for (const FRouteMob& Mob : Mobs)
	{
		Alive += Mob.Dino.IsValid() ? 1 : 0;
	}
	if (!FGFFerryRouteSpawnRules::CanSpawn(Alive, MobSpawn.MaxAlive, true) || !Territory)
	{
		return;
	}

	auto HazardAt = [Territory](const FVector& Point) -> FName
	{
		const FGFTerritoryHazardHit Hit = Territory->GetHazardAt(Point);
		return Hit.bHazard ? Hit.Id : NAME_None;
	};

	for (const FGFFerryRoute& Route : Routes)
	{
		const FGFTerritoryHazard* Box = FindHazard(Hazards, Route.LandRouteHazardId);
		if (!Box || !FGFFerryRouteSpawnRules::IsRouteActive(*Box, MobSpawn.ActivationMargin, People))
		{
			continue;
		}
		FVector Point;
		if (!FGFFerryRouteSpawnRules::PickSpawnPoint(*Box, Route.LandRouteHazardId, MobRng, HazardAt, Point))
		{
			continue;
		}
		const FName Species = FGFFerryRouteSpawnRules::PickSpecies(MobSpawn.MobSpecies, MobRng);
		if (Species.IsNone())
		{
			return;
		}
		Point.Z += MobSpawnLift;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFDino* Dino = GetWorld()->SpawnActor<AGFDino>(AGFDino::StaticClass(), Point, FRotator::ZeroRotator, Params);
		if (!Dino || !Dino->Taming)
		{
			continue;
		}
		Dino->SetSpecies(Species);
		Dino->AttackDamage = FGFFerryRouteSpawnRules::ScaledAttackDamage(Dino->AttackDamage, MobSpawn.MobLevel, MobSpawn.MobDamagePerLevel);

		const FGFFerryRoutePerson* Nearest = nullptr;
		float Best = 0.f;
		for (const FGFFerryRoutePerson& Person : People)
		{
			if (Person.bFlying || Person.Key.IsEmpty())
			{
				continue;
			}
			if (FGFFerryRouteSpawnRules::DistanceToBox(*Box, Person.Location) > MobSpawn.ActivationMargin)
			{
				continue;
			}
			const float Dist = FVector::Dist2D(Person.Location, Point);
			if (!Nearest || Dist < Best)
			{
				Nearest = &Person;
				Best = Dist;
			}
		}
		if (Nearest)
		{
			Dino->Taming->ServerForceHostile(Nearest->Key);
		}
		FRouteMob Mob;
		Mob.Dino = Dino;
		Mob.HazardId = Route.LandRouteHazardId;
		Mobs.Add(Mob);
		return;
	}
}

void AGFFerryDirector::DestroySpawned()
{
	for (FFlight& Flight : Flights)
	{
		FinishFlight(Flight);
	}
	Flights.Reset();
	for (FRouteMob& Mob : Mobs)
	{
		if (AGFDino* Dino = Mob.Dino.Get())
		{
			Dino->Destroy();
		}
	}
	Mobs.Reset();
	for (AGFFerryDino* Dino : StopDinos)
	{
		if (Dino)
		{
			Dino->Destroy();
		}
	}
	StopDinos.Reset();
}
