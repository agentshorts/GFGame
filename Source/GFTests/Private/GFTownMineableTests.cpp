// Town mining whitelist (town_v1 kit). Captain's rule: only crystal and trees can be mined.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.TownMineable;Quit" -unattended -nullrhi
//  - Rules: the pure slot rule and the json parser.
//  - Data: every mesh in Data/Town/town_v1_mineable.json is imported with matching mineable data, slots and tri count.
//  - Hits: complex traces onto real triangles. Crystal-slot faces and trees are allowed; the main slot, walls,
//    inlay pieces (mosaic, planter, fountain, canal) and the power span are refused.
#include "Misc/AutomationTest.h"
#include "GFMineable.h"
#include "GFPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFTownMineTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFTownMineTest
{
	static UStaticMesh* LoadMesh(const FString& Path)
	{
		const FString Name = FPackageName::GetLongPackageAssetName(Path);
		return LoadObject<UStaticMesh>(nullptr, *(Path + TEXT(".") + Name));
	}

	static const FGFMineableEntry* FindEntry(const TArray<FGFMineableEntry>& All, const TCHAR* Asset)
	{
		return All.FindByPredicate([Asset](const FGFMineableEntry& E) { return E.Asset == FName(Asset); });
	}

	static int32 LodTris(const UStaticMesh* Mesh)
	{
		const FStaticMeshRenderData* RD = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!RD || RD->LODResources.Num() == 0)
		{
			return 0;
		}
		int32 N = 0;
		for (const FStaticMeshSection& S : RD->LODResources[0].Sections)
		{
			N += S.NumTriangles;
		}
		return N;
	}

	struct FTri
	{
		FVector A, B, C;
		int32 Slot = INDEX_NONE;
	};

	/** Triangles of LOD0 from the mesh description, each with the material slot it uses. */
	static TArray<FTri> Triangles(UStaticMesh* Mesh)
	{
		TArray<FTri> Out;
		const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!MD)
		{
			return Out;
		}
		FStaticMeshConstAttributes Attr(*MD);
		const TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		const TPolygonGroupAttributesConstRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
		for (const FTriangleID T : MD->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> V = MD->GetTriangleVertices(T);
			FTri Tri;
			Tri.A = FVector(Pos[V[0]]);
			Tri.B = FVector(Pos[V[1]]);
			Tri.C = FVector(Pos[V[2]]);
			Tri.Slot = Mesh->GetMaterialIndex(SlotNames[MD->GetTrianglePolygonGroup(T)]);
			Out.Add(Tri);
		}
		return Out;
	}

	struct FWorld
	{
		UWorld* World = nullptr;

		bool Create()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFTownMineWorld"));
			if (!World)
			{
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			return true;
		}

		UStaticMeshComponent* Place(UStaticMesh* Mesh, const FVector& At)
		{
			AStaticMeshActor* A = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator);
			if (!A)
			{
				return nullptr;
			}
			UStaticMeshComponent* C = A->GetStaticMeshComponent();
			C->SetMobility(EComponentMobility::Movable);
			C->SetStaticMesh(Mesh);
			C->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			C->RecreatePhysicsState();
			return C;
		}

		void Destroy()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}
	};

	struct FTally
	{
		int32 Traced = 0;
		int32 OnTarget = 0;
		int32 SlotMismatch = 0;
		int32 Overlap = 0;
		int32 Allowed = 0;
		int32 Refused = 0;
		int32 AllowedCrystal = 0;
		int32 RefusedMain = 0;
	};

	/**
	 * Traces onto the centroid of each sampled triangle (from both sides of the face) and checks the whitelist
	 * against the triangle's own slot. Expect(slot) says whether that slot may be mined.
	 */
	/** True when P lies on triangle (A, B, C) within Tol (cm). */
	static bool PointOnTri(const FVector& P, const FVector& A, const FVector& B, const FVector& C, double Tol)
	{
		const FVector Closest = FMath::ClosestPointOnTriangleToPoint(P, A, B, C);
		return FVector::Dist(Closest, P) <= Tol;
	}

	static FTally TraceMesh(FAutomationTestBase& Test, FWorld& W, const TCHAR* Label, UStaticMeshComponent* C, int32 Every, TFunctionRef<bool(int32)> Expect, const AGFPlayerState* PS = nullptr)
	{
		FTally T;
		const TArray<FTri> Tris = Triangles(C->GetStaticMesh());
		const FTransform X = C->GetComponentTransform();
		for (int32 i = 0; i < Tris.Num(); ++i)
		{
			const FTri& Tri = Tris[i];
			// Every crystal-slot face is traced; other faces are sampled.
			if (Tri.Slot == 0 && Every > 1 && i % Every != 0)
			{
				continue;
			}
			const FVector A = X.TransformPosition(Tri.A), B = X.TransformPosition(Tri.B), Cc = X.TransformPosition(Tri.C);
			const FVector N = FVector::CrossProduct(B - A, Cc - A).GetSafeNormal();
			if (N.IsNearlyZero())
			{
				continue;
			}
			const FVector Mid = (A + B + Cc) / 3.0;
			for (const double Side : { 1.0, -1.0 })
			{
				FHitResult Hit;
				const FVector Start = Mid + N * (Side * 2.0);
				if (!W.World->LineTraceSingleByChannel(Hit, Start, Mid - N * (Side * 1.0), ECC_Visibility, FGFMineable::QueryParams()))
				{
					continue;
				}
				if (Hit.GetComponent() != C || FVector::Dist(Hit.ImpactPoint, Mid) > 0.25)
				{
					continue;
				}
				++T.Traced;
				const int32 HitSlot = FGFMineable::SlotFromHit(Hit);
				if (HitSlot == Tri.Slot)
				{
					++T.OnTarget;
				}
				else
				{
					// The ray may land where a face of the hit slot overlaps the target (crystal lining laid on a
					// wall face); that is a correct answer for the face actually hit, so only count real mismatches.
					bool bOverlap = false;
					for (const FTri& O : Tris)
					{
						if (O.Slot == HitSlot && PointOnTri(Hit.ImpactPoint, X.TransformPosition(O.A), X.TransformPosition(O.B), X.TransformPosition(O.C), 0.25))
						{
							bOverlap = true;
							break;
						}
					}
					bOverlap ? ++T.Overlap : ++T.SlotMismatch;
				}
				FString Why;
				const bool bOk = PS ? PS->CanMineHit(Hit, Why) : FGFMineable::CanMineHit(Hit, Why);
				const bool bWant = Expect(HitSlot);
				if (bOk != bWant)
				{
					Test.AddError(FString::Printf(TEXT("%s tri %d slot %d: CanMineHit %d, want %d (%s)"), Label, i, HitSlot, bOk, bWant, *Why));
				}
				bOk ? ++T.Allowed : ++T.Refused;
				if (bOk && HitSlot == 1)
				{
					++T.AllowedCrystal;
				}
				if (!bOk && HitSlot == 0)
				{
					++T.RefusedMain;
				}
				break;
			}
		}
		Test.AddInfo(FString::Printf(TEXT("%s: %d traced, %d on target slot, %d on an overlapping face of the hit slot, %d mismatched, %d allowed, %d refused"), Label, T.Traced, T.OnTarget, T.Overlap, T.SlotMismatch, T.Allowed, T.Refused));
		Test.TestTrue(FString::Printf(TEXT("%s: traces land on the mesh"), Label), T.Traced > 0);
		// Face -> section -> slot must agree with the slot of the face the ray actually hit.
		Test.TestTrue(FString::Printf(TEXT("%s: hit slot matches the face's slot (%d of %d, %d overlaps)"), Label, T.OnTarget, T.Traced, T.Overlap), T.SlotMismatch == 0);
		return T;
	}
}

