#include "GFTerritorySubsystem.h"
#include "GFDino.h"
#include "GFGameState.h"
#include "GFSafeZone.h"
#include "GFSocialComponent.h"
#include "GFSocialDirector.h"
#include "GFTamingComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarGFTerritoryDrawHazards(
	TEXT("gf.Territory.DrawHazards"),
	0,
	TEXT("Draw territory hazard boxes. 0 is off. Queries ignore Z; the drawn box uses a fixed vertical extent."));

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
	DrawHazards();
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
	Map.Hazards = Hazards;
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
	BindAccess();
}

void UGFTerritorySubsystem::BindAccess()
{
	if (!Map.AccessSource)
	{
		// Live read: a toggle on the server (or its replication on a client) counts on the very next query.
		TWeakObjectPtr<const UGFSocialComponent>* Source = &Social;
		Map.AccessSource = [Source]() -> const FGFFactionAccess* { return Source->IsValid() ? &Source->Get()->Access : nullptr; };
	}
	if (!Social.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			Social = AGFSocialDirector::GetSocial(World, false);
		}
	}
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

FGFTerritoryHazardHit UGFTerritorySubsystem::GetHazardAt(FVector Location)
{
	RefreshFromWorld();
	return Map.GetHazardAt(Location);
}

void UGFTerritorySubsystem::DrawHazards() const
{
	if (CVarGFTerritoryDrawHazards.GetValueOnGameThread() == 0)
	{
		return;
	}
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Vertical extent is visual only. GetHazardAt ignores Z.
	const float VisualHalfZ = 100.f;
	for (const FGFTerritoryHazard& Hazard : Map.Hazards)
	{
		const FVector Extent(FMath::Abs(Hazard.HalfExtent.X), FMath::Abs(Hazard.HalfExtent.Y), VisualHalfZ);
		DrawDebugBox(World, Hazard.Center, Extent, FColor::Orange, false, 0.f, 0, 2.f);
		DrawDebugString(World, Hazard.Center + FVector(0.f, 0.f, VisualHalfZ), Hazard.Id.ToString(), nullptr, FColor::Orange, 0.f, false, 1.f);
	}
#endif
}

bool UGFTerritorySubsystem::IsSafeFor(const AGFPlayerState* Player, FVector Location)
{
	RefreshFromWorld();
	const FGFSocialPlayer Who = SubjectFor(Player);
	return Map.IsSafeFor(FGFSafeSubject(Who.Key, Who.Faction, Who.bHost), Location);
}

bool UGFTerritorySubsystem::IsSafeForDino(const AGFDino* Dino, const FVector& Location)
{
	RefreshFromWorld();
	return Map.IsSafeFor(SubjectFor(Dino).Faction, Location);
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
