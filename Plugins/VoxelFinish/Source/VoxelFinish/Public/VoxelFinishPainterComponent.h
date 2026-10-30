#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelFinishRateLimiter.h"
#include "VoxelFinishTypes.h"
#include "VoxelFinishPainterComponent.generated.h"

class AVoxelFinishVolume;
class APawn;
class APlayerState;
enum class EVoxelFinishPaintResult : uint8;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVoxelFinishPaintRejected, FIntVector /*Cell*/, uint8 /*EVoxelFinishPaintResult*/);

/**
 * Player-owned paint entry point. Add it to the PlayerController (or the PlayerState or possessed Pawn).
 * Server RPCs only run on actors owned by the calling connection, so the volume itself has no RPC.
 * The client never names the payer: the server derives it from this component's owner.
 * Server checks, in order: rate limit, volume, payer, target block (AVoxelFinishVolume::SetPaintTargetResolver),
 * reach, then the volume's own checks and the materials hook. Every request counts against the rate limit.
 * Reach is read at paint time: the game can supply it live with SetMaxReachProvider, otherwise MaxReachCm is used.
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

	/** Pawn the reach check measures from: the owner itself, a controller's pawn, or a PlayerState's pawn. */
	APawn* ResolvePawn() const;

	/**
	 * Server: farthest distance (cm) from the pawn's location to the centre of the painted block. 0 = no limit.
	 * Ignored while a reach provider is bound (SetMaxReachProvider).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelFinish|Server", meta = (ClampMin = "0"))
	float MaxReachCm = 600.f;

	/**
	 * Server: supplies the reach (cm) live, read at each paint, so the game can keep it in one place
	 * (e.g. a game-state setting that changes during play). A value <= 0 means no limit. Pass an empty function to unbind.
	 */
	void SetMaxReachProvider(TFunction<float()> InProvider);

	/** The reach the next paint is checked against: the provider's value if bound, else MaxReachCm. <= 0 = no limit. */
	float GetEffectiveMaxReachCm() const;

	/** Server: paint requests accepted per second from this player, with bursts of up to the same number. 0 = no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelFinish|Server", meta = (ClampMin = "0"))
	float MaxPaintsPerSecond = 10.f;

	/**
	 * Fires on the owning client when the server rejects a paint. Rate-limited requests are dropped quietly
	 * (no RPC back), so a flooding client cannot make the server answer every extra request.
	 */
	FOnVoxelFinishPaintRejected OnPaintRejected;

	/** Server: how many of this player's requests the rate limit has dropped. */
	int32 GetNumRateLimited() const { return NumRateLimited; }

protected:
	UFUNCTION(Server, Reliable)
	void ServerRequestPaint(FIntVector Cell, FFinish Finish);

	UFUNCTION(Client, Reliable)
	void ClientPaintRejected(FIntVector Cell, uint8 Result);

private:
	void AuthorityHandlePaint(const FIntVector& Cell, const FFinish& Finish);
	EVoxelFinishPaintResult AuthorityCheckAndPaint(const FIntVector& Cell, const FFinish& Finish);
	AVoxelFinishVolume* ResolveVolume();

	FVoxelFinishRateLimiter RateLimiter;
	TFunction<float()> MaxReachProvider;
	int32 NumRateLimited = 0;

	UPROPERTY(Transient)
	TWeakObjectPtr<AVoxelFinishVolume> Volume;
};
