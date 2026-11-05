#include "GFTheftDirector.h"
#include "GFPlayerController.h"
#include "GFDino.h"
#include "GFElderBook.h"
#include "GFLog.h"
#include "GFPlayerState.h"
#include "GFQuestLine.h"
#include "GFQuestSubsystem.h"
#include "GFTamingComponent.h"
#include "GFTerritorySubsystem.h"
#include "GFTownNodes.h"
#include "GFTownsfolkRules.h"
#include "GFVillager.h"
#include "GFWildDinoDirector.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace GFTheftPrivate
{
	bool HasSight(UWorld* World, const AActor* From, const FVector& To)
	{
		if (!World || !From)
		{
			return false;
		}
		const FVector Start = From->GetActorLocation() + FVector(0.f, 0.f, 80.f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GFTheftSight), false, From);
		if (!World->LineTraceSingleByChannel(Hit, Start, To, ECC_Visibility, Params))
		{
			return true;
		}
		return Hit.Distance + 30.f >= FVector::Dist(Start, To);
	}

	FString DisplayNameFor(UWorld* World, const FString& Key)
	{
		if (!World || Key.IsEmpty())
		{
			return FString(TEXT("Someone"));
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
			if (PS && PS->PlayerKey == Key)
			{
				const FString Name = PS->GetPlayerName();
				if (!Name.IsEmpty())
				{
					return Name;
				}
			}
		}
		if (const AGameStateBase* GS = World->GetGameState())
		{
			for (const APlayerState* Player : GS->PlayerArray)
			{
				const AGFPlayerState* PS = Cast<AGFPlayerState>(Player);
				if (PS && PS->PlayerKey == Key)
				{
					const FString Name = PS->GetPlayerName();
					return Name.IsEmpty() ? Key : Name;
				}
			}
		}
		return Key;
	}

	APawn* PawnFor(UWorld* World, const FString& Key)
	{
		return UGFTamingComponent::FindTamerPawn(World, Key);
	}

	bool InTown(UWorld* World, const FString& Key, FName TownId)
	{
		const APawn* Pawn = PawnFor(World, Key);
		UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(World);
		if (!Pawn || !Territory || TownId.IsNone())
		{
			return false;
		}
		const FGFTerritoryZoneHit Hit = Territory->GetZoneAt(Pawn->GetActorLocation());
		return Hit.Zone == EGFTerritoryZone::Faction && Hit.FactionId == TownId;
	}

	FName TownAt(UWorld* World, const FVector& Location)
	{
		UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(World);
		const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
		const TArray<FGFTerritoryFaction>* Factions = nullptr;
		FGFTerritoryCurve Curve;
		FName ZoneFaction = NAME_None;
		if (Territory)
		{
			const FGFTerritoryZoneHit Hit = Territory->GetZoneAt(Location);
			if (Hit.Zone == EGFTerritoryZone::Faction)
			{
				ZoneFaction = Hit.FactionId;
			}
			if (Territory->FactionLayout.Num() > 0)
			{
				Factions = &Territory->FactionLayout;
				Curve = Territory->Curve;
			}
		}
		if (!Factions && Defaults)
		{
			Factions = &Defaults->FactionLayout;
			Curve = Defaults->Curve;
		}
		if (!Factions)
		{
			return ZoneFaction;
		}
		return FGFTheftRules::ResolveTown(*Factions, Curve, ZoneFaction, Location);
	}

	FVector TownCenter(UWorld* World, FName TownId)
	{
		auto Search = [TownId](const TArray<FGFTerritoryFaction>& Factions) -> const FGFTerritoryFaction*
		{
			for (const FGFTerritoryFaction& Faction : Factions)
			{
				if (Faction.Id == TownId)
				{
					return &Faction;
				}
			}
			return nullptr;
		};
		if (const UGFTerritorySubsystem* Live = UGFTerritorySubsystem::Get(World))
		{
			if (const FGFTerritoryFaction* Found = Search(Live->FactionLayout))
			{
				return Found->Center;
			}
		}
		if (const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>())
		{
			if (const FGFTerritoryFaction* Found = Search(Defaults->FactionLayout))
			{
				return Found->Center;
			}
		}
		return FVector::ZeroVector;
	}

	AGFVillager* ElderOf(UWorld* World, FName TownId)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AGFVillager> It(World); It; ++It)
		{
			if (It->TownId == TownId && It->TownsfolkRole == EGFTownsfolkRole::Elder)
			{
				return *It;
			}
		}
		return nullptr;
	}

	void SpeciesFor(UWorld* World, FName TownId, TArray<FName>& OutSpecies, float& OutRange)
	{
		OutSpecies.Reset();
		OutRange = FGFTheftRules::DefaultHireRangeCm;
		auto Take = [&OutSpecies, &OutRange, TownId](const TArray<FGFWildDinoSpawn>& Spawns)
		{
			for (const FGFWildDinoSpawn& Spawn : Spawns)
			{
				if (Spawn.AnchorId == TownId && !Spawn.Species.IsNone())
				{
					OutSpecies.AddUnique(Spawn.Species);
					OutRange = FMath::Max(OutRange, Spawn.Radius);
				}
			}
		};
		if (World)
		{
			for (TActorIterator<AGFWildDinoDirector> It(World); It; ++It)
			{
				Take(It->Spawns);
				if (OutSpecies.Num() > 0)
				{
					return;
				}
			}
		}
		if (const AGFWildDinoDirector* Defaults = GetDefault<AGFWildDinoDirector>())
		{
			Take(Defaults->Spawns);
		}
	}

	template <typename HireT>
	int32 CountChasing(const TArray<HireT>& Hired, const FString& ThiefKey)
	{
		int32 Alive = 0;
		for (const HireT& Hire : Hired)
		{
			const AGFDino* Dino = Hire.Dino.Get();
			if (!Dino || !Dino->Taming)
			{
				continue;
			}
			const bool bAlive = !Dino->IsActorBeingDestroyed() && Dino->GetLife() == EGFDinoLife::Alive;
			const FGFTameMachine& Machine = Dino->Taming->GetMachine();
			if (FGFTheftRules::IsChasing(bAlive, Machine.State, Machine.TargetKey, ThiefKey))
			{
				++Alive;
			}
		}
		return Alive;
	}

	/** One hire leaves its case. Despawn, release, or keep, per FGFTheftRules::ReleaseFor. */
	void LetGo(AGFDino* Dino, bool bSpawned, bool bWorldEnding)
	{
		// A body that already ended play (the world is tearing down around it) has nothing left to let go of.
		if (!Dino || Dino->IsActorBeingDestroyed() || !Dino->HasActorBegunPlay())
		{
			return;
		}
		const EGFTameState State = Dino->Taming ? Dino->Taming->GetTameState() : EGFTameState::Wild;
		switch (FGFTheftRules::ReleaseFor(bSpawned, State, bWorldEnding))
		{
		case EGFHireRelease::Despawn:
		{
			// The case owns a hire spawn's body. A live or dead one goes now. A downed one has paid its loot,
			// keeps its down pose for the delay, and is then removed even if its own tick never gets there.
			const float Delay = FGFTheftRules::DespawnDelay(Dino->GetLife());
			if (Delay <= 0.f)
			{
				Dino->Destroy();
			}
			else
			{
				Dino->SetLifeSpan(Delay);
			}
			break;
		}
		case EGFHireRelease::Release:
			if (Dino->Taming)
			{
				Dino->Taming->ServerReleaseHire();
			}
			break;
		default:
			break;
		}
	}

	AGFDino* SpawnHiredOnRing(UWorld* World, const FVector& Center, FName Species, float MinCm, float MaxCm)
	{
		if (!World || Species.IsNone())
		{
			return nullptr;
		}
		FRandomStream Rng(FMath::Rand());
		FVector Point = AGFWildDinoDirector::PickRingPoint(Center, FMath::Min(MinCm, MaxCm), MaxCm, Rng);
		Point.Z = Center.Z + 100.f;
		const float Yaw = (Point - Center).GetSafeNormal2D().Rotation().Yaw;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), Point, FRotator(0.f, Yaw, 0.f), Params);
		if (!Dino)
		{
			return nullptr;
		}
		Dino->SetSpecies(Species);
		if (Dino->Wander)
		{
			Dino->Wander->SetHome(Point);
			Dino->Wander->ApplyWalkSpeed();
		}
		return Dino;
	}

	AGFDino* SpawnHired(UWorld* World, const FVector& Center, FName Species, float Range)
	{
		return SpawnHiredOnRing(World, Center, Species, 600.f, Range);
	}
}

