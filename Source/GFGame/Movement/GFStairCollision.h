// #341 stair collision audit and fix. The playtest found stairs and porches that feel like an invisible wall: the
// placed mesh's simple collision (its UCX hull) stands where the render mesh shows a tread. This library finds those
// treads on a placed component and, in the editor, switches a mesh to use its render triangles as collision, so what
// the player sees is what the capsule walks on.
// Tools/fix_stair_collision_341.py drives it on L_World_Factions_A; GFGame.Movement.StairKit.TownCollision asserts it.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GFStairCollision.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class GFGAME_API UGFStairCollisionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Buried treads on one placed, registered component. A tread sample is an up-facing render triangle point between
	 * 1.5 cm and StepHeightCm above the bottom of the component's bounds (a step a walking pawn can take from the
	 * ground). It is buried when the component's blocking collision fills the capsule band above it while the render
	 * mesh shows open air there, and a horizontal ray from knee height leaves the mesh somewhere (a player can reach it).
	 * Triangles come from the LOD0 render buffers, or in the editor from the LOD0 mesh description when the render
	 * copy is not CPU readable. A mesh that uses complex as simple returns 0: its collision is its render triangles,
	 * so no hull can cover a tread (and its own walls would read as false buried samples).
	 * Returns how many samples are buried, or -1 when it cannot measure (DescribeTreadAuditBlocker says why).
	 */
	UFUNCTION(BlueprintCallable, Category = "GF|Collision")
	static int32 FindBuriedTreads(UStaticMeshComponent* Component, float StepHeightCm, TArray<FVector>& OutWorldPoints, TArray<float>& OutTreadHeightsCm);

	/** Empty when FindBuriedTreads can measure this component, else why it returns -1 (no physics state, no triangles). */
	UFUNCTION(BlueprintCallable, Category = "GF|Collision")
	static FString DescribeTreadAuditBlocker(UStaticMeshComponent* Component);

	/** Where FindBuriedTreads reads triangles for this component: "render buffers", "mesh description" or "none". */
	UFUNCTION(BlueprintCallable, Category = "GF|Collision")
	static FString DescribeTreadSource(UStaticMeshComponent* Component);

	/** One line about the mesh's simple collision: trace flag, element counts, each element's local box, and how much of the render bounds the biggest one fills. */
	UFUNCTION(BlueprintCallable, Category = "GF|Collision")
	static FString DescribeSimpleCollision(UStaticMesh* Mesh);

	/** The mesh's body setup traces and blocks with its render triangles (CTF_UseComplexAsSimple). */
	UFUNCTION(BlueprintPure, Category = "GF|Collision")
	static bool UsesComplexAsSimple(UStaticMesh* Mesh);

	/**
	 * Editor only. Sets the collision trace flag to CTF_UseComplexAsSimple (or back to CTF_UseDefault), rebuilds the
	 * physics meshes, refreshes every component using the mesh and marks the package dirty. The UCX elements stay in
	 * the asset, so reverting brings the old hull back. Does not save. Returns false outside the editor.
	 */
	UFUNCTION(BlueprintCallable, Category = "GF|Collision")
	static bool SetUseComplexAsSimple(UStaticMesh* Mesh, bool bUseComplex);
};
