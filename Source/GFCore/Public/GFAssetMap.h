#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFAssetMap.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UMaterialInterface;
class UStaticMeshComponent;
class USkeletalMeshComponent;

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
 * Same pattern as DT_Blocks: an empty Mesh means "greybox placeholder" (engine shape + tint).
 * Skeletal art is the trailing columns (SkeletalMesh and after). An empty SkeletalMesh keeps the placeholder.
 * IdleAnim / MoveAnim are DT_AnimMap keys for UGFSimpleAnimComponent. ArtScale / ArtOffset / ArtRotation
 * are the skeletal mesh's offset from its attach parent (the player capsule, a dino capsule, or the giant's ScaleRoot).
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

	/** Skeletal art. Empty keeps the static Mesh / placeholder. Trailing CSV column: do not reorder ahead of Mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
	/** DT_AnimMap key, looped while nearly still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") FName IdleAnim;
	/** DT_AnimMap key, looped while moving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") FName MoveAnim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") FVector ArtScale = FVector::OneVector;
	/** Capsule-relative (or ScaleRoot-relative) offset of the skeletal mesh. Player feet: Z = -88. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") FVector ArtOffset = FVector::ZeroVector;
	/** Imported meshes face +X (Force Front X), so yaw 0 faces the actor forward. Yaw -90 is the mannequin fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asset") FRotator ArtRotation = FRotator::ZeroRotator;
};

struct GFCORE_API FGFAssetMap
{
	/** Sets mesh + material on Comp for Key (art if mapped, else the tinted placeholder). Returns true if real art was used. */
	static bool Apply(UStaticMeshComponent* Comp, FName Key, const FLinearColor* TintOverride = nullptr);
	/**
	 * Shows the skeletal mesh for Key when it loads (ArtScale / ArtOffset / ArtRotation), and hides Fallback.
	 * Otherwise hides Art and keeps today's placeholder on Fallback. A path that fails to load logs once and
	 * falls back; it never crashes. Returns true when the skeletal mesh is showing.
	 * Copies IdleAnim / MoveAnim onto a UGFSimpleAnimComponent on Art's owner when there is one.
	 * Tint applies to the placeholder only.
	 */
	static bool ApplySkeletal(USkeletalMeshComponent* Art, UStaticMeshComponent* Fallback, FName Key, const FLinearColor* Tint = nullptr);
	/** ApplySkeletal for a row already in hand. Key is only used in the missing-asset log. */
	static bool ApplySkeletalFromRow(USkeletalMeshComponent* Art, UStaticMeshComponent* Fallback, FName Key, const FGFAssetMapRow* Row, const FLinearColor* Tint = nullptr);
	static const FGFAssetMapRow* Find(FName Key);
	static UStaticMesh* PlaceholderMesh(EGFPlaceholderShape Shape);
	/** The engine's basic shape material with a "Color" parameter, as a dynamic instance on Outer. */
	static UMaterialInterface* MakeTint(UObject* Outer, const FLinearColor& Color);
};
