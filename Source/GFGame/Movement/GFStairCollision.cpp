#include "GFStairCollision.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "StaticMeshResources.h"
#include "UObject/UObjectIterator.h"
#if WITH_EDITORONLY_DATA
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

namespace GFStairCollisionDetail
{
	/** Up-facing within about 18 degrees. Treads are flat. */
	constexpr float MinTreadNormalZ = 0.95f;
	/** A tread this close to the bounds bottom is the ground floor face, not a step. */
	constexpr float MinTreadHeightCm = 1.5f;
	/** The capsule band checked above a tread: from just over it to a standing player's head (88 half height). */
	constexpr float BandBottomCm = 2.f;
	constexpr float BandTopCm = 176.f;
	constexpr float BandHalfWidthCm = 2.f;
	/** Knee height for the reach rays. */
	constexpr float ReachHeightCm = 50.f;
	/** One sample per this many cm of triangle edge, capped per triangle. */
	constexpr float SampleSpacingCm = 25.f;
	constexpr int32 MaxSubdivisions = 8;

	struct FTri
	{
		FVector A;
		FVector B;
		FVector C;
		FVector Normal;
	};

	/** World-space up-facing triangles of LOD0 from the render buffers. False when the CPU copy is gone (bAllowCPUAccess off). */
	bool GatherUpTrianglesFromRender(const UStaticMeshComponent* Component, TArray<FTri>& Out)
	{
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		const FStaticMeshRenderData* Render = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!Render || Render->LODResources.Num() == 0)
		{
			return false;
		}
		const FStaticMeshLODResources& Lod = Render->LODResources[0];
		const FPositionVertexBuffer& Positions = Lod.VertexBuffers.PositionVertexBuffer;
		const FStaticMeshVertexBuffer& Tangents = Lod.VertexBuffers.StaticMeshVertexBuffer;
		if (Positions.GetNumVertices() == 0 || !Positions.GetVertexData() || Tangents.GetNumVertices() != Positions.GetNumVertices() || !Tangents.GetTangentData())
		{
			return false;
		}
		TArray<uint32> Indices;
		Lod.IndexBuffer.GetCopy(Indices);
		if (Indices.Num() < 3)
		{
			return false;
		}
		const FTransform ToWorld = Component->GetComponentTransform();
		const uint32 NumVertices = Positions.GetNumVertices();
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			const uint32 I0 = Indices[Index];
			const uint32 I1 = Indices[Index + 1];
			const uint32 I2 = Indices[Index + 2];
			if (I0 >= NumVertices || I1 >= NumVertices || I2 >= NumVertices)
			{
				continue;
			}
			const FVector LocalNormal = FVector(FVector3f(Tangents.VertexTangentZ(I0))) + FVector(FVector3f(Tangents.VertexTangentZ(I1))) + FVector(FVector3f(Tangents.VertexTangentZ(I2)));
			const FVector Normal = ToWorld.TransformVectorNoScale(LocalNormal).GetSafeNormal();
			if (Normal.Z < MinTreadNormalZ)
			{
				continue;
			}
			Out.Add({ ToWorld.TransformPosition(FVector(Positions.VertexPosition(I0))), ToWorld.TransformPosition(FVector(Positions.VertexPosition(I1))),
				ToWorld.TransformPosition(FVector(Positions.VertexPosition(I2))), Normal });
		}
		return true;
	}

	/**
	 * Editor fallback: the same triangles from the LOD0 mesh description (the imported source), scaled by the LOD0 build
	 * scale. Town meshes do not set bAllowCPUAccess, so an editor script often has no CPU render copy, but the source is
	 * always there in the editor. Uses the imported vertex normals; a triangle without them uses its winding normal.
	 */
	bool GatherUpTrianglesFromSource(const UStaticMeshComponent* Component, TArray<FTri>& Out)
	{
#if WITH_EDITORONLY_DATA
		UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		const FMeshDescription* Source = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!Source || Source->Triangles().Num() == 0)
		{
			return false;
		}
		const FVector BuildScale = Mesh->GetNumSourceModels() > 0 ? FVector(Mesh->GetSourceModel(0).BuildSettings.BuildScale3D) : FVector::OneVector;
		FStaticMeshConstAttributes Attributes(*Source);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		const TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		const FTransform ToWorld = Component->GetComponentTransform();
		for (const FTriangleID Triangle : Source->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> Vertices = Source->GetTriangleVertices(Triangle);
			const TArrayView<const FVertexInstanceID> Instances = Source->GetTriangleVertexInstances(Triangle);
			if (Vertices.Num() != 3 || Instances.Num() != 3)
			{
				continue;
			}
			const FVector A = FVector(Positions[Vertices[0]]) * BuildScale;
			const FVector B = FVector(Positions[Vertices[1]]) * BuildScale;
			const FVector C = FVector(Positions[Vertices[2]]) * BuildScale;
			FVector LocalNormal = FVector::ZeroVector;
			if (Normals.IsValid())
			{
				LocalNormal = FVector(Normals[Instances[0]]) + FVector(Normals[Instances[1]]) + FVector(Normals[Instances[2]]);
			}
			if (LocalNormal.IsNearlyZero())
			{
				// Unreal's front face winding: (C - A) x (B - A) points out.
				LocalNormal = FVector::CrossProduct(C - A, B - A);
			}
			const FVector Normal = ToWorld.TransformVectorNoScale(LocalNormal).GetSafeNormal();
			if (Normal.Z < MinTreadNormalZ)
			{
				continue;
			}
			Out.Add({ ToWorld.TransformPosition(A), ToWorld.TransformPosition(B), ToWorld.TransformPosition(C), Normal });
		}
		return true;