AGFTheftDirector::AGFTheftDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = false;
	SetReplicatingMovement(false);
}

void AGFTheftDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
		return;
	}
	CrystalHandle = FGFTownNodeEvents::OnCrystalTaken().AddUObject(this, &AGFTheftDirector::HandleCrystal);
	HireHandle = AGFVillager::OnHireAsked().AddUObject(this, &AGFTheftDirector::HandleHireAsk);
}

void AGFTheftDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		// Without this, a dino hired mid-hunt keeps its hire after the director is gone and ignores pack calls.
		LetGoEveryCase(FGFTheftRules::IsWorldEnding(EndPlayReason));
	}
	if (CrystalHandle.IsValid())
	{
		FGFTownNodeEvents::OnCrystalTaken().Remove(CrystalHandle);
		CrystalHandle.Reset();
	}
	if (HireHandle.IsValid())
	{
		AGFVillager::OnHireAsked().Remove(HireHandle);
		HireHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AGFTheftDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		Refresh(DeltaSeconds);
	}
}

FName AGFTheftDirector::TownAt(UWorld* World, const FVector& Location)
{
	return GFTheftPrivate::TownAt(World, Location);
}

AGFDino* AGFTheftDirector::SpawnHireBody(UWorld* World, const FVector& Center, FName Species, float MinCm, float MaxCm)
{
	return GFTheftPrivate::SpawnHiredOnRing(World, Center, Species, MinCm, MaxCm);
}

