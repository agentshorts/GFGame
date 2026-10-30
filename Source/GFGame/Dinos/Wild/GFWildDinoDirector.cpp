#include "GFWildDinoDirector.h"
#include "GFLog.h"
#include "GFTamingComponent.h"
#include "GFTerritorySubsystem.h"
#include "GFWanderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

AGFWildDinoDirector::AGFWildDinoDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	bReplicates = false;
	SetReplicatingMovement(false);
}

FVector AGFWildDinoDirector::PickRingPoint(const FVector& Center, float MinRadius, float Radius, FRandomStream& Rng)
{
	float Inner = FMath::Max(0.f, MinRadius);
	float Outer = FMath::Max(0.f, Radius);
	if (Inner > Outer)
	{
		Swap(Inner, Outer);
	}
	const float Dist = FMath::Sqrt(FMath::Lerp(Inner * Inner, Outer * Outer, Rng.FRand()));
	const float Ang = Rng.FRand() * 2.f * PI;
	return FVector(Center.X + FMath::Cos(Ang) * Dist, Center.Y + FMath::Sin(Ang) * Dist, Center.Z);
}

void AGFWildDinoDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
		return;
	}
	Rng.Initialize(FMath::Rand() ^ 0x57D10);
	RebuildSlots();
	FillSlots();
	UE_LOG(LogGF, Log, TEXT("WildDino: %d alive, cap %d, respawn %.0fs"), CountAlive(), MaxAlive, RespawnSeconds);
}

void AGFWildDinoDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		FillSlots();
	}
}

void AGFWildDinoDirector::RebuildSlots()
{
	Slots.Reset();
	for (int32 Index = 0; Index < Spawns.Num(); ++Index)
	{
		const FGFWildDinoSpawn& Entry = Spawns[Index];
		if (Entry.Species.IsNone() || Entry.Count <= 0)
		{
			continue;
		}
		for (int32 N = 0; N < Entry.Count; ++N)
		{
			FGFWildDinoSlot Slot;
			Slot.SpawnIndex = Index;
			Slots.Add(Slot);
		}
	}
}

int32 AGFWildDinoDirector::CountAlive() const
{
	int32 Alive = 0;
	for (const FGFWildDinoSlot& Slot : Slots)
	{
		if (Slot.Dino.IsValid())
		{
			++Alive;
		}
	}
	return Alive;
}

void AGFWildDinoDirector::FillSlots()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	int32 Alive = CountAlive();
	const float Now = World->GetTimeSeconds();
	for (FGFWildDinoSlot& Slot : Slots)
	{
		if (Slot.Dino.IsValid())
		{
			continue;
		}
		if (Slot.bLived)
		{
			if (Slot.DeadAt < 0.f)
			{
				Slot.DeadAt = Now;
			}
			if (Now - Slot.DeadAt < RespawnSeconds)
			{
				continue;
			}
		}
		if (Alive >= MaxAlive)
		{
			continue;
		}
		if (!Spawns.IsValidIndex(Slot.SpawnIndex))
		{
			continue;
		}
		if (AGFDino* Dino = SpawnOne(Spawns[Slot.SpawnIndex]))
		{
			Slot.Dino = Dino;
			Slot.bLived = true;
			Slot.DeadAt = -1.f;
			++Alive;
		}
	}
}

FVector AGFWildDinoDirector::ResolveCenter(const FGFWildDinoSpawn& Entry) const
{
	const TArray<FGFTerritoryFaction>* Layout = nullptr;
	if (const UGFTerritorySubsystem* Live = UGFTerritorySubsystem::Get(this))
	{
		if (Live->FactionLayout.Num() > 0)
		{
			Layout = &Live->FactionLayout;
		}
	}
	if (!Layout)
	{
		if (const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>())
		{
			Layout = &Defaults->FactionLayout;
		}
	}
	if (Layout)
	{
		for (const FGFTerritoryFaction& Faction : *Layout)
		{
			if (Faction.Id == Entry.AnchorId)
			{
				return Faction.Center;
			}
		}
	}
	if (!Entry.AnchorId.IsNone())
	{
		UE_LOG(LogGF, Warning, TEXT("WildDino: anchor %s is not a faction; using Location"), *Entry.AnchorId.ToString());
	}
	return Entry.Location;
}

float AGFWildDinoDirector::ResolveGroundZ(const FVector& Center, float X, float Y) const
{
	const float Fallback = Center.Z + 100.f;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Fallback;
	}
	const FVector Start(X, Y, Center.Z + 2000.f);
	const FVector End(X, Y, Center.Z + 2000.f - 50000.f);
	FHitResult Hit;
	FCollisionQueryParams Query(TEXT("GFWildDinoGround"), false, this);
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Query))
	{
		return Fallback;
	}
	float Z = Hit.ImpactPoint.Z;
	if (const AGFDino* Defaults = GetDefault<AGFDino>())
	{
		if (const UCapsuleComponent* Capsule = Defaults->GetCapsuleComponent())
		{
			Z += Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	return Z;
}

AGFDino* AGFWildDinoDirector::SpawnOne(const FGFWildDinoSpawn& Entry)
{
	UWorld* World = GetWorld();
	if (!World || Entry.Species.IsNone())
	{
		return nullptr;
	}
	const FVector Center = ResolveCenter(Entry);
	FVector Point = PickRingPoint(Center, Entry.MinRadius, Entry.Radius, Rng);
	const float Yaw = (Point - Center).GetSafeNormal2D().Rotation().Yaw;
	Point.Z = ResolveGroundZ(Center, Point.X, Point.Y);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), Point, FRotator(0.f, Yaw, 0.f), Params);
	if (!Dino)
	{
		UE_LOG(LogGF, Warning, TEXT("WildDino: spawn failed for %s"), *Entry.Species.ToString());
		return nullptr;
	}
	Dino->SetSpecies(Entry.Species);
	if (Dino->Wander)
	{
		Dino->Wander->SetHome(Point);
		Dino->Wander->ApplyWalkSpeed();
	}
	if (bSpawnHostile && Dino->Taming)
	{
		Dino->Taming->ServerForceHostile(FString());
	}
	UE_LOG(LogGF, Log, TEXT("WildDino: %s at %s (anchor %s)"), *Entry.Species.ToString(), *Point.ToString(), *Entry.AnchorId.ToString());
	return Dino;
}
