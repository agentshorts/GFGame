#include "GFTownsfolkDirector.h"
#include "GFElder.h"
#include "GFLog.h"
#include "GFTerritorySubsystem.h"
#include "GFVillager.h"
#include "GFWanderComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AGFTownsfolkDirector::AGFTownsfolkDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	VillagerOffsets = {
		FVector(280.f, 0.f, 88.f),
		FVector(-280.f, 0.f, 88.f),
		FVector(0.f, 280.f, 88.f),
		FVector(0.f, -280.f, 88.f)
	};
}

void AGFTownsfolkDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const FString MapName = World ? World->GetMapName() : FString();
	const FString Package = (World && World->GetOutermost()) ? World->GetOutermost()->GetName() : FString();
	if (!FGFTownsfolkRules::IsFactionsWorld(MapName) && !FGFTownsfolkRules::IsFactionsWorld(Package))
	{
		return;
	}
	SpawnAll();
}

void AGFTownsfolkDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AGFVillager* Folk : Townsfolk)
	{
		if (Folk)
		{
			Folk->Destroy();
		}
	}
	Townsfolk.Reset();
	Super::EndPlay(EndPlayReason);
}

const TArray<FGFTerritoryFaction>* AGFTownsfolkDirector::Layout(FGFTerritoryCurve& OutCurve) const
{
	if (const UGFTerritorySubsystem* Live = UGFTerritorySubsystem::Get(this))
	{
		if (Live->FactionLayout.Num() > 0)
		{
			OutCurve = Live->Curve;
			return &Live->FactionLayout;
		}
	}
	if (const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>())
	{
		OutCurve = Defaults->Curve;
		return &Defaults->FactionLayout;
	}
	return nullptr;
}

void AGFTownsfolkDirector::GatherTagged(TArray<FGFTownsfolkSpot>& Out) const
{
	Out.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		FGFTownsfolkSpot Spot;
		if (FGFTownsfolkRules::SpotFromTags(It->Tags, It->GetActorLocation(), Spot))
		{
			Out.Add(Spot);
		}
	}
}

void AGFTownsfolkDirector::SpawnAll()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	FGFTerritoryCurve Curve;
	const TArray<FGFTerritoryFaction>* Factions = Layout(Curve);
	if (!Factions)
	{
		return;
	}
	TArray<FGFTownsfolkSpot> Tagged;
	GatherTagged(Tagged);
	TArray<FGFTownsfolkSpot> Spots;
	FGFTownsfolkRules::Resolve(*Factions, Tagged, VillagerOffsets, ElderOffset, VillagersPerTown, EldersPerTown, Spots);

	const UGFWanderComponent* WanderDefaults = GetDefault<UGFWanderComponent>();
	const float IdleMin = WanderDefaults ? WanderDefaults->WanderEveryMin : 4.f;
	const float IdleMax = WanderDefaults ? WanderDefaults->WanderEveryMax : 9.f;
	const float Stop = WanderDefaults ? WanderDefaults->StopDistance : 100.f;
	const float RadiusPick = WanderRadius > 0.f
		? WanderRadius
		: (WanderDefaults && WanderDefaults->WanderRadius > 0.f ? WanderDefaults->WanderRadius : 400.f);
	const UGFTerritorySubsystem* Live = UGFTerritorySubsystem::Get(this);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;

	for (const FGFTownsfolkSpot& Spot : Spots)
	{
		const FGFTerritoryFaction* Faction = nullptr;
		for (const FGFTerritoryFaction& Entry : *Factions)
		{
			if (Entry.Id == Spot.TownId)
			{
				Faction = &Entry;
				break;
			}
		}
		if (!Faction)
		{
			continue;
		}
		int32 Members = 0;
		if (Live)
		{
			if (const FGFTerritoryFaction* Found = Live->GetMap().FindFaction(Spot.TownId))
			{
				Members = Found->Members;
			}
		}
		UClass* Class = (Spot.Role == EGFTownsfolkRole::Elder) ? AGFElder::StaticClass() : AGFVillager::StaticClass();
		AGFVillager* Folk = World->SpawnActor<AGFVillager>(Class, Spot.Location, FRotator::ZeroRotator, Params);
		if (!Folk)
		{
			continue;
		}
		FGFTownsfolkSpawnParams SpawnParams;
		SpawnParams.TownId = Spot.TownId;
		SpawnParams.Role = Spot.Role;
		SpawnParams.TownCenter = Faction->Center;
		SpawnParams.TownRadius = FGFTownsfolkRules::CampRadius(*Faction, Curve, Members);
		SpawnParams.WanderRadius = RadiusPick;
		SpawnParams.WalkSpeed = WalkSpeed;
		SpawnParams.TalkSeconds = TalkSeconds;
		SpawnParams.Reach = InteractReach > 0.f ? InteractReach : FGFTownsfolkRules::InteractReachCm;
		SpawnParams.IdleMin = IdleMin;
		SpawnParams.IdleMax = IdleMax;
		SpawnParams.StopDistance = Stop;
		Folk->InitTownsfolk(SpawnParams);
		Townsfolk.Add(Folk);
	}
	UE_LOG(LogGF, Log, TEXT("Townsfolk: %d across %d towns"), Townsfolk.Num(), Factions->Num());
}
