#pragma once

// Mining feel on top of the integrity step. Hit shake, break hit-stop and the crosshair tick live on the character.
// This file is the shared math: crack stages, the one replicated cell, and cosmetic prediction that never breaks a node.

#include "CoreMinimal.h"
#include "GFMiningFX.h"
#include "GFMiningFeel.generated.h"

class UPrimitiveComponent;
class UStaticMeshComponent;

/** One cell the caster is aiming at. The wound map stays on the server. */
USTRUCT()
struct GFMAGIC_API FGFTargetedCellFraction
{
	GENERATED_BODY()

	UPROPERTY()
	bool bActive = false;

	/** Town mesh (Log or CrystalVein). Otherwise Cell is a voxel. */
	UPROPERTY()
	bool bTown = false;

	UPROPERTY()
	FIntVector Cell = FIntVector::ZeroValue;

	UPROPERTY()
	FString NodeKey;

	/** Remaining integrity over the full block. 1 is untouched, 0 is spent. */
	UPROPERTY()
	float Fraction = 1.f;

	/** Bumps when the payload changes, so a client can tell a real publish from the default. */
	UPROPERTY()
	int32 Serial = 0;

	bool Matches(const FGFTargetedCellFraction& Other) const;
};

/** 75, 50 and 25 percent remaining. Stage 0 is intact or already broken. */
struct GFMAGIC_API FGFCrackParams
{
	float Crack75 = 0.f;
	float Crack50 = 0.f;
	float Crack25 = 0.f;
	int32 Stage = 0;

	static FGFCrackParams FromFraction(float Fraction);
};

/**
 * Writes the three crack params. Custom primitive data slots 0, 1 and 2 are Crack75, Crack50 and Crack25.
 * A dynamic material already on the mesh also gets the scalar names. Materials are not replaced.
 */
struct GFMAGIC_API FGFMiningCrack
{
	static constexpr int32 Index75 = 0;
	static constexpr int32 Index50 = 1;
	static constexpr int32 Index25 = 2;

	static FName Param75() { return TEXT("Crack75"); }
	static FName Param50() { return TEXT("Crack50"); }
	static FName Param25() { return TEXT("Crack25"); }

	static void Apply(UPrimitiveComponent* Prim, float Fraction);
	static void Clear(UPrimitiveComponent* Prim);
	static bool Read(const UPrimitiveComponent* Prim, FGFCrackParams& Out);
};

/** What a hit, crack or break should do on the owning client. A beam does nothing. */
struct GFMAGIC_API FGFMiningFeelResponse
{
	bool bShake = false;
	bool bCrosshair = false;
	bool bHitStop = false;
};

struct GFMAGIC_API FGFMiningFeelRules
{
	static constexpr float DefaultFieldRiseSeconds = 0.15f;
	static constexpr float DefaultBreakHitStopSeconds = 0.05f;
	static constexpr float MinBreakHitStopSeconds = 0.04f;
	static constexpr float MaxBreakHitStopSeconds = 0.06f;
	static constexpr float DefaultHitShakeSeconds = 0.12f;
	static constexpr float DefaultHitShakeCm = 1.6f;
	static constexpr float DefaultCrosshairTickSeconds = 0.14f;
	static constexpr float DefaultPredictTolerance = 0.2f;
	/** Prediction stops here so the client never spends the node. The server owns the break. */
	static constexpr float PredictFloor = 0.02f;

	static bool FieldRises(float HeldSeconds, float Threshold);
	static float ClampHitStop(float Seconds);
	/** Fraction of a full block one step removes. 0 when the element does not bite. */
	static float FractionLoss(FName BlockId, EGFMagicElement Element, float IntegrityPerSecond, float Rate, float Dt);
	static FGFMiningFeelResponse ResponseFor(EGFMiningFXKind Kind);
};

/**
 * Owning-client crack guess. Reconcile pulls it back when the server fraction is higher than the lead allows.
 * Shown() never means the node broke: a server clear returns the guess to 1, and the mesh hide stays on the relay.
 */
struct GFMAGIC_API FGFCrackClient
{
	void BeginTarget(bool bInTown, const FIntVector& InCell, const FString& InKey);
	void PredictStep(float FractionLoss);
	void Reconcile(const FGFTargetedCellFraction& Server, float Tolerance);
	float Shown() const { return Predicted; }
	bool RolledBack() const { return bRolledBack; }

	float Predicted = 1.f;
	bool bTown = false;
	bool bHasTarget = false;
	bool bRolledBack = false;
	FIntVector Cell = FIntVector::ZeroValue;
	FString NodeKey;
};

/** Local plate on the hit. No collision, not replicated. The integrity step does not spawn this. */
UCLASS()
class GFMAGIC_API AGFMiningCrackMarker : public AActor
{
	GENERATED_BODY()

public:
	AGFMiningCrackMarker();

	void Show(const FVector& At, const FVector& Normal, float Fraction);
	void Hide();
	UStaticMeshComponent* GetPlate() const;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Plate;
};
