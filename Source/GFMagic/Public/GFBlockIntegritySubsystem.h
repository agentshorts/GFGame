#pragma once

// Server mining step. Ten times a second, 64 cells across every caster. Clients watch the voxels move.

#include "CoreMinimal.h"
#include "GFMagicComponent.h"
#include "GFMagicShapes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFBlockIntegritySubsystem.generated.h"

class AGFVoxelWorld;
class APawn;
class AGFPlayerState;

UCLASS()
class GFMAGIC_API UGFBlockIntegritySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	void NotePlaced(const FIntVector& Cell);
	void NoteCleared(const FIntVector& Cell);
	void NotePlacedRegion(const FIntVector& Min, const FIntVector& Max);
	bool IsPlaced(const FIntVector& Cell) const;
	bool IsSpellProof(FName BlockId) const;

	/** Cuts the claw tunnel now. Whatever does not fit in this tick's budget continues next tick. */
	void CastClaw(UGFMagicComponent* Caster);

	/** Stairwell Call asks the game for a player-scale dig. Empty until the game mode binds one. */
	void SetStairwell(TFunction<TArray<FIntVector>(const APawn*, const FVector&, const AGFVoxelWorld*)> InFn);

	/** Whole 0.1 s steps covering Seconds. No hitch cap, so a test can mine a dirt cell in one call. */
	void Advance(float Seconds);

private:
	void EnsureBound();
	void Step();
	AGFVoxelWorld* FindVoxel() const;
	TArray<UGFMagicComponent*> Casters() const;
	UGFMagicComponent* FindCaster(const FString& Key) const;
	void ApplyCast(UGFMagicComponent* Caster, FMagicDeferredCast Deferred, int32& Budget, AGFVoxelWorld* Voxel);
	void Commit(AGFVoxelWorld* Voxel, const FMagicHitOutput& Out);
	void ApplySwings(UGFMagicComponent* Caster, int32& Budget, AGFVoxelWorld* Voxel);
	void CastField(UGFMagicComponent* Caster, int32& Budget, AGFVoxelWorld* Voxel, TArray<FIntVector>& Detonate);
	void SpawnGlobs(UGFMagicComponent* Caster, const FGFMiningSpellRow& Spell, AGFVoxelWorld* Voxel);
	void StepGlobs(AGFVoxelWorld* Voxel);
	void StepPuddlesAndSmears(int32& Budget, AGFVoxelWorld* Voxel);
	void StepFreeze(int32& Budget, AGFVoxelWorld* Voxel, const TArray<FIntVector>& Detonate, UGFMagicComponent* BeamCaster, const TArray<FIntVector>& Beam, const FIntVector& AimCell);
	FMagicWorldView MakeView(AGFPlayerState* PS, AGFVoxelWorld* Voxel) const;
	void RememberMark(const FIntVector& Cell, FName Mark, float Seconds);
	FName ActiveShape(const UGFMagicComponent* Caster) const;

	TSet<FIntVector> Placed;
	TMap<FIntVector, FMagicWound> Wounds;
	TArray<FMagicGlob> Globs;
	TArray<FMagicPuddle> Puddles;
	TArray<FMagicSmear> Smears;
	TMap<FIntVector, FMagicFreezeCell> Freeze;
	TMap<int32, int32> FreezeChains;
	TMap<FString, float> GlobFraction;
	TMap<FIntVector, FMagicMark> Marks;
	int32 NextChainId = 1;
	double MiningNow = 0.0;
	float Accumulator = 0.f;
	bool bBound = false;
	FDelegateHandle EditHandle;
	FDelegateHandle StampHandle;
	TFunction<TArray<FIntVector>(const APawn*, const FVector&, const AGFVoxelWorld*)> StairwellFn;
};
