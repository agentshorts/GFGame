#include "VoxelFinishPainterComponent.h"

#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "VoxelFinishVolume.h"

UVoxelFinishPainterComponent::UVoxelFinishPainterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UVoxelFinishPainterComponent::SetVolume(AVoxelFinishVolume* InVolume)
{
	Volume = InVolume;
}

APlayerState* UVoxelFinishPainterComponent::ResolvePayer() const
{
	AActor* Owner = GetOwner();
	if (APlayerState* State = Cast<APlayerState>(Owner))
	{
		return State;
	}
	if (const AController* Controller = Cast<AController>(Owner))
	{
		return Controller->PlayerState;
	}
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		return Pawn->GetPlayerState();
	}
	return nullptr;
}

APawn* UVoxelFinishPainterComponent::ResolvePawn() const
{
	AActor* Owner = GetOwner();
	if (APawn* Pawn = Cast<APawn>(Owner))
	{
		return Pawn;
	}
	if (const AController* Controller = Cast<AController>(Owner))
	{
		return Controller->GetPawn();
	}
	if (const APlayerState* State = Cast<APlayerState>(Owner))
	{
		return State->GetPawn();
	}
	return nullptr;
}

AVoxelFinishVolume* UVoxelFinishPainterComponent::ResolveVolume()
{
	if (AVoxelFinishVolume* Cached = Volume.Get())
	{
		return Cached;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AVoxelFinishVolume> It(World); It; ++It)
	{
		Volume = *It;
		return *It;
	}
	return nullptr;
}

void UVoxelFinishPainterComponent::RequestPaint(FIntVector Cell, const FFinish& Finish)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		AuthorityHandlePaint(Cell, Finish);
		return;
	}
	ServerRequestPaint(Cell, Finish);
}

void UVoxelFinishPainterComponent::ServerRequestPaint_Implementation(FIntVector Cell, FFinish Finish)
{
	AuthorityHandlePaint(Cell, Finish);
}

void UVoxelFinishPainterComponent::AuthorityHandlePaint(const FIntVector& Cell, const FFinish& Finish)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	const EVoxelFinishPaintResult Result = AuthorityCheckAndPaint(Cell, Finish);
	if (Result != EVoxelFinishPaintResult::Applied && Result != EVoxelFinishPaintResult::AlreadyPainted)
	{
		ClientPaintRejected(Cell, static_cast<uint8>(Result));
	}
}

EVoxelFinishPaintResult UVoxelFinishPainterComponent::AuthorityCheckAndPaint(const FIntVector& Cell, const FFinish& Finish)
{
	const UWorld* World = GetWorld();
	// Real time: pausing or slowing the game does not change how fast a player may send requests.
	const double Now = World ? World->GetRealTimeSeconds() : FPlatformTime::Seconds();
	if (!RateLimiter.TryConsume(Now, MaxPaintsPerSecond))
	{
		return EVoxelFinishPaintResult::RejectedRateLimited;
	}
	AVoxelFinishVolume* Target = ResolveVolume();
	if (!Target)
	{
		return EVoxelFinishPaintResult::RejectedNoVolume;
	}
	APlayerState* Payer = ResolvePayer();
	if (!Payer)
	{
		return EVoxelFinishPaintResult::RejectedNoPayer;
	}
	FVector Center = FVector::ZeroVector;
	if (!Target->ResolvePaintTarget(Cell, Center))
	{
		return EVoxelFinishPaintResult::RejectedNoBlock;
	}
	const APawn* Pawn = ResolvePawn();
	if (!Pawn || FVector::DistSquared(Pawn->GetActorLocation(), Center) > FMath::Square(static_cast<double>(MaxReachCm)))
	{
		return EVoxelFinishPaintResult::RejectedOutOfReach;
	}
	return Target->AuthorityPaint(Cell, Finish, Payer);
}

void UVoxelFinishPainterComponent::ClientPaintRejected_Implementation(FIntVector Cell, uint8 Result)
{
	OnPaintRejected.Broadcast(Cell, Result);
}
