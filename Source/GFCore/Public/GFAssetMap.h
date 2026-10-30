#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFAssetMap.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;

/** Placeholder primitive used until the art for a key lands. */
UENUM(BlueprintType)
enum class EGFPlaceholderShape : uint8
{
	Cube,
	Sphere,
	Cylinder,
	Cone
};

/**
 * Row for DT_AssetMap (Data/CSV/DT_AssetMap.csv). Row name = asset key, e.g. Dino.Raptor, Prop.Mailbox.
 * Same pattern as DT_Blocks: an empty Mesh means "greybox placeholder" (engine shape + tint); when the
 * art is imported, only this row changes (Mesh = /Game/Art/...), no code.
 */
USTRUCT(BlueprintType)
struct GFCORE_API FGFAssetMapRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") TSoftObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") EGFPlaceholderShape Placeholder = EGFPlaceholderShape::Cube;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") FLinearColor Color = FLinearColor::White;
	/** Scale applied to the placeholder (engine shapes are 100 cm). Art meshes use their own size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") FVector PlaceholderScale = FVector::OneVector;
	/** Offset applied to the placeholder (engine shapes are centre-pivoted; art is bottom-centre). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") FVector PlaceholderOffset = FVector(0, 0, 50);
};

struct GFCORE_API FGFAssetMap
{
	/** Sets mesh + material on Comp for Key (art if mapped, else the tinted placeholder). Returns true if real art was used. */
	static bool Apply(UStaticMeshComponent* Comp, FName Key, const FLinearColor* TintOverride = nullptr);
	static const FGFAssetMapRow* Find(FName Key);
	static UStaticMesh* PlaceholderMesh(EGFPlaceholderShape Shape);
	/** The engine's basic shape material with a "Color" parameter, as a dynamic instance on Outer. */
	static UMaterialInterface* MakeTint(UObject* Outer, const FLinearColor& Color);
};