#else
		return false;
#endif
	}

	/** Render buffers first, then the editor source. False when neither has triangles. */
	bool GatherUpTriangles(const UStaticMeshComponent* Component, TArray<FTri>& Out)
	{
		Out.Reset();
		if (GatherUpTrianglesFromRender(Component, Out))
		{
			return true;
		}
		Out.Reset();
		return GatherUpTrianglesFromSource(Component, Out);
	}

	/** Empty when FindBuriedTreads can measure the component, else why it returns -1. */
	FString AuditBlocker(const UStaticMeshComponent* Component)
	{
		if (!Component)
		{
			return TEXT("no component");
		}
		if (!Component->GetStaticMesh())
		{
			return TEXT("component has no static mesh");
		}
		if (!Component->IsRegistered())
		{
			return TEXT("component is not registered");
		}
		if (!Component->IsPhysicsStateCreated() || !Component->GetBodyInstance() || !Component->GetBodyInstance()->IsValidBodyInstance())
		{
			return FString::Printf(TEXT("no physics state (collision %s, world %s)"),
				Component->IsCollisionEnabled() ? TEXT("on") : TEXT("off"),
				Component->GetWorld() && Component->GetWorld()->GetPhysicsScene() ? TEXT("has a physics scene") : TEXT("has no physics scene"));
		}
		return FString();
	}

	/** Sub-triangle centroids on a grid sized to the triangle, so a big plinth top is sampled at its rim, not only its middle. */
	void SampleTriangle(const FTri& Tri, TArray<FVector>& Out)
	{
		const float Longest = FMath::Max3((Tri.B - Tri.A).Size(), (Tri.C - Tri.B).Size(), (Tri.A - Tri.C).Size());
		const int32 N = FMath::Clamp(FMath::CeilToInt(Longest / SampleSpacingCm), 1, MaxSubdivisions);
		for (int32 I = 0; I < N; ++I)
		{
			for (int32 J = 0; I + J < N; ++J)
			{
				const float U = (I + 1.f / 3.f) / N;
				const float V = (J + 1.f / 3.f) / N;
				Out.Add(Tri.A + (Tri.B - Tri.A) * U + (Tri.C - Tri.A) * V);
			}
		}
	}
}

