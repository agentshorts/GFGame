#include "GFWildDinoDirector.h"
#include "GFLog.h"
#include "GFTamingComponent.h"
#include "GFTerritorySubsystem.h"
#include "GFWanderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace GFWildDinoPrivate
{
	const FName LavaBridge(TEXT("LavaBridge"));
	constexpr int32 SpawnAttempts = 32;

	float DistanceToHazardBox(const FGFTerritoryHazard& Hazard, const FVector& Point)
	{
		const float HalfX = FMath::Abs(Hazard.HalfExtent.X);
		const float HalfY = FMath::Abs(Hazard.HalfExtent.Y);
		// Center and Point are doubles. A float 0.f does not match Max3's single type parameter.
		const float DX = FMath::Max3(Hazard.Center.X - HalfX - Point.X, 0.0, Point.X - (Hazard.Center.X + HalfX));
		const float DY = FMath::Max3(Hazard.Center.Y - HalfY - Point.Y, 0.0, Point.Y - (Hazard.Center.Y + HalfY));
		return FMath::Sqrt(DX * DX + DY * DY);
	}

	bool IsWildLavaBridge(UGFTerritorySubsystem* Territory, const FVector& Point)
	{
		if (!Territory)
		{
			return false;
		}
		const FGFTerritoryHazardHit Hit = Territory->GetHazardAt(Point);
		return Hit.bHazard && Hit.Id == LavaBridge;
	}

	// True when an idle wander disc around Point can step onto the lava box. Owned land is not the hazard.
	bool WanderCanEnterLava(UGFTerritorySubsystem* Territory, const FVector& Point, float Margin)
	{
		if (!Territory || Margin <= 0.f)
		{
			return false;
		}
		for (const FGFTerritoryHazard& Hazard : Territory->Hazards)
		{
			if (Hazard.Id == LavaBridge && DistanceToHazardBox(Hazard, Point) <= Margin)
			{
				return true;
			}
		}
		return false;
	}
}

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
	// The ferry director spawns the LavaBridge mobs. A camp ring that overlaps that box, or whose
	// wander disc would walk onto it, is retried. A zero-radius fallback is refused only when the
	// point itself is wild lava, so a placed Location outside the box still spawns.
	UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(this);
	const float WanderMargin = GetDefault<UGFWanderComponent>() ? GetDefault<UGFWanderComponent>()->WanderRadius : 0.f;
	const bool bRing = FMath::Max(Entry.MinRadius, Entry.Radius) > KINDA_SMALL_NUMBER;
	FVector Point = FVector::ZeroVector;
	bool bAccepted = false;
	for (int32 Attempt = 0; Attempt < GFWildDinoPrivate::SpawnAttempts; ++Attempt)
	{
		Point = PickRingPoint(Center, Entry.MinRadius, Entry.Radius, Rng);
		if (GFWildDinoPrivate::IsWildLavaBridge(Territory, Point))
		{
			continue;
		}
		if (bRing && GFWildDinoPrivate::WanderCanEnterLava(Territory, Point, WanderMargin))
		{
			continue;
		}
		bAccepted = true;
		break;
	}
	if (!bAccepted)
	{
		UE_LOG(LogGF, Warning, TEXT("WildDino: %s stayed on LavaBridge; the ferry spawner owns that hazard"), *Entry.Species.ToString());
		return nullptr;
	}
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
