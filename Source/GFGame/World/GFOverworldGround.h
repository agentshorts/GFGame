// Placeholder floor under the faction layout. Spawned with the game state.
// Reads UGFTerritorySubsystem live, or its CDO when the subsystem is missing.
// BuildShapes still returns one slab, one disc per faction at the largest
// RadiusByMembers entry, and one box per hazard. Tops sit just under Z=0.
// Drawing is narrower: L_Hub draws none of them (the voxel world is the floor,
// and the map already has the hazard props). Other maps draw the slab and the
// discs, and never the hazard boxes.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFTerritoryTypes.h"

class UWorld;

#include "GFOverworldGround.generated.h"

/** Config tint. Id is a faction id or a hazard id. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFOverworldTint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|World")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|World")
	FLinearColor Color = FLinearColor::White;
};

enum class EGFOverworldShapeKind : uint8
{
	Slab,
	FactionDisc,
	HazardBox
};

/** One floor piece. Positions are world centimetres. Scale is for a 100 cm engine basic shape. */
struct GFGAME_API FGFOverworldShape
{
	EGFOverworldShapeKind Kind = EGFOverworldShapeKind::Slab;
	FName Id;
	FVector Center = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
	/** Horizontal half-size of a slab or hazard box. */
	FVector2D HalfExtent = FVector2D::ZeroVector;
	/** Horizontal radius of a faction disc. */
	float DiscRadius = 0.f;
	float TopZ = 0.f;
	FLinearColor Color = FLinearColor::White;

	bool CoversXY(const FVector2D& Point) const;
};

/** Knobs for BuildShapes. Copied off the actor so the maths can run with no world. */
struct GFGAME_API FGFOverworldParams
{
	float Margin = 2000.f;
	float Thickness = 100.f;
	float SurfaceZ = -2.f;
	float OverlayLift = 1.f;
	FLinearColor SlabColor = FLinearColor(0.45f, 0.40f, 0.34f, 1.f);
	TArray<FGFOverworldTint> FactionColours;
	TArray<FGFOverworldTint> HazardColours;
};

UCLASS(Config = Game)
class GFGAME_API AGFOverworldGround : public AActor
{
	GENERATED_BODY()

public:
	AGFOverworldGround();

	virtual void BeginPlay() override;

	UPROPERTY(Config, EditAnywhere, Category = "GF|World", meta = (ClampMin = "0"))
	float Margin;

	UPROPERTY(Config, EditAnywhere, Category = "GF|World", meta = (ClampMin = "1"))
	float Thickness;

	/** Top of the base slab, centimetres. Kept under the voxel ground at Z=0. */
	UPROPERTY(Config, EditAnywhere, Category = "GF|World")
	float SurfaceZ;

	/** How far discs and hazard boxes sit above the slab so the rings read. */
	UPROPERTY(Config, EditAnywhere, Category = "GF|World", meta = (ClampMin = "0"))
	float OverlayLift;

	UPROPERTY(Config, EditAnywhere, Category = "GF|World")
	FLinearColor SlabColor;

	UPROPERTY(Config, EditAnywhere, Category = "GF|World")
	TArray<FGFOverworldTint> FactionColours;

	UPROPERTY(Config, EditAnywhere, Category = "GF|World")
	TArray<FGFOverworldTint> HazardColours;

	FGFOverworldParams CurrentParams() const;

	static TArray<FGFOverworldShape> BuildShapes(
		const TArray<FGFTerritoryFaction>& Factions,
		const TArray<FGFTerritoryHazard>& Hazards,
		const FGFOverworldParams& Params);

	/** True when the map name or its package contains L_Hub (including a PIE prefix). */
	static bool IsVoxelOverworldMap(const UWorld* World);

	/**
	 * L_Hub draws no placeholder meshes. Other maps draw the slab and discs.
	 * Hazard boxes are never drawn: L_Hub already places those props.
	 */
	static bool ShouldDrawShape(EGFOverworldShapeKind Kind, bool bVoxelOverworldMap);

private:
	bool bGroundBuilt = false;
};