int32 UGFStairCollisionLibrary::FindBuriedTreads(UStaticMeshComponent* Component, float StepHeightCm, TArray<FVector>& OutWorldPoints, TArray<float>& OutTreadHeightsCm)
{
	using namespace GFStairCollisionDetail;
	OutWorldPoints.Reset();
	OutTreadHeightsCm.Reset();
	if (!Component || !Component->GetStaticMesh())
	{
		return -1;
	}
	// Complex as simple means the blocking collision is the render triangles, so no hull can cover a tread the eye
	// sees. The overlap test below would read the mesh's own walls and risers over open treads as buried, so skip it.
	if (UsesComplexAsSimple(Component->GetStaticMesh()))
	{
		return 0;
	}
	if (!AuditBlocker(Component).IsEmpty())
	{
		return -1;
	}
	TArray<FTri> Treads;
	if (!GatherUpTriangles(Component, Treads))
	{
		return -1;
	}
	const FBox Bounds = Component->Bounds.GetBox();
	const double GroundZ = Bounds.Min.Z;
	const double Reach = Bounds.GetSize().Size2D() + 100.0;
	FCollisionQueryParams Complex(SCENE_QUERY_STAT(GFStairCollisionComplex), true);
	const FCollisionShape Band = FCollisionShape::MakeBox(FVector(BandHalfWidthCm, BandHalfWidthCm, (BandTopCm - BandBottomCm) * 0.5f));
	const FVector BandCentreUp(0.0, 0.0, BandBottomCm + (BandTopCm - BandBottomCm) * 0.5f);
	static const FVector ReachDirs[] = {
		FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0),
		FVector(1, 1, 0).GetSafeNormal(), FVector(1, -1, 0).GetSafeNormal(), FVector(-1, 1, 0).GetSafeNormal(), FVector(-1, -1, 0).GetSafeNormal() };

	TArray<FVector> Samples;
	for (const FTri& Tri : Treads)
	{
		Samples.Reset();
		SampleTriangle(Tri, Samples);
		for (const FVector& Point : Samples)
		{
			const float Height = static_cast<float>(Point.Z - GroundZ);
			if (Height <= MinTreadHeightCm || Height > StepHeightCm)
			{
				continue;
			}
			// Blocking collision (the simple hull) fills the band over the tread.
			if (!Component->OverlapComponent(Point + BandCentreUp, FQuat::Identity, Band))
			{
				continue;
			}
			// The render mesh shows air over the tread.
			FHitResult Hit;
			if (Component->LineTraceComponent(Hit, Point + FVector(0.0, 0.0, BandBottomCm), Point + FVector(0.0, 0.0, BandTopCm), Complex))
			{
				continue;
			}
			// A player can see it from outside: some knee-high ray leaves the render mesh without a hit.
			const FVector Knee = Point + FVector(0.0, 0.0, ReachHeightCm);
			bool bReachable = false;
			for (const FVector& Dir : ReachDirs)
			{
				FHitResult Ray;
				if (!Component->LineTraceComponent(Ray, Knee, Knee + Dir * Reach, Complex))
				{
					bReachable = true;
					break;
				}
			}
			if (!bReachable)
			{
				continue;
			}
			OutWorldPoints.Add(Point);
			OutTreadHeightsCm.Add(Height);
		}
	}
	return OutWorldPoints.Num();
}

FString UGFStairCollisionLibrary::DescribeTreadAuditBlocker(UStaticMeshComponent* Component)
{
	FString Why = GFStairCollisionDetail::AuditBlocker(Component);
	if (!Why.IsEmpty())
	{
		return Why;
	}
	if (UsesComplexAsSimple(Component->GetStaticMesh()))
	{
		return FString();
	}
	TArray<GFStairCollisionDetail::FTri> Treads;
	if (GFStairCollisionDetail::GatherUpTrianglesFromRender(Component, Treads))
	{
		return FString();
	}
	Treads.Reset();
	if (GFStairCollisionDetail::GatherUpTrianglesFromSource(Component, Treads))
	{
		return FString();
	}
	return TEXT("no CPU render triangles (bAllowCPUAccess off) and no editor mesh description for LOD0");
}

