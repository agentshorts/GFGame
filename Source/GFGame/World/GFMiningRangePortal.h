#pragma once

// Hub entrance to the mining range. This is not a catalog gate, so the hub ring stays at eight.
// The director also owns the range map: it spawns AGFMiningRange there and stays quiet everywhere else.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFModeTypes.h"
#include "GFMiningRangePortal.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Opens a map. A bound override is used by tests instead of ServerTravel / OpenLevel. */
GFGAME_API void GFTravelToMap(UWorld* World, const FString& Map, const TFunction<void(const FString&)>& Override);

UCLASS()
class GFGAME_API AGFMiningRangePortal : public AActor
{
	GENERATED_BODY()

public:
	AGFMiningRangePortal();

	virtual void BeginPlay() override;

	void ApplyVisuals();
	FText GetSignText() const { return SignText; }

	/** When set, overlap calls this instead of travelling. */
	TFunction<void(const FString& Map)> TravelOverride;

private:
	UFUNCTION()
	void OnTriggerBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> LabelText;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Trigger;

	FText SignText;
	bool bVisuals = false;
};

UCLASS()
class GFGAME_API AGFMiningRangeDirector : public AActor, public IGFModeHandler
{
	GENERATED_BODY()

public:
	AGFMiningRangeDirector();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual FName GetModeName() const override { return TEXT("MiningRange"); }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual FName EditBlockReason(const AGFPlayerState* By, const FVector& Location) const override { return NAME_None; }

	/** Hub: one signed portal, off the ring. Range map: the range actor. Anything else: nothing. */
	void Place(const FString& WorldPackage, const FVector& RimeCenter);

	FVector RimeAnchor(const UWorld* World) const;

private:
	bool bRegistered = false;
};
