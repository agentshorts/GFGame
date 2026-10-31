#include "GFFerryDirector.h"
#include "GFFerryDino.h"
#include "GFFerryRiderComponent.h"
#include "GFFerryStand.h"
#include "GFDino.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFTamingComponent.h"
#include "GFTerritorySubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"

namespace GFFerryDirectorPrivate
{
	/** Capsule lift so a spawned mob's feet are near the hazard's ground Z. */
	constexpr float MobSpawnLift = 100.f;

	FGFFerryResult FerryDirectorFail(FName Code)
	{
		FGFFerryResult Result;
		Result.bOk = false;
		Result.Code = Code;
		return Result;
	}

	FGFFerryResult FerryDirectorSucceed()
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

	void SeatOnBasket(APawn* Rider, USceneComponent* Basket)
	{
		if (!Rider || !Basket)
		{
			return;
		}
		// Snap to the basket, then to the seat offset in basket space (-25, 0, 12.5).
		// The Seat socket is authored at that same point. Attaching to the socket stores
		// a zero relative location, so the rider is on the ferry but not at the seat.
		const FAttachmentTransformRules Rules(EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false);
		Rider->AttachToComponent(Basket, Rules);
		Rider->SetActorRelativeLocation(FGFFerryArt::SeatLocal());
	}

	void GrabBasket(AGFFerryDino* Craft, UStaticMeshComponent* Basket)
	{
		if (!Craft || !Basket)
		{
			return;
		}
		USceneComponent* Target = Craft->GetBird() ? static_cast<USceneComponent*>(Craft->GetBird()) : Craft->GetRootComponent();
		if (!Target)
		{
			return;
		}
		const FAttachmentTransformRules Rules(EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, false);
		if (Target->DoesSocketExist(FGFFerryArt::FerryGripSocket()))
		{
			Basket->AttachToComponent(Target, Rules, FGFFerryArt::FerryGripSocket());
		}
		else
		{
			Basket->AttachToComponent(Target, Rules);
		}
		FVector Grip = FGFFerryArt::GripLocal();
		if (Basket->DoesSocketExist(FGFFerryArt::GripSocket()))
		{
			Grip = Basket->GetSocketTransform(FGFFerryArt::GripSocket(), RTS_Component).GetLocation();
		}
		Basket->AddWorldOffset(-Basket->GetComponentTransform().TransformVector(Grip));
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
	SpawnStands();
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

FGFFerryResult AGFFerryDirector::RequestTrip(APawn* Rider, FName FromStop, const FGuid& RequestId)
{
	const FGFFerryRoute* Route = FGFFerryRules::FindRouteFrom(Routes, FromStop);
	const FName ToStop = Route ? FGFFerryRules::OtherStop(*Route, FromStop) : NAME_None;
	return RequestTripTo(Rider, FromStop, ToStop, RequestId);
}

FGFFerryResult AGFFerryDirector::RequestTripTo(APawn* Rider, FName FromStop, FName ToStop, const FGuid& RequestId)
{
	if (!HasAuthority() || !Rider || !RequestId.IsValid())
	{
		return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::NotAuthority);
	}
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	AGFPlayerState* PS = Rider->GetPlayerState<AGFPlayerState>();
	if (!GS || !PS || PS->PlayerKey.IsEmpty())
	{
		return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::NotAuthority);
	}
	const FGuid TxId = FGFFerryRules::FareTxId(RequestId);
	const FGuid RefundTx = FGFFerryRules::FareRefundTx(RequestId);
	if (GS->Ledger.WasApplied(RefundTx))
	{
		return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::FlightFailed);
	}
	if (GS->Ledger.WasApplied(TxId))
	{
		return GFFerryDirectorPrivate::FerryDirectorSucceed();
	}

	UGFFerryRiderComponent* Ride = Rider->FindComponentByClass<UGFFerryRiderComponent>();
	const bool bInFlight = Ride && Ride->IsInFlight();
	const FGFFerryStop* From = FGFFerryRules::FindStop(Stops, FromStop);
	const FGFFerryStop* To = FGFFerryRules::FindStop(Stops, ToStop);
	const FGFFerryRoute* Route = (From && To) ? FGFFerryRules::FindRouteBetween(Routes, From->Id, To->Id) : nullptr;
	const FGFFerryStop* BoardStop = Route ? From : nullptr;

	FGFInventory Inventory;
	if (!GS->Accounts().ReadAccount(PS->PlayerKey, Inventory))
	{
		return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::NotAuthority);
	}
	const FGFFerryFarePlan Plan = FGFFerryRules::PlanFare(
		true, bInFlight, false, BoardStop, Rider->GetActorLocation(), Inventory, Tuning);
	if (!Plan.Result.bOk)
	{
		if (Plan.Result.Code == FGFFerryCodes::UnknownStop && From && To && !Route)
		{
			return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::NoRoute);
		}
		return Plan.Result;
	}
	EGFTxResult Burned = EGFTxResult::AlreadyApplied;
	if (Plan.bCharge)
	{
		Burned = GS->ServerBurn(TxId, PS->PlayerKey, Plan.Stacks, FGFFerryRules::FareReason());
		if (!GFFerryDirectorPrivate::ChargeApplied(Burned))
		{
			return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::CantAfford);
		}
	}
	if (!From || !To || !StartFlight(Rider, *From, *To))
	{
		if (Plan.bCharge && Burned == EGFTxResult::Applied)
		{
			GS->ServerMint(RefundTx, PS->PlayerKey, Plan.Stacks, FGFFerryRules::FareRefundReason());
		}
		return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::FlightFailed);
	}
	return GFFerryDirectorPrivate::FerryDirectorSucceed();
}

