#pragma once

// Greybox stand at one ferry stop. Walk onto a pad to pay and fly, or to buy the lifetime pass.
// The bird itself stays the parked AGFFerryDino. This actor does not follow the player and does not take hits.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFFerryRules.h"
#include "GFFerryStand.generated.h"

class AGFFerryDirector;
class APawn;
class AGFPlayerState;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;

/**
 * Pad positions are local to the stop, in centimetres.
 * Destination pads step down -Y. The pass pad sits on +Y.
 * Both stay inside the board disc. Drops land about half a board-radius along the route (mostly X),
 * so they miss these pads and do not immediately board a return trip.
 */
struct GFGAME_API FGFFerryStandLayout
{
	static constexpr float ExtentXY = 60.f;
	static constexpr float ExtentZ = 140.f;
	static constexpr float Ring = 220.f;
	static constexpr float DestPitch = 140.f;

	static FVector PassLocal() { return FVector(0.f, Ring, 100.f); }
	static FVector DestLocal(int32 Index)
	{
		return FVector(0.f, -Ring - static_cast<float>(Index) * DestPitch, 100.f);
	}
	static FVector Extent() { return FVector(ExtentXY, ExtentXY, ExtentZ); }

	static bool ContainsHorizontal(const FVector& PadLocal, const FVector& PointLocal)
	{
		return FMath::Abs(PointLocal.X - PadLocal.X) <= ExtentXY
			&& FMath::Abs(PointLocal.Y - PadLocal.Y) <= ExtentXY;
	}

	static bool InsideBoard(const FVector& PadLocal, float BoardRadius)
	{
		return FVector::Dist2D(PadLocal, FVector::ZeroVector) <= FMath::Max(0.f, BoardRadius);
	}
};

UCLASS()
class GFGAME_API AGFFerryStand : public AActor
{
	GENERATED_BODY()

public:
	AGFFerryStand();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server. Copies the stop and its destinations, then builds the pads. */
	void Configure(AGFFerryDirector* InDirector, FName InStopId);

	static AGFFerryStand* Find(const UWorld* World, FName InStopId);

	FName GetStopId() const { return StopId; }
	FName GetAnchorId() const { return AnchorId; }
	const TArray<FName>& GetDestinationIds() const { return DestinationIds; }

	bool IsPlayerNear(const APawn* Pawn) const;

	/** Server. The pad calls this. Charges the fare (or accepts a pass) and starts the flight. */
	FGFFerryResult TryDepart(APawn* Rider, FName ToStop);

	/** Server. The pass pad calls this. Burns the taming material and mints one pass. */
	FGFFerryResult TryBuyPass(APawn* Rider);

	UBoxComponent* GetFlyTrigger(FName ToStop) const;
	UBoxComponent* GetPassTrigger() const;

	void GetPromptLines(TArray<FString>& OutLines) const;

	/** HUD radius (cm). A little wider than the board disc so the sign shows as you walk up. */
	static constexpr float NearRadiusCm = 500.f;

	UPROPERTY(Replicated)
	FString LastPrompt;

protected:
	UFUNCTION()
	void OnRep_Stop();

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(ReplicatedUsing = OnRep_Stop)
	FName StopId;

	UPROPERTY(Replicated)
	FName AnchorId;

	UPROPERTY(Replicated)
	TArray<FName> DestinationIds;

	UPROPERTY(Replicated)
	TArray<FName> DestinationAnchors;

	TWeakObjectPtr<AGFFerryDirector> Director;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> Triggers;

	TArray<FName> TriggerDests;
	bool bVisualsBuilt = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	/** Pawns already handled for this stay, one set per trigger. */
	TArray<TSet<TWeakObjectPtr<APawn>>> SpotInside;

	void BuildIfNeeded();
	void BuildVisuals(const TArray<FName>& Ids, const TArray<FName>& Anchors, FName Anchor);
	void PollPads();
	void AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock);
	void AddLabel(const FString& Text, const FVector& Local);
	UBoxComponent* AddTrigger(FName ToStop, const FVector& Local);
	AGFFerryDirector* ResolveDirector();
	const FGFFerryTuning& Tuning() const;
	bool PawnInPad(const APawn* Pawn, const UBoxComponent* Box) const;
	void Note(const FString& What, const FGFFerryResult& Result);
};