void AGFTheftDirector::LetGoHire(AGFDino* Dino, bool bSpawned, bool bWorldEnding)
{
	GFTheftPrivate::LetGo(Dino, bSpawned, bWorldEnding);
}

void AGFTheftDirector::HandleCrystal(UWorld* World, const FString& NodeKey, FVector Location, const TArray<FString>& ContributorKeys)
{
	if (!HasAuthority() || World != GetWorld())
	{
		return;
	}
	TArray<FString> Players;
	for (const FString& Key : ContributorKeys)
	{
		if (!Key.IsEmpty() && !Players.Contains(Key))
		{
			Players.Add(Key);
		}
	}
	if (Players.Num() == 0)
	{
		return;
	}

	const FName TownId = GFTheftPrivate::TownAt(World, Location);
	for (const FString& Key : Players)
	{
		FGFQuestEvents::Raise(World, FGFQuestEvents::CrystalTaken(Key, TownId));
	}

	TArray<FString> Open;
	for (const FString& Key : Players)
	{
		// Heat is per town: already hunted in another town still opens a case here.
		const FGFTheftHeat* Heat = Heats.Find(Key, TownId);
		if (!Heat || Heat->State == EGFTheftHeat::Clear)
		{
			Open.Add(Key);
		}
	}

	TArray<AGFVillager*> Witnesses;
	if (!TownId.IsNone())
	{
		for (TActorIterator<AGFVillager> It(World); It; ++It)
		{
			AGFVillager* Villager = *It;
			if (!Villager || Villager->TownId != TownId)
			{
				continue;
			}
			const float Distance = FVector::Dist(Villager->GetActorLocation(), Location);
			const bool bSight = GFTheftPrivate::HasSight(World, Villager, Location);
			if (FGFTheftRules::Sees(Villager->TownsfolkRole, Distance, bSight))
			{
				Witnesses.Add(Villager);
			}
		}
	}

	if (Witnesses.Num() == 0)
	{
		for (const FString& Key : Open)
		{
			if (!TownId.IsNone())
			{
				ClearPlayer(Key, TownId);
			}
		}
		PublishHeats();
		return;
	}
	if (Open.Num() == 0)
	{
		return;
	}

	FCase& Case = Cases.AddDefaulted_GetRef();
	Case.Id = NextCaseId++;
	Case.TownId = TownId;
	Case.ThiefKey = Open[0];
	Case.Players = Open;
	Case.Incident = Location;
	Case.OpenedAt = World->GetTimeSeconds();

	const APawn* Thief = GFTheftPrivate::PawnFor(World, Case.ThiefKey);
	const FVector Face = Thief ? Thief->GetActorLocation() : Location;
	const AGFVillager* Elder = GFTheftPrivate::ElderOf(World, TownId);
	const FVector ElderPoint = Elder ? Elder->GetActorLocation() : GFTheftPrivate::TownCenter(World, TownId);
	for (AGFVillager* Villager : Witnesses)
	{
		Villager->BeginWarn(Face, ElderPoint);
	}
	for (const FString& Key : Open)
	{
		FGFTheftRules::NoteWitness(Heats.FindOrAdd(Key, TownId), TownId);
	}
	PublishHeats();
	UE_LOG(LogGF, Log, TEXT("Theft: %s crystal %s seen by %d"), *TownId.ToString(), *NodeKey, Witnesses.Num());
}

