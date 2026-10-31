#include "GFHubGateDirector.h"

#include "GFBeatHopCourse.h"
#include "GFLog.h"
#include "GFMineDirector.h"
#include "GFMiniGameCatalog.h"
#include "GFMiniGameGate.h"
#include "GFTerritorySubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/SoftObjectPath.h"

AGFHubGateDirector::AGFHubGateDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
}

void AGFHubGateDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	Place(GetWorld()->GetOutermost()->GetName(), RimeAnchor(GetWorld()));
}

FVector AGFHubGateDirector::RimeAnchor(const UWorld* World) const
{
	const UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(World);
	const TArray<FGFTerritoryFaction>* Layout = nullptr;
	if (Territory && Territory->FactionLayout.Num() > 0)
	{
		Layout = &Territory->FactionLayout;
	}
	else if (const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>())
	{
		Layout = &Defaults->FactionLayout;
	}
	if (Layout)
	{
		for (const FGFTerritoryFaction& Faction : *Layout)
		{
			if (Faction.Id == FName(TEXT("Rime")))
			{
				return Faction.Center;
			}
		}
	}
	return FVector::ZeroVector;
}

namespace
{
	AGFMiniGameGate* FindPlacedGate(UWorld* World, FName Id, EGFMiniGameGateRole GateRole)
	{
		for (TActorIterator<AGFMiniGameGate> It(World); It; ++It)
		{
			if (It->EntryId == Id && It->GateRole == GateRole)
			{
				return *It;
			}
		}
		return nullptr;
	}

	AGFMiniGameGate* EnsureGate(UWorld* World, const FGFMiniGameEntry& Entry, EGFMiniGameGateRole GateRole, const FVector& At, const FRotator& Facing, const FVector& Rime)
	{
		if (AGFMiniGameGate* Existing = FindPlacedGate(World, Entry.Id, GateRole))
		{
			Existing->RimeCenter = Rime;
			Existing->SetActorLocationAndRotation(At, Facing);
			Existing->ApplyVisuals();
			return Existing;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FTransform Xform(Facing, At);
		AGFMiniGameGate* Gate = World->SpawnActorDeferred<AGFMiniGameGate>(AGFMiniGameGate::StaticClass(), Xform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Gate)
		{
			return nullptr;
		}
		Gate->EntryId = Entry.Id;
		Gate->GateRole = GateRole;
		Gate->RimeCenter = Rime;
		Gate->FinishSpawning(Xform);
		// OnConstruction already applied visuals. Call again so this stays the entry point
		// even if a future spawn path finishes construction before the entry id is set.
		Gate->ApplyVisuals();
		return Gate;
	}

	AActor* EnsureClass(UWorld* World, const FString& ClassPath, const FVector& At, bool bMoveExisting)
	{
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}
		UClass* Class = FSoftClassPath(ClassPath).TryLoadClass<AActor>();
		if (!Class)
		{
			UE_LOG(LogGF, Warning, TEXT("Gate: class %s did not load"), *ClassPath);
			return nullptr;
		}
		for (TActorIterator<AActor> It(World, Class); It; ++It)
		{
			if (bMoveExisting)
			{
				It->SetActorLocation(At);
			}
			return *It;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Spawned = World->SpawnActor<AActor>(Class, FTransform(At), Params);
		if (bMoveExisting && Spawned)
		{
			Spawned->SetActorLocation(At);
		}
		return Spawned;
	}
}

void AGFHubGateDirector::Place(const FString& WorldPackage, const FVector& InRimeCenter)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	const TArray<FGFMiniGameEntry>& Entries = FGFMiniGameCatalog::All();
	const int32 RingCount = Entries.Num();
	const bool bHub = FGFMiniGameCatalog::WorldIs(WorldPackage, FGFMiniGameCatalog::HubMap());

	for (const FGFMiniGameEntry& Entry : Entries)
	{
		if (bHub)
		{
			const FVector At = FGFMiniGameCatalog::GateLocation(InRimeCenter, Entry.RingIndex, RingCount);
			const FRotator Facing = FGFMiniGameCatalog::GateFacing(At, InRimeCenter);
			EnsureGate(World, Entry, EGFMiniGameGateRole::Portal, At, Facing, InRimeCenter);
			if (Entry.bHasPad)
			{
				const FVector Pad = FGFMiniGameCatalog::PadOrigin(Entry, InRimeCenter);
				EnsureGate(World, Entry, EGFMiniGameGateRole::Marker, Pad, FRotator::ZeroRotator, InRimeCenter);
				AActor* Director = EnsureClass(World, Entry.DirectorClass, Pad, true);
				// Pebble Mine (#216): rooms and verb spots on the pad the gate already drops you on.
				if (AGFMineDirector* Mine = Cast<AGFMineDirector>(Director))
				{
					Mine->EnsurePlayableSite();
				}
			}
		}
		else if (Entry.HasMap() && FGFMiniGameCatalog::WorldIs(WorldPackage, Entry.Map))
		{
			EnsureGate(World, Entry, EGFMiniGameGateRole::Marker, FVector(-500.f, 0.f, 0.f), FRotator::ZeroRotator, InRimeCenter);
			AActor* Placed = EnsureClass(World, Entry.PlacedClass, FVector::ZeroVector, false);
			// Beat Hop (#212): the course runs as a session on its own map: lobby, start gate, results, back to the hub.
			if (AGFBeatHopCourse* Course = Cast<AGFBeatHopCourse>(Placed))
			{
				Course->BeginSession();
			}
		}
	}
}
