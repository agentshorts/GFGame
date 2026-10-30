#pragma once

// Server resolves each pawn at 5 Hz, with 2 m of hysteresis, and writes CurrentZoneId.
// Clients may call ResolveAt for cosmetics. Faction land still wins inside the ring.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFZoneTypes.h"
#include "GFZoneSubsystem.generated.h"

class AGFPlayerState;
struct FGFTerritoryZoneHit;

UCLASS()
class GFGAME_API UGFZoneSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	FGFZoneHit ResolveAt(const FVector& Location) const;
	int32 GetDangerTierAt(const FVector& Location) const;
	bool IsPvPAllowedAt(const FVector& Location) const;

	UFUNCTION()
	void HandleMiniGameFinished(FName GateId, const FGFMiniGameResult& Result);

private:
	FGFTerritoryZoneHit TerritoryAt(const FVector& Location) const;
	void TrackPlayer(AGFPlayerState* PS);
	void BindGates();

	float Accumulator = 0.f;
	bool bGatesBound = false;
};
