#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelFinishTypes.h"
#include "VoxelFinishPainterComponent.generated.h"

class AVoxelFinishVolume;
class APlayerState;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVoxelFinishPaintRejected, FIntVector /*Cell*/, uint8 /*EVoxelFinishPaintResult*/);

/**
 * Player-owned paint entry point. Add it to the PlayerController (or the PlayerState or possessed Pawn).
 * Server RPCs only run on actors owned by the calling connection, so the volume itself has no RPC.
 * The client never names the payer: the server derives it from this component's owner.
 */
UCLASS(ClassGroup = (VoxelFinish), meta = (BlueprintSpawnableComponent))
class VOXELFINISH_API UVoxelFinishPainterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoxelFinishPainterComponent();

	/** Sends the paint to the server (or applies it directly when the owner is the authority). */
	UFUNCTION(BlueprintCallable, Category = "VoxelFinish")
	void RequestPaint(FIntVector Cell, const FFinish& Finish);

	/** Optional. Without it the first AVoxelFinishVolume in the world is used. */
	void SetVolume(AVoxelFinishVolume* InVolume);

	/** PlayerState of the owner: the owner itself, a controller's PlayerState, or a pawn's PlayerState. */
	APlayerState* ResolvePayer() const;

	/** Fires on the owning client when the server rejects a paint. */
	FOnVoxelFinishPaintRejected OnPaintRejected;

protected:
	UFUNCTION(Server, Reliable)
	void ServerRequestPaint(FIntVector Cell, FFinish Finish);

	UFUNCTION(Client, Reliable)
	void ClientPaintRejected(FIntVector Cell, uint8 Result);

private:
	void AuthorityHandlePaint(const FIntVector& Cell, const FFinish& Finish);
	AVoxelFinishVolume* ResolveVolume();

	UPROPERTY(Transient)
	TWeakObjectPtr<AVoxelFinishVolume> Volume;
};
