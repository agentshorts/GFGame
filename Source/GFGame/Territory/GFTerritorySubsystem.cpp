#include "GFTerritorySubsystem.h"
#include "GFDino.h"
#include "GFGameState.h"
#include "GFSafeZone.h"
#include "GFTamingComponent.h"
#include "Engine/World.h"

UGFTerritorySubsystem::UGFTerritorySubsystem()
{
	FGFTerritoryMap Defaults;
	Defaults.SetDefaultFactions();
	FactionLayout = Defaults.Factions;
}

UGFTerritorySubsystem* UGFTerritorySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGFTerritorySubsystem>() : nullptr;
}

void UGFTerritorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UGFSafeZoneSubsystem>();
	Super::Initialize(Collection);
	RefreshFromWorld();
	Publish();
}

void UGFTerritorySubsystem::Deinitialize()
{
	if (UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(this))
	{
		Zones->UnregisterProvider(this);
	}
	Super::Deinitialize();
}

void UGFTerritorySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshFromWorld();
	Publish();
}

TStatId UGFTerritorySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGFTerritorySubsystem, STATGROUP_Tickables);
}

bool UGFTerritorySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGFTerritorySubsystem::RefreshFromWorld()
{
	if (FactionLayout.Num() == 0)
	{
		FGFTerritoryMap Defaults;
		Defaults.SetDefaultFactions();
		FactionLayout = Defaults.Factions;
	}

	const UWorld* World = GetWorld();
	const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	Map.Curve = Curve;
	Map.Neutral = NeutralZone;
	Map.MaxMembers = FGFTerritoryLimits::CapMembers(
		FGFTerritoryLimits::SessionMaxPlayers(World),
		FGFTerritoryLimits::FactionSeatCap(GS));

	TArray<FGFTerritoryFaction> Next = FactionLayout;
	for (FGFTerritoryFaction& Slot : Next)
	{
		if (GS)
		{
			Slot.Members = GS->FactionCount(Slot.Id);
		}
		else if (const FGFTerritoryFaction* Prev = Map.FindFaction(Slot.Id))
		{
			Slot.Members = Prev->Members;
		}
		else
		{
			Slot.Members = 0;
		}
	}
	Map.Factions = Next;
}

void UGFTerritorySubsystem::Publish()
{
	UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(this);
	if (!Zones)
	{
		return;
	}
	// Only take the safe-zone slot once some faction actually owns land. An empty registration would
	// sit newest and hide the giant's default circle.
	if (Map.HasAnySafe())
	{
		Zones->RegisterProvider(this);
	}
	else
	{
		Zones->UnregisterProvider(this);
	}
}

FGFTerritoryZoneHit UGFTerritorySubsystem::GetZoneAt(FVector Location)
{
	RefreshFromWorld();
	return Map.GetZoneAt(Location);
}

bool UGFTerritorySubsystem::IsSafeFor(const AGFPlayerState* Player, FVector Location)
{
	RefreshFromWorld();
	return Map.IsSafeFor(SubjectFor(Player), Location);
}

bool UGFTerritorySubsystem::IsSafeForDino(const AGFDino* Dino, const FVector& Location)
{
	RefreshFromWorld();
	return Map.IsSafeFor(SubjectFor(Dino), Location);
}

TArray<FGFSafeRegion> UGFTerritorySubsystem::CopySafeRegions()
{
	RefreshFromWorld();
	return Map.CopySafeRegions();
}

const IGFSafeZoneQuery* UGFTerritorySubsystem::GetSafeZoneQuery() const
{
	// Null when nobody owns land, so a newer registration still falls through to the giant's default circle.
	return Map.HasAnySafe() ? &Map : nullptr;
}

FGFTerritorySubject UGFTerritorySubsystem::SubjectFor(const AGFPlayerState* Player)
{
	if (!Player || Player->bLoggedOut)
	{
		return FGFTerritorySubject::Player(NAME_None, Player && Player->bIsHost);
	}
	return FGFTerritorySubject::Player(Player->FactionId, Player->bIsHost);
}

FGFTerritorySubject UGFTerritorySubsystem::SubjectFor(const AGFDino* Dino) const
{
	if (!Dino || !Dino->Taming || Dino->Taming->GetTameState() != EGFTameState::Tamed)
	{
		return FGFTerritorySubject::WildDino();
	}
	const UWorld* World = GetWorld();
	const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	const AGFPlayerState* Owner = GS ? GS->FindPlayerByKey(Dino->Taming->GetOwnerKey()) : nullptr;
	if (!Owner || Owner->bLoggedOut)
	{
		return FGFTerritorySubject::TamedDino(NAME_None, false);
	}
	return FGFTerritorySubject::TamedDino(Owner->FactionId, Owner->bIsHost);
}