void AGFTheftDirector::HandleHireAsk(AActor* VillagerActor)
{
	AGFVillager* Villager = Cast<AGFVillager>(VillagerActor);
	if (!HasAuthority() || !Villager || Villager->GetWorld() != GetWorld())
	{
		return;
	}
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		FCase& Case = Cases[Index];
		if (Case.bClosed || Case.bHired || Case.TownId != Villager->TownId)
		{
			continue;
		}
		if (!FGFTheftRules::ShouldHire(1, Case.bHired))
		{
			continue;
		}
		CommitHire(Index);
		return;
	}
}

void AGFTheftDirector::CommitHire(int32 CaseIndex)
{
	if (!Cases.IsValidIndex(CaseIndex))
	{
		return;
	}
	FCase& Case = Cases[CaseIndex];
	if (Case.bHired || Case.bClosed || Case.ThiefKey.IsEmpty())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	Case.bHired = true;

	TArray<FName> Species;
	float Range = FGFTheftRules::DefaultHireRangeCm;
	GFTheftPrivate::SpeciesFor(World, Case.TownId, Species, Range);
	const FVector Center = GFTheftPrivate::TownCenter(World, Case.TownId);

	TArray<AGFDino*> Bodies;
	TArray<FGFHireCandidate> Pool;
	for (TActorIterator<AGFDino> It(World); It; ++It)
	{
		AGFDino* Dino = *It;
		// A rental is somebody's ride or escort (a side quest hire walking with a player): never borrowed for a case.
		if (!Dino || !Dino->Taming || Dino->GetLife() != EGFDinoLife::Alive || Dino->IsRental())
		{
			continue;
		}
		if (!Species.Contains(Dino->Taming->Species))
		{
			continue;
		}
		FGFHireCandidate Candidate;
		Candidate.bWild = Dino->Taming->GetTameState() == EGFTameState::Wild;
		Candidate.Distance = FVector::Dist2D(Dino->GetActorLocation(), Case.Incident);
		Candidate.bInRange = FVector::Dist2D(Dino->GetActorLocation(), Center) <= Range;
		Bodies.Add(Dino);
		Pool.Add(Candidate);
	}

	const FGFHirePlan Plan = FGFTheftRules::PlanHire(Pool, Species.Num());
	for (int32 Index : Plan.Existing)
	{
		AGFDino* Dino = Bodies.IsValidIndex(Index) ? Bodies[Index] : nullptr;
		if (!Dino || !Dino->Taming)
		{
			continue;
		}
		if (Dino->Taming->ServerHire(Case.ThiefKey) != EGFForceHostile::Refused)
		{
			Case.Hired.Add(FCase::FHire{ Dino, false });
		}
	}
	for (int32 SpawnIndex = 0; SpawnIndex < Plan.SpawnCount; ++SpawnIndex)
	{
		const FName SpeciesId = Species[SpawnIndex % Species.Num()];
		AGFDino* Dino = GFTheftPrivate::SpawnHired(World, Center, SpeciesId, Range);
		if (!Dino)
		{
			continue;
		}
		if (Dino->Taming && Dino->Taming->ServerHire(Case.ThiefKey) != EGFForceHostile::Refused)
		{
			Case.Hired.Add(FCase::FHire{ Dino, true });
		}
		else
		{
			// Nobody else owns a hire spawn. One the hire could not take must not stay behind.
			Dino->Destroy();
		}
	}

	for (const FString& Key : Case.Players)
	{
		FGFTheftRules::NoteHire(Heats.FindOrAdd(Key, Case.TownId));
	}

	const FString Name = GFTheftPrivate::DisplayNameFor(World, Case.ThiefKey);
	const double Now = World->GetTimeSeconds();
	const EGFElderChannel Channel = FGFElderBook::TownChannel(Case.TownId);
	FGFElderBook::Post(World, Channel, EGFElderKind::Theft, Name, Case.ThiefKey, Case.TownId, Now);
	FGFElderBook::Post(World, Channel, EGFElderKind::Hire, Name, Case.ThiefKey, Case.TownId, Now);
	UE_LOG(LogGF, Log, TEXT("Theft: hired %d against %s in %s"), Case.Hired.Num(), *Case.ThiefKey, *Case.TownId.ToString());

	if (GFTheftPrivate::CountChasing(Case.Hired, Case.ThiefKey) <= 0)
	{
		LetGoAll(CaseIndex);
		for (const FString& Key : Case.Players)
		{
			ClearPlayer(Key, Case.TownId);
		}
		Case.bClosed = true;
	}
	PublishHeats();
}

