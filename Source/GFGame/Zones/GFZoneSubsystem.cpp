#include "GFZoneSubsystem.h"

#include "GFMiniGameGateSubsystem.h"
#include "GFPlayerState.h"
#include "GFTerritorySubsystem.h"
#include "GFZoneCatalog.h"
#include "GFZoneLayout.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Stats/Stats.h"

void UGFZoneSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	BindGates();
}

void UGFZoneSubsystem::BindGates()
{
	if (bGatesBound)
	{
		return;
	}
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (!GI)
	{
		return;
	}
	bGatesBound = true;
	for (const FGFRegionSlot& Slot : FGFZoneLayout::Regions())
	{
		if (UGFMiniGameGateSubsystem* Gate = FGFZoneLayout::FindLiveGate(GI, Slot.GateId))
		{
			Gate->OnMiniGameFinished.AddUniqueDynamic(this, &UGFZoneSubsystem::HandleMiniGameFinished);
		}
	}
}

void UGFZoneSubsystem::HandleMiniGameFinished(FName GateId, const FGFMiniGameResult& Result)
{
	(void)Result;
	const FGFRegionSlot* Slot = FGFZoneLayout::FindByGate(GateId);
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!Slot || !GS || !World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	for (APlayerState* Other : GS->PlayerArray)
	{
		if (AGFPlayerState* PS = Cast<AGFPlayerState>(Other))
		{
			PS->UnlockShardstone(Slot->StoneId);
		}
	}
}

TStatId UGFZoneSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGFZoneSubsystem, STATGROUP_Tickables);
}

bool UGFZoneSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UGFZoneSubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->GetAuthGameMode() != nullptr;
}

FGFTerritoryZoneHit UGFZoneSubsystem::TerritoryAt(const FVector& Location) const
{
	if (UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(this))
	{
		return Territory->GetZoneAt(Location);
	}
	FGFTerritoryZoneHit Wild;
	Wild.Zone = EGFTerritoryZone::Wild;
	return Wild;
}

FGFZoneHit UGFZoneSubsystem::ResolveAt(const FVector& Location) const
{
	const FGFTerritoryZoneHit Territory = TerritoryAt(Location);
	return FGFZoneCatalog::Get().ResolveAt(Location, &Territory);
}

int32 UGFZoneSubsystem::GetDangerTierAt(const FVector& Location) const
{
	const FGFTerritoryZoneHit Territory = TerritoryAt(Location);
	return FGFZoneCatalog::Get().DangerTierAt(Location, &Territory);
}

bool UGFZoneSubsystem::IsPvPAllowedAt(const FVector& Location) const
{
	const FGFTerritoryZoneHit Territory = TerritoryAt(Location);
	return FGFZoneCatalog::Get().IsPvPAllowedAt(Location, &Territory);
}

void UGFZoneSubsystem::TrackPlayer(AGFPlayerState* PS)
{
	const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	if (!PS || !Pawn)
	{
		return;
	}
	const FVector At = Pawn->GetActorLocation();
	const FGFTerritoryZoneHit Territory = TerritoryAt(At);
	const UGFTerritorySubsystem* TerritorySys = UGFTerritorySubsystem::Get(this);
	const FGFTerritoryMap* Map = TerritorySys ? &TerritorySys->GetMap() : nullptr;
	const FGFZoneCatalog& Catalog = FGFZoneCatalog::Get();
	const FName Next = Catalog.Advance(PS->GetCurrentZoneId(), At, &Territory, Map);
	if (Next != PS->GetCurrentZoneId())
	{
		const FString Toast = Catalog.ToastFor(PS->GetCurrentZoneId(), Next, Map);
		PS->SetCurrentZoneId(Next);
		if (!Toast.IsEmpty())
		{
			PS->NotifyLand(Toast);
		}
	}
	for (const FGFShardstoneRow& Stone : Catalog.Stones)
	{
		if (FVector::Dist2D(Stone.Location, At) <= FGFZoneConstants::UnlockRadiusCm)
		{
			PS->UnlockShardstone(Stone.Id);
		}
	}
}

void UGFZoneSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	BindGates();
	Accumulator += DeltaSeconds;
	if (Accumulator < FGFZoneConstants::ZoneTickSeconds)
	{
		return;
	}
	Accumulator = 0.f;
	const AGameStateBase* GS = World->GetGameState();
	if (!GS)
	{
		return;
	}
	for (APlayerState* Other : GS->PlayerArray)
	{
		TrackPlayer(Cast<AGFPlayerState>(Other));
	}
}