FGFFerryResult AGFFerryDirector::BuyPass(APawn* Rider, FName AtStop, const FGuid& RequestId)
{
	if (!HasAuthority() || !Rider || !RequestId.IsValid())
	{
		return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::NotAuthority);
	}
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	AGFPlayerState* PS = Rider->GetPlayerState<AGFPlayerState>();
	if (!GS || !PS || PS->PlayerKey.IsEmpty())
	{
		return GFFerryDirectorPrivate::FerryDirectorFail(FGFFerryCodes::NotAuthority);
	}
	UGFFerryRiderComponent* Ride = Rider->FindComponentByClass<UGFFerryRiderComponent>();
	const bool bInFlight = Ride && Ride->IsInFlight();
	const FGFFerryStop* Stop = FGFFerryRules::FindStop(Stops, AtStop);
	return FGFFerryRules::ApplyPass(
		GS->Accounts(), GS->Ledger, RequestId, PS->PlayerKey, true, bInFlight, Stop, Rider->GetActorLocation(), Tuning);
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
		const FVector Perch = FGFFerryArt::PerchWorld(Stop.Location);
		AGFFerryDino* Dino = World->SpawnActor<AGFFerryDino>(AGFFerryDino::StaticClass(), Perch, FGFFerryArt::ParkRotation(), Params);
		if (!Dino)
		{
			continue;
		}
		Dino->ParkAt(Stop.Location);
		Dino->SetStopId(Stop.Id);
		StopDinos.Add(Dino);
	}
}

void AGFFerryDirector::SpawnStands()
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
		AGFFerryStand* Stand = World->SpawnActor<AGFFerryStand>(AGFFerryStand::StaticClass(), Stop.Location, FRotator::ZeroRotator, Params);
		if (!Stand)
		{
			continue;
		}
		Stand->Configure(this, Stop.Id);
		Stands.Add(Stand);
	}
}

