#pragma once
// Acceleration Hop (issue 334, lit target in issue 342): one hop tile on the road. Placed by Tools/place_acceleration_hop.py; the kind picks
// the mesh (/Game/Art/AccelerationHop/Tiles/SM_Hop_Tile_<Kind>). The tile only marks the spot: the movement
// component judges the landing and owns the boost.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFAccelHop.h"
#include "GFAccelHopTile.generated.h"

class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class AGFAccelHopRoad;

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

	/** v2 (#342): the road that owns this tile and its index in the road's sorted list (set by the road). */
	AGFAccelHopRoad* GetRoad() const { return Road.Get(); }
	int32 GetRoadIndex() const { return RoadIndex; }
	void SetRoad(AGFAccelHopRoad* InRoad, int32 InIndex);

	/**
	 * v2 (#342): the lit target look. Lit drives this tile's own M_GF_HopCel instance to full glow (EmissiveStrength,
	 * RimLightWidth) and its per-tile BeatPulse to Pulse; unlit puts the material's own values back. Works before the
	 * art import too (no material, so only the flag changes).
	 */
	void SetLitVisual(bool bLit, float Pulse, const FGFAccelHopLitTuning& Tuning);

	UFUNCTION(BlueprintPure, Category = "GF|AccelHop")
	bool IsLitVisual() const { return bLitVisual; }

	/** The per-tile BeatPulse last written while lit (0 when unlit). */
	float GetLitPulse() const { return LitPulse; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	EGFHopTileKind Kind = EGFHopTileKind::Standard;

private:
	void ApplyKind();
	/** Makes the dynamic instance on first light and remembers the material's own glow values. */
	UMaterialInstanceDynamic* LitMaterial();

	TWeakObjectPtr<AGFAccelHopRoad> Road;
	int32 RoadIndex = INDEX_NONE;
	bool bLitVisual = false;
	float LitPulse = 0.f;
	float BaseEmissiveStrength = 2.f;
	float BaseRimLightWidth = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LitMID;
};
