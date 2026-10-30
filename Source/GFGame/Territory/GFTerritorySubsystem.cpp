#include "GFTerritorySubsystem.h"
#include "GFDino.h"
#include "GFGameState.h"
#include "GFSafeZone.h"
#include "GFTamingComponent.h"
#include "Engine/World.h"

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
	bPublished = false;
	Super::Deinitialize();
}

void UGFTerritorySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshFromWorld();
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
	const UWorld* World = GetWorld();
	const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	Map.Curve = Curve;
	Map.Neutral = NeutralZone;
	Map.MaxMembers = FGFTerritoryLimits::FactionSeatCap(GS);

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
	if (bPublished)
	{
		return;
	}
	if (UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(this))
	{
		Zones->RegisterProvider(this);
		bPublished = true;
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
	// No faction land yet: let the default (or the giant's) provider answer.
	return Map.HasAnySafe() ? &Map : nullptr;
}

FGFSocialPlayer UGFTerritorySubsystem::SubjectFor(const AGFPlayerState* Player) const
{
	if (!Player || Player->bLoggedOut)
	{
		return FGFSocialPlayer(Player ? Player->PlayerKey : FString(), NAME_None, Player && Player->bIsHost, false, false);
	}
	const AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	return FGFSocialPlayer(Player->PlayerKey, Player->FactionId, Player->bIsHost, false, GS && GS->IsFactionLeader(Player));
}

FGFSocialPlayer UGFTerritorySubsystem::SubjectFor(const AGFDino* Dino) const
{
	if (!Dino || !Dino->Taming || Dino->Taming->GetTameState() != EGFTameState::Tamed)
	{
		return FGFSocialPlayer();
	}
	const UWorld* World = GetWorld();
	const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	const AGFPlayerState* Owner = GS ? GS->FindPlayerByKey(Dino->Taming->GetOwnerKey()) : nullptr;
	return SubjectFor(Owner);
}
