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
	APlayerState* Payer = ResolvePayer();
	AVoxelFinishVolume* Target = ResolveVolume();
	EVoxelFinishPaintResult Result = EVoxelFinishPaintResult::RejectedNoPayer;
	if (!Target)
	{
		Result = EVoxelFinishPaintResult::RejectedNoVolume;
	}
	else if (Payer)
	{
		Result = Target->AuthorityPaint(Cell, Finish, Payer);
	}
	if (Result != EVoxelFinishPaintResult::Applied && Result != EVoxelFinishPaintResult::AlreadyPainted)
	{
		ClientPaintRejected(Cell, static_cast<uint8>(Result));
	}
}

void UVoxelFinishPainterComponent::ClientPaintRejected_Implementation(FIntVector Cell, uint8 Result)
{
	OnPaintRejected.Broadcast(Cell, Result);
}
