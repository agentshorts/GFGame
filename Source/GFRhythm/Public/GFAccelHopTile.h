#pragma once
// Acceleration Hop (issue 334): one hop tile on the road. Placed by Tools/place_acceleration_hop.py; the kind picks
// the mesh (/Game/Art/AccelerationHop/Tiles/SM_Hop_Tile_<Kind>). The tile only marks the spot: the movement
// component judges the landing and owns the boost.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFAccelHop.h"
#include "GFAccelHopTile.generated.h"

class UStaticMeshComponent;
class UPrimitiveComponent;

UCLASS()
class GFRHYTHM_API AGFAccelHopTile : public AActor
{
	GENERATED_BODY()

public:
	AGFAccelHopTile();

	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintPure, Category = "GF|AccelHop")
	EGFHopTileKind GetKind() const { return Kind; }

	/** Sets the kind and swaps the mesh (the placement script calls this before saving the map). */
	UFUNCTION(BlueprintCallable, Category = "GF|AccelHop")
	void SetKind(EGFHopTileKind InKind);

	UStaticMeshComponent* GetMesh() const { return Mesh; }

	/** The tile a floor component belongs to, or null (what the movement component stands on). */
	static AGFAccelHopTile* FromFloor(const UPrimitiveComponent* Floor);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	EGFHopTileKind Kind = EGFHopTileKind::Standard;

private:
	void ApplyKind();
};