FString UGFStairCollisionLibrary::DescribeTreadSource(UStaticMeshComponent* Component)
{
	TArray<GFStairCollisionDetail::FTri> Treads;
	if (GFStairCollisionDetail::GatherUpTrianglesFromRender(Component, Treads))
	{
		return TEXT("render buffers");
	}
	Treads.Reset();
	if (GFStairCollisionDetail::GatherUpTrianglesFromSource(Component, Treads))
	{
		return TEXT("mesh description");
	}
	return TEXT("none");
}

FString UGFStairCollisionLibrary::DescribeSimpleCollision(UStaticMesh* Mesh)
{
	const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
	if (!Body)
	{
		return Mesh ? FString(TEXT("no body setup")) : FString(TEXT("no mesh"));
	}
	const FKAggregateGeom& Geom = Body->AggGeom;
	const TCHAR* Flag = Body->CollisionTraceFlag == CTF_UseComplexAsSimple ? TEXT("complex as simple")
		: Body->CollisionTraceFlag == CTF_UseSimpleAsComplex ? TEXT("simple as complex") : TEXT("default");
	TArray<FString> Elems;
	double Biggest = 0.0;
	auto AddElem = [&Elems, &Biggest](const TCHAR* Kind, const FName& Name, const FBox& Box)
	{
		Biggest = FMath::Max(Biggest, Box.GetVolume());
		Elems.Add(FString::Printf(TEXT("%s %s [%s .. %s]"), Kind, *Name.ToString(), *Box.Min.ToCompactString(), *Box.Max.ToCompactString()));
	};
	for (const FKBoxElem& Elem : Geom.BoxElems)
	{
		const FVector Half(Elem.X * 0.5f, Elem.Y * 0.5f, Elem.Z * 0.5f);
		AddElem(TEXT("box"), Elem.GetName(), FBox(Elem.Center - Half, Elem.Center + Half));
	}
	for (const FKConvexElem& Elem : Geom.ConvexElems)
	{
		AddElem(TEXT("convex"), Elem.GetName(), Elem.ElemBox);
	}
	const double RenderVolume = Mesh->GetBoundingBox().GetVolume();
	const double Share = RenderVolume > 0.0 ? Biggest / RenderVolume : 0.0;
	return FString::Printf(TEXT("trace %s; %d boxes, %d convex, %d spheres, %d capsules; biggest element fills %.0f%% of the render bounds; %s"),
		Flag, Geom.BoxElems.Num(), Geom.ConvexElems.Num(), Geom.SphereElems.Num(), Geom.SphylElems.Num(), Share * 100.0, *FString::Join(Elems, TEXT("; ")));
}

bool UGFStairCollisionLibrary::UsesComplexAsSimple(UStaticMesh* Mesh)
{
	const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
	return Body && Body->CollisionTraceFlag == CTF_UseComplexAsSimple;
}

bool UGFStairCollisionLibrary::SetUseComplexAsSimple(UStaticMesh* Mesh, bool bUseComplex)
{
#if WITH_EDITOR
	if (!Mesh)
	{
		return false;
	}
	Mesh->Modify();
	if (!Mesh->GetBodySetup())
	{
		Mesh->CreateBodySetup();
	}
	UBodySetup* Body = Mesh->GetBodySetup();
	if (!Body)
	{
		return false;
	}
	Body->Modify();
	Body->CollisionTraceFlag = bUseComplex ? CTF_UseComplexAsSimple : CTF_UseDefault;
	Body->InvalidatePhysicsData();
	Body->CreatePhysicsMeshes();
	// A reimport keeps hand-set collision only when the asset says it was customised.
	Mesh->bCustomizedCollision = true;
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		if (It->GetStaticMesh() == Mesh && It->IsRegistered())
		{
			It->RecreatePhysicsState();
		}
	}
	Mesh->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}