void AGFTheftDirector::Refresh(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		FCase& Case = Cases[Index];
		if (Case.bClosed)
		{
			continue;
		}
		if (!Case.bHired)
		{
			const double Age = World->GetTimeSeconds() - Case.OpenedAt;
			if (Age >= static_cast<double>(FGFTownsfolkMind::WarnSeconds + FGFTownsfolkMind::FleeSeconds))
			{
				CommitHire(Index);
			}
			continue;
		}
		for (int32 HireIndex = Case.Hired.Num() - 1; HireIndex >= 0; --HireIndex)
		{
			const FCase::FHire Hire = Case.Hired[HireIndex];
			AGFDino* Dino = Hire.Dino.Get();
			if (!Dino || Dino->IsActorBeingDestroyed())
			{
				Case.Hired.RemoveAt(HireIndex);
				continue;
			}
			if (Dino->GetLife() != EGFDinoLife::Alive)
			{
				// It went down. The case lets go on this tick instead of holding the body until stand down,
				// so a downed hire spawn is cleaned up and a downed borrowed dino drops the hire.
				Case.Hired.RemoveAt(HireIndex);
				GFTheftPrivate::LetGo(Dino, Hire.bSpawned, false);
				continue;
			}
			if (!Dino->Taming)
			{
				continue;
			}
			const EGFTameState State = Dino->Taming->GetTameState();
			if (State == EGFTameState::Fled)
			{
				// It ran. The ring has already let go of it, so the case lets go too instead of hiring it back.
				Case.Hired.RemoveAt(HireIndex);
				GFTheftPrivate::LetGo(Dino, Hire.bSpawned, false);
				continue;
			}
			if (FGFTheftRules::ShouldRehire(State))
			{
				Dino->Taming->ServerHire(Case.ThiefKey);
			}
		}

		const int32 Alive = GFTheftPrivate::CountChasing(Case.Hired, Case.ThiefKey);
		bool bThiefClear = false;
		for (const FString& Key : Case.Players)
		{
			FGFTheftHeat* Heat = Heats.Find(Key, Case.TownId);
			if (!Heat)
			{
				continue;
			}
			const bool bInside = GFTheftPrivate::InTown(World, Key, Case.TownId);
			if (FGFTheftRules::AdvanceHeat(*Heat, DeltaSeconds, bInside, Alive))
			{
				FGFQuestEvents::Raise(World, FGFQuestEvents::HeatCleared(Key, Case.TownId));
				if (Key == Case.ThiefKey)
				{
					bThiefClear = true;
				}
			}
		}
		if (bThiefClear || Alive <= 0)
		{
			StandDown(Index);
			for (const FString& Key : Case.Players)
			{
				FGFTheftHeat* Heat = Heats.Find(Key, Case.TownId);
				if (Heat && Heat->State == EGFTheftHeat::Hunted)
				{
					if (FGFTheftRules::AdvanceHeat(*Heat, 0.f, true, 0))
					{
						FGFQuestEvents::Raise(World, FGFQuestEvents::HeatCleared(Key, Case.TownId));
					}
				}
			}
			Case.bClosed = true;
		}
	}
	// Closed cases have let go of their dinos and their heat. Nothing reads them again.
	Cases.RemoveAll([](const FCase& Case) { return Case.bClosed; });
	SettleStrandedEscapes();
	PublishHeats();
}

