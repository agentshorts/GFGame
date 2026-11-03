#pragma once

// Server mining step. Ten times a second, 64 cells across every caster. Clients watch the voxels move.
// Town meshes (trees and crystal slots) take the same spells here: see FGFTownNodeField.

#include "CoreMinimal.h"
#include "GFMagicComponent.h"
#include "GFMagicShapes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFBlockIntegritySubsystem.generated.h"

class AGFVoxelWorld;
class APawn;
class AGFPlayerState;
class UPrimitiveComponent;
class FGFTownNodeField;

UCLASS()
class GFMAGIC_API UGFBlockIntegritySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UGFBlockIntegritySubsystem();
	virtual ~UGFBlockIntegritySubsystem();

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

	/**
	 * A mining spell that is actually casting. The game turns the cone into ApplyDamage on dinos.
	 * Empty until the game mode binds one. Voxel damage does not wait on it.
	 */
	void SetDinoHit(TFunction<void(UGFMagicComponent* Caster, const FVector& Origin, const FVector& Dir, float Range, float HalfAngleDeg, float Damage)> InFn);

	/** Whole 0.1 s steps covering Seconds. No hitch cap, so a test can mine a dirt cell in one call. */
	void Advance(float Seconds);

	/** Log integrity (100) or CrystalVein integrity (200) before a hit, the wound after, 0 when spent, -1 if the mesh is not mineable. */
	float GetTownNodeRemaining(const UPrimitiveComponent* Component) const;
	bool IsTownNodeDepleted(const UPrimitiveComponent* Component) const;
	float GetTownRegrowSeconds() const;
	void SetTownRegrowSeconds(float Seconds);

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
	/** Crystal cost and the beam, used when the world has no voxel grid. With a grid, CastField already paid. */
	void PayTownField(UGFMagicComponent* Caster);
	void OfferDinoHit(UGFMagicComponent* Caster, const FVector& Origin, const FVector& Dir, float Range, float HalfAngleDeg, float Damage);
	void TryFieldDinoHit(UGFMagicComponent* Caster);

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
	TFunction<void(UGFMagicComponent* Caster, const FVector& Origin, const FVector& Dir, float Range, float HalfAngleDeg, float Damage)> DinoHitFn;
	TUniquePtr<FGFTownNodeField> TownNodes;
};