bool AGFFerryDirector::StartFlight(APawn* Rider, const FGFFerryStop& From, const FGFFerryStop& To)
{
	if (bFailNextFlight)
	{
		bFailNextFlight = false;
		return false;
	}
	UWorld* World = GetWorld();
	if (!World || !Rider)
	{
		return false;
	}
	const FVector Perch = FGFFerryArt::PerchWorld(From.Location);
	const FVector RiderAt = Rider->GetActorLocation();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFFerryDino* Craft = World->SpawnActor<AGFFerryDino>(AGFFerryDino::StaticClass(), Perch, FGFFerryArt::ParkRotation(), Params);
	if (!Craft)
	{
		return false;
	}
	Craft->BeginTransit(Perch);
	UStaticMeshComponent* Basket = Craft->SpawnBasket();
	if (!Basket)
	{
		Craft->Destroy();
		return false;
	}
	Basket->SetWorldLocation(FGFFerryArt::BasketSpotWorld(From.Location));

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
	GFFerryDirectorPrivate::SeatOnBasket(Rider, Basket);
	if (Ride)
	{
		Ride->BeginSeat();
	}

	FFlight Flight;
	Flight.Craft = Craft;
	Flight.Rider = Rider;
	Flight.Ride = Ride;
	Flight.Basket = Basket;
	Flight.From = Perch;
	Flight.To = FGFFerryArt::PerchWorld(To.Location);
	Flight.Drop = FGFFerryRules::DropPoint(From.Location, To.Location, RiderAt, Tuning.BoardRadius);
	Flight.ArriveBasket = FGFFerryArt::BasketSpotWorld(To.Location);
	Flight.Duration = Tuning.FlightSeconds;
	Flight.Altitude = Tuning.CruiseAltitude;
	Flight.bSawController = Rider->GetController() != nullptr;
	Flight.bDisabledMovement = bDisabledMovement;
	Flights.Add(Flight);
	return true;
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
	const float LeaveAt = FGFFerryArt::FrameSeconds(FGFFerryArt::LeavePerchFrame);
	const float GrabAt = FGFFerryArt::FrameSeconds(FGFFerryArt::GrabBasketFrame);
	const float TakeoffEnd = FGFFerryArt::FrameSeconds(FGFFerryArt::TakeoffFrames);
	const float LandLen = FGFFerryArt::FrameSeconds(FGFFerryArt::LandFrames);
	const float LandStart = FMath::Max(TakeoffEnd, Flight.Duration - LandLen);
	const bool bFinished = Flight.Duration <= 0.f || Flight.Elapsed >= Flight.Duration - KINDA_SMALL_NUMBER;

	float MoveAlpha = 0.f;
	if (bFinished || Flight.Duration <= LeaveAt)
	{
		MoveAlpha = 1.f;
	}
	else if (Flight.Elapsed > LeaveAt)
	{
		MoveAlpha = FMath::Clamp((Flight.Elapsed - LeaveAt) / (Flight.Duration - LeaveAt), 0.f, 1.f);
	}

	const FVector Body = FGFFerryRules::FlightPosition(Flight.From, Flight.To, MoveAlpha, Flight.Altitude);
	Craft->SetActorLocation(Body);
	if (MoveAlpha <= KINDA_SMALL_NUMBER)
	{
		Craft->SetActorRotation(FGFFerryArt::ParkRotation());
	}
	else
	{
		const FVector Tangent = FGFFerryRules::FlightTangent(Flight.From, Flight.To, MoveAlpha, Flight.Altitude);
		if (!Tangent.IsNearlyZero())
		{
			Craft->SetActorRotation(Tangent.Rotation());
		}
	}

	FName Phase = FGFFerryArt::TakeoffAnim();
	if (Flight.Elapsed >= LandStart)
	{
		Phase = FGFFerryArt::LandAnim();
	}
	else if (Flight.Elapsed >= FMath::Min(TakeoffEnd, LandStart))
	{
		Phase = FGFFerryArt::CarryAnim();
	}
	Craft->SetAnim(Phase);

	UStaticMeshComponent* Basket = Flight.Basket.Get();
	if (Basket && !Flight.bBasketGrabbed && Flight.Elapsed >= GrabAt && Flight.Elapsed < LandStart)
	{
		GFFerryDirectorPrivate::GrabBasket(Craft, Basket);
		Flight.bBasketGrabbed = true;
	}
	const float DownAt = LandStart + FGFFerryArt::FrameSeconds(FGFFerryArt::BasketDownFrame);
	const float ReleaseAt = LandStart + FGFFerryArt::FrameSeconds(FGFFerryArt::ReleaseFrame);
	if (Basket && Flight.Elapsed >= DownAt && !Flight.bBasketReleased)
	{
		if (!Flight.bBasketDown)
		{
			Basket->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			Flight.BasketFrom = Basket->GetComponentLocation();
			Flight.bBasketDown = true;
		}
		if (Flight.Elapsed >= ReleaseAt || ReleaseAt <= DownAt)
		{
			Basket->SetWorldLocation(Flight.ArriveBasket);
			Flight.bBasketReleased = true;
		}
		else
		{
			const float U = (Flight.Elapsed - DownAt) / (ReleaseAt - DownAt);
			Basket->SetWorldLocation(FMath::Lerp(Flight.BasketFrom, Flight.ArriveBasket, U));
		}
	}

	if (bFinished || MoveAlpha >= 1.f - KINDA_SMALL_NUMBER)
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
		Ride->EndSeat();
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
		const FGFTerritoryHazard* Box = GFFerryDirectorPrivate::FindHazard(Hazards, Mob.HazardId);
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
		const FGFTerritoryHazard* Box = GFFerryDirectorPrivate::FindHazard(Hazards, Route.LandRouteHazardId);
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
		const FGFTerritoryHazard* Box = GFFerryDirectorPrivate::FindHazard(Hazards, Route.LandRouteHazardId);
		if (!Box || !FGFFerryRouteSpawnRules::IsRouteActive(*Box, MobSpawn.ActivationMargin, People))
		{
			continue;
		}
		FVector Point;
		if (!FGFFerryRouteSpawnRules::PickSpawnPoint(*Box, Route.LandRouteHazardId, MobRng, HazardAt, Point))
		{
			continue;
		}
		if (Territory->GetZoneAt(Point).Zone != EGFTerritoryZone::Wild)
		{
			continue;
		}
		const FGFTerritoryHazardHit Live = Territory->GetHazardAt(Point);
		if (!Live.bHazard || Live.Id != Route.LandRouteHazardId)
		{
			continue;
		}
		const FName Species = FGFFerryRouteSpawnRules::PickSpecies(MobSpawn.MobSpecies, MobRng);
		if (Species.IsNone())
		{
			return;
		}
		Point.Z += GFFerryDirectorPrivate::MobSpawnLift;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFDino* Dino = GetWorld()->SpawnActor<AGFDino>(AGFDino::StaticClass(), Point, FRotator::ZeroRotator, Params);
		if (!Dino)
		{
			continue;
		}
		if (!Dino->Taming)
		{
			Dino->Destroy();
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
	for (AGFFerryStand* Stand : Stands)
	{
		if (Stand)
		{
			Stand->Destroy();
		}
	}
	Stands.Reset();
}