void AGFTheftDirector::StandDown(int32 CaseIndex)
{
	LetGoAll(CaseIndex);
}

void AGFTheftDirector::LetGoAll(int32 CaseIndex, bool bWorldEnding)
{
	if (!Cases.IsValidIndex(CaseIndex))
	{
		return;
	}
	FCase& Case = Cases[CaseIndex];
	for (const FCase::FHire& Hire : Case.Hired)
	{
		GFTheftPrivate::LetGo(Hire.Dino.Get(), Hire.bSpawned, bWorldEnding);
	}
	Case.Hired.Reset();
}

void AGFTheftDirector::LetGoEveryCase(bool bWorldEnding)
{
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		LetGoAll(Index, bWorldEnding);
		Cases[Index].bClosed = true;
	}
	Cases.Reset();
}

void AGFTheftDirector::SettleStrandedEscapes()
{
	UWorld* World = GetWorld();
	const UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(this);
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!Quest || !GS)
	{
		return;
	}
	for (const APlayerState* Player : GS->PlayerArray)
	{
		const AGFPlayerState* PS = Cast<AGFPlayerState>(Player);
		if (!PS || PS->PlayerKey.IsEmpty())
		{
			continue;
		}
		const FGFQuestProgress* Progress = Quest->FindProgress(PS->PlayerKey);
		if (Progress && FGFQuestLine::IsStrandedAtEscape(*Progress, Heats.IsHot(PS->PlayerKey)))
		{
			UE_LOG(LogGF, Log, TEXT("Theft: %s was at Escape with no live heat. Clearing it."), *PS->PlayerKey);
			FGFQuestEvents::Raise(World, FGFQuestEvents::HeatCleared(PS->PlayerKey));
		}
	}
}

void AGFTheftDirector::ClearPlayer(const FString& PlayerKey, FName TownId)
{
	if (PlayerKey.IsEmpty())
	{
		return;
	}
	FGFTheftHeat& Heat = Heats.FindOrAdd(PlayerKey, TownId);
	if (Heat.State != EGFTheftHeat::Clear)
	{
		Heat.State = EGFTheftHeat::Clear;
		Heat.bReportedClear = true;
	}
	FGFQuestEvents::Raise(GetWorld(), FGFQuestEvents::HeatCleared(PlayerKey, TownId));
}

void AGFTheftDirector::PublishHeats()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	for (const FString& Key : Heats.Players())
	{
		const FGFTheftHeat* Shown = Heats.Hottest(Key);
		if (!Shown)
		{
			continue;
		}
		AGFPlayerController* Found = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			AGFPlayerController* Candidate = Cast<AGFPlayerController>(It->Get());
			const AGFPlayerState* PS = Candidate ? Candidate->GetPlayerState<AGFPlayerState>() : nullptr;
			if (PS && PS->PlayerKey == Key)
			{
				Found = Candidate;
				break;
			}
		}
		if (Found)
		{
			Found->NotifyTheftHeat(Shown->State, Shown->HuntLeft);
		}
	}
}