using namespace GFTownMineTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownMineRulesTest, "GFGame.TownMineable.Rules", GFTownMineTestFlags)
bool FGFTownMineRulesTest::RunTest(const FString&)
{
	TestFalse(TEXT("no data refuses"), FGFMineable::AllowsSlot(nullptr, 0));
	UGFMineableAssetUserData* D = NewObject<UGFMineableAssetUserData>();
	D->Mode = EGFMineableMode::None;
	TestFalse(TEXT("None refuses slot 0"), FGFMineable::AllowsSlot(D, 0));
	TestFalse(TEXT("None refuses slot 1"), FGFMineable::AllowsSlot(D, 1));
	D->Mode = EGFMineableMode::WholeMesh;
	TestTrue(TEXT("WholeMesh allows slot 0"), FGFMineable::AllowsSlot(D, 0));
	TestTrue(TEXT("WholeMesh allows an unresolved face"), FGFMineable::AllowsSlot(D, INDEX_NONE));
	D->Mode = EGFMineableMode::Slots;
	D->Slots = { 1 };
	TestTrue(TEXT("Slots allows the crystal slot"), FGFMineable::AllowsSlot(D, 1));
	TestFalse(TEXT("Slots refuses the main slot"), FGFMineable::AllowsSlot(D, 0));
	TestFalse(TEXT("Slots refuses an unresolved face"), FGFMineable::AllowsSlot(D, INDEX_NONE));

	FHitResult Empty;
	FString Why;
	TestFalse(TEXT("a hit with no component is refused"), FGFMineable::CanMineHit(Empty, Why));
	const FCollisionQueryParams Q = FGFMineable::QueryParams();
	TestTrue(TEXT("mining traces are complex"), Q.bTraceComplex);
	TestTrue(TEXT("mining traces return the face index"), Q.bReturnFaceIndex);

	TArray<FGFMineableEntry> Parsed;
	FString Err;
	TestTrue(TEXT("parser reads a row"), FGFMineable::ParseWhitelist(TEXT("{\"schema\":\"gf.mineable/v1\",\"meshes\":[{\"asset\":\"SM_X\",\"path\":\"/Game/X\",\"mode\":\"slots\",\"slots\":[1],\"material_slots\":[\"Main\",\"Crystal\"],\"tri_count\":12}]}"), Parsed, Err));
	TestTrue(TEXT("parsed slots row"), Parsed.Num() == 1 && Parsed[0].Mode == EGFMineableMode::Slots && Parsed[0].Slots == TArray<int32>{ 1 } && Parsed[0].TriCount == 12);
	Parsed.Reset();
	TestFalse(TEXT("parser rejects an unknown mode"), FGFMineable::ParseWhitelist(TEXT("{\"schema\":\"gf.mineable/v1\",\"meshes\":[{\"asset\":\"SM_X\",\"path\":\"/Game/X\",\"mode\":\"maybe\"}]}"), Parsed, Err));
	Parsed.Reset();
	TestFalse(TEXT("parser rejects another schema"), FGFMineable::ParseWhitelist(TEXT("{\"schema\":\"mineable/v2\",\"meshes\":[]}"), Parsed, Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownMineDataTest, "GFGame.TownMineable.Data", GFTownMineTestFlags)
bool FGFTownMineDataTest::RunTest(const FString&)
{
	TArray<FGFMineableEntry> All;
	FString Err;
	if (!TestTrue(TEXT("town whitelist loads: ") + Err, FGFMineable::LoadTownWhitelist(All, Err)))
	{
		return false;
	}
	int32 Whole = 0, Slots = 0, None = 0;
	for (const FGFMineableEntry& E : All)
	{
		const FString Name = E.Asset.ToString();
		(E.Mode == EGFMineableMode::WholeMesh ? Whole : E.Mode == EGFMineableMode::Slots ? Slots : None) += 1;
		// Only trees are whole-mesh mineable, and only a Crystal slot is ever listed.
		if (E.Mode == EGFMineableMode::WholeMesh)
		{
			TestTrue(Name + TEXT(" whole-mesh entry is a tree"), Name.Contains(TEXT("Tree")));
		}
		if (E.Mode == EGFMineableMode::Slots)
		{
			TestTrue(Name + TEXT(" mines slot 1 only"), E.Slots == TArray<int32>{ 1 });
			TestTrue(Name + TEXT(" slot 1 is Crystal"), E.MaterialSlots.IsValidIndex(1) && E.MaterialSlots[1] == FName(TEXT("Crystal")));
		}
		UStaticMesh* Mesh = LoadMesh(E.Path);
		if (!TestNotNull(Name + TEXT(" is imported at ") + E.Path, Mesh))
		{
			continue;
		}
		const UGFMineableAssetUserData* D = FGFMineable::Find(Mesh);
		if (TestNotNull(Name + TEXT(" carries mineable data"), D))
		{
			TestEqual(Name + TEXT(" mineable mode"), static_cast<int32>(D->Mode), static_cast<int32>(E.Mode));
			TestTrue(Name + TEXT(" mineable slots"), D->Slots == E.Slots);
			TestEqual(Name + TEXT(" kit"), D->Kit, FName(TEXT("town_v1")));
		}
		const TArray<FStaticMaterial>& Mats = Mesh->GetStaticMaterials();
		TestEqual(Name + TEXT(" material slot count"), Mats.Num(), E.MaterialSlots.Num());
		for (int32 s = 0; s < FMath::Min(Mats.Num(), E.MaterialSlots.Num()); ++s)
		{
			TestEqual(FString::Printf(TEXT("%s slot %d name"), *Name, s), Mats[s].MaterialSlotName, E.MaterialSlots[s]);
		}
		if (E.Mode == EGFMineableMode::Slots && Mats.IsValidIndex(1))
		{
			const FString Want = TEXT("MI_") + Name.RightChop(3) + TEXT("_Crystal");
			TestTrue(Name + TEXT(" crystal slot uses ") + Want, Mats[1].MaterialInterface && Mats[1].MaterialInterface->GetName() == Want);
		}
		TestEqual(Name + TEXT(" tri count (manifest tri_count)"), LodTris(Mesh), E.TriCount);
		TestTrue(Name + TEXT(" sits on the ground (min z ~ 0)"), FMath::Abs(Mesh->GetBoundingBox().Min.Z) < 0.5);
	}
	TestEqual(TEXT("town_v1 meshes"), All.Num(), 30);
	TestEqual(TEXT("whole-mesh (trees)"), Whole, 3);
	TestEqual(TEXT("crystal-slot meshes"), Slots, 8);
	TestEqual(TEXT("not mineable"), None, 19);
	for (const TCHAR* Refused : { TEXT("SM_Town_Wall_Straight"), TEXT("SM_Town_Wall_Corner"), TEXT("SM_Town_Wall_Gate"), TEXT("SM_Town_Canal_Straight"), TEXT("SM_Town_Canal_Corner"), TEXT("SM_Town_Canal_Bridge"), TEXT("SM_Town_Paving_Mosaic"), TEXT("SM_Town_Planter"), TEXT("SM_Town_Fountain"), TEXT("SM_Town_PowerLine_Span") })
	{
		const FGFMineableEntry* E = FindEntry(All, Refused);
		TestTrue(FString(Refused) + TEXT(" is listed and not mineable"), E && E->Mode == EGFMineableMode::None);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownMineHitsTest, "GFGame.TownMineable.Hits", GFTownMineTestFlags)
bool FGFTownMineHitsTest::RunTest(const FString&)
{
	TArray<FGFMineableEntry> All;
	FString Err;
	if (!TestTrue(TEXT("town whitelist loads"), FGFMineable::LoadTownWhitelist(All, Err)))
	{
		return false;
	}
	FWorld W;
	if (!TestTrue(TEXT("test world"), W.Create()))
	{
		return false;
	}
	const AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>();
	TestNotNull(TEXT("player state"), PS);

	int32 Slot = 0;
	auto Place = [&](const TCHAR* Asset) -> UStaticMeshComponent*
	{
		const FGFMineableEntry* E = FindEntry(All, Asset);
		UStaticMesh* Mesh = E ? LoadMesh(E->Path) : nullptr;
		if (!TestNotNull(FString(Asset) + TEXT(" loads"), Mesh))
		{
			return nullptr;
		}
		// 50 m apart so traces only find the mesh under test.
		return W.Place(Mesh, FVector(5000.0 * Slot++, 0.0, 0.0));
	};

	// Crystal-slot meshes: slot 1 allowed, slot 0 refused.
	for (const TCHAR* Asset : { TEXT("SM_Town_House_Small"), TEXT("SM_Town_House_Med"), TEXT("SM_Town_House_Large"), TEXT("SM_Town_Tower_Spire"), TEXT("SM_Town_PowerPylon"), TEXT("SM_Town_LampPost"), TEXT("SM_Town_CaravanPen"), TEXT("SM_Town_CaravanCart") })
	{
		if (UStaticMeshComponent* C = Place(Asset))
		{
			const FTally T = TraceMesh(*this, W, Asset, C, 4, [](int32 S) { return S == 1; }, PS);
			TestTrue(FString(Asset) + TEXT(": a crystal-slot hit is allowed"), T.AllowedCrystal > 0);
			TestTrue(FString(Asset) + TEXT(": a main-slot hit is refused"), T.RefusedMain > 0);
		}
	}
	// Trees: every face allowed.
	for (const TCHAR* Asset : { TEXT("SM_Town_ForestTree_A"), TEXT("SM_Town_ForestTree_B"), TEXT("SM_Town_ForestTree_C") })
	{
		if (UStaticMeshComponent* C = Place(Asset))
		{
			const FTally T = TraceMesh(*this, W, Asset, C, 3, [](int32) { return true; }, PS);
			TestTrue(FString(Asset) + TEXT(": a tree hit is allowed"), T.Allowed > 0 && T.Refused == 0);
		}
	}
	// Walls, inlay pieces (mosaic, planter, fountain, canal) and the power span: every face refused, crystal
	// texels included. The fountain is sampled (5878 tris); the rest are traced on every face.
	for (const TCHAR* Asset : { TEXT("SM_Town_Wall_Straight"), TEXT("SM_Town_Wall_Corner"), TEXT("SM_Town_Wall_Gate"), TEXT("SM_Town_Paving_Mosaic"), TEXT("SM_Town_Planter"), TEXT("SM_Town_Fountain"), TEXT("SM_Town_Canal_Straight"), TEXT("SM_Town_Canal_Bridge"), TEXT("SM_Town_PowerLine_Span"), TEXT("SM_Town_Stall_Frosthorn") })
	{
		if (UStaticMeshComponent* C = Place(Asset))
		{
			const int32 Every = FCString::Strcmp(Asset, TEXT("SM_Town_Fountain")) == 0 ? 20 : 1;
			const FTally T = TraceMesh(*this, W, Asset, C, Every, [](int32) { return false; }, PS);
			TestTrue(FString(Asset) + TEXT(": hits are refused"), T.Refused > 0 && T.Allowed == 0);
		}
	}
	// A mesh with no mineable data (engine cube) is refused.
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		UStaticMeshComponent* C = W.Place(Cube, FVector(5000.0 * Slot++, 0.0, 0.0));
		FHitResult Hit;
		const FVector At = C->GetComponentLocation();
		const bool bHit = W.World->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 500), At, ECC_Visibility, FGFMineable::QueryParams());
		FString Why;
		TestTrue(TEXT("cube is hit"), bHit && Hit.GetComponent() == C);
		TestFalse(TEXT("a mesh without mineable data is refused"), FGFMineable::CanMineHit(Hit, Why));
	}
	W.Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
