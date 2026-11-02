// Town mining whitelist (town_v1 kit). Captain's rule: only crystal and trees can be mined.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.TownMineable;Quit" -unattended -nullrhi
//  - Rules: the pure slot rule and the json parser.
//  - Data: every mesh in Data/Town/town_v1_mineable.json is imported with matching mineable data, slots and tri count.
//  - Hits: complex traces onto real triangles. Crystal-slot faces and trees are allowed; the main slot, walls,
//    inlay pieces (mosaic, planter, fountain, canal) and the power span are refused.
//  - Damage, Break, Regrow, Spells: allowed hits spend Log / CrystalVein integrity, drop through the magnet, and regrow.
// The networked case is GFGame.Net.TownMineable (needs the editor).
#include "Misc/AutomationTest.h"
#include "GFMineable.h"
#include "GFPlayerState.h"
#include "GFBlockIntegritySubsystem.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFMagicActors.h"
#include "GFMagicComponent.h"
#include "GFMagicPlayerState.h"
#include "GFTownNodes.h"
#include "GFVoxelWorld.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "GFNetTestUtils.h"

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

		bool Create(const TCHAR* Name = TEXT("GFTownMineWorld"))
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
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

	struct FMineWorld : FWorld
	{
		UGFBlockIntegritySubsystem* Magic = nullptr;

		bool Open(FAutomationTestBase& Test, const TCHAR* Name)
		{
			if (!Create(Name))
			{
				return false;
			}
			Magic = World->GetSubsystem<UGFBlockIntegritySubsystem>();
			return Test.TestNotNull(TEXT("Integrity subsystem"), Magic);
		}

		~FMineWorld()
		{
			Destroy();
		}
	};

	struct FStand
	{
		FVector Impact = FVector::ZeroVector;
		FVector At = FVector::ZeroVector;
		bool bOk = false;
	};

	/** Stand Distance cm outside the face, and confirm a second trace from there still hits it. */
	static bool ConfirmStand(UWorld* World, UStaticMeshComponent* Comp, const FHitResult& Hit, float Distance, TFunctionRef<bool(const FHitResult&)> Accept, FStand& Out)
	{
		if (Hit.GetComponent() != Comp || !Accept(Hit))
		{
			return false;
		}
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			return false;
		}
		const FVector Stand = FVector(Hit.ImpactPoint) + Normal * Distance;
		FHitResult Check;
		if (!World->LineTraceSingleByChannel(Check, Stand, FVector(Hit.ImpactPoint), ECC_Visibility, FGFMineable::QueryParams()))
		{
			return false;
		}
		if (Check.GetComponent() != Comp || !Accept(Check))
		{
			return false;
		}
		Out.Impact = Check.ImpactPoint;
		Out.At = Stand;
		Out.bOk = true;
		return true;
	}

	/** A point Distance cm outside a face. Accept chooses which face (any, tree, or crystal slot). */
	static FStand StandOutside(UWorld* World, UStaticMeshComponent* Comp, float Distance, TFunctionRef<bool(const FHitResult&)> Accept)
	{
		FStand Out;
		if (!World || !Comp)
		{
			return Out;
		}
		Comp->UpdateBounds();
		const FVector Center = Comp->Bounds.Origin;
		const float Reach = Comp->Bounds.SphereRadius + Distance + 100.f;
		const FVector Dirs[] = {
			FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0), FVector(0, 0, 1), FVector(0, 0, -1)
		};
		for (const FVector& Dir : Dirs)
		{
			FHitResult Hit;
			const FVector Start = Center + Dir * Reach;
			if (!World->LineTraceSingleByChannel(Hit, Start, Center, ECC_Visibility, FGFMineable::QueryParams()))
			{
				continue;
			}
			if (ConfirmStand(World, Comp, Hit, Distance, Accept, Out))
			{
				return Out;
			}
		}
		// Axis rays miss a crystal inlay that does not face the bounds centre. Walk crystal faces first, then a sample of the body.
		const TArray<FTri> Tris = Triangles(Comp->GetStaticMesh());
		const FTransform Xform = Comp->GetComponentTransform();
		const int32 Step = FMath::Max(1, Tris.Num() / 96);
		for (int32 Pass = 0; Pass < 2 && !Out.bOk; ++Pass)
		{
			int32 Tried = 0;
			for (int32 Index = 0; Index < Tris.Num() && Tried < 96 && !Out.bOk; ++Index)
			{
				const FTri& Tri = Tris[Index];
				if (Pass == 0)
				{
					if (Tri.Slot == 0)
					{
						continue;
					}
				}
				else if ((Index % Step) != 0)
				{
					continue;
				}
				++Tried;
				const FVector A = Xform.TransformPosition(Tri.A);
				const FVector B = Xform.TransformPosition(Tri.B);
				const FVector C = Xform.TransformPosition(Tri.C);
				const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
				if (N.IsNearlyZero())
				{
					continue;
				}
				const FVector Mid = (A + B + C) / 3.0;
				for (const float Side : { 1.f, -1.f })
				{
					FHitResult Hit;
					const FVector Start = Mid + N * (Side * (Distance + 4.f));
					if (!World->LineTraceSingleByChannel(Hit, Start, Mid - N * (Side * 2.f), ECC_Visibility, FGFMineable::QueryParams()))
					{
						continue;
					}
					if (ConfirmStand(World, Comp, Hit, Distance, Accept, Out))
					{
						return Out;
					}
				}
			}
		}
		return Out;
	}

	static AGFMagicPlayerState* Digger(UWorld* World, const TCHAR* Key, const FVector& At)
	{
		AGFMagicPlayerState* PS = World ? World->SpawnActor<AGFMagicPlayerState>() : nullptr;
		if (!PS || !PS->Magic)
		{
			return nullptr;
		}
		PS->PlayerKey = Key;
		PS->Magic->BindAvatar(nullptr);
		PS->Magic->SetManualOrigin(At);
		PS->Magic->SetAim(At, At);
		return PS;
	}

	static void Hold(AGFMagicPlayerState* PS, UGFBlockIntegritySubsystem* Magic, const FVector& At, const FVector& Aim, float Seconds)
	{
		if (!PS || !PS->Magic || !Magic)
		{
			return;
		}
		PS->Magic->SetManualOrigin(At);
		PS->Magic->SetAim(At, Aim);
		PS->Magic->SetFieldHeld(true);
		Magic->Advance(Seconds);
	}

	static int32 Ribbons(UWorld* World)
	{
		int32 Count = 0;
		if (!World)
		{
			return 0;
		}
		for (TActorIterator<AGFMagicDropRibbon> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	static UStaticMeshComponent* PlaceAsset(FWorld& W, const TCHAR* Asset, const FVector& At)
	{
		TArray<FGFMineableEntry> All;
		FString Err;
		if (!FGFMineable::LoadTownWhitelist(All, Err))
		{
			return nullptr;
		}
		const FGFMineableEntry* E = FindEntry(All, Asset);
		TArray<FGFMineableEntry> V2;
		if (!E && FGFMineable::LoadTownsV2Whitelist(V2, Err))
		{
			E = FindEntry(V2, Asset);
		}
		UStaticMesh* Mesh = E ? LoadMesh(E->Path) : nullptr;
		return Mesh ? W.Place(Mesh, At) : nullptr;
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

// ---------- towns_v2 (Rime, Cinderwright, homesteads) ----------
// Same rule: only the Crystal slot (slot 1) on crystal-bearing meshes and whole trees (ash trees, snow pines) can be
// mined. Lava fields, ground patches, yard plates, smoke plumes, pipes, chimneys, the crane, roads, fences and the
// rest are refused. Data/Town/towns_v2_mineable.json is built by Tools/gen_towns_v2_mineable.py.

namespace GFTownMineTest
{
	static int32 SampleEvery(const UStaticMesh* Mesh)
	{
		return FMath::Max(1, LodTris(Mesh) / 400);
	}

	static bool IsTreeName(const FString& Name)
	{
		return Name.Contains(TEXT("Tree")) || Name.Contains(TEXT("Pine"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownsV2MineDataTest, "GFGame.TownMineable.V2Data", GFTownMineTestFlags)
bool FGFTownsV2MineDataTest::RunTest(const FString&)
{
	TArray<FGFMineableEntry> All;
	FString Err;
	if (!TestTrue(TEXT("towns_v2 whitelist loads: ") + Err, FGFMineable::LoadTownsV2Whitelist(All, Err)))
	{
		return false;
	}
	int32 Whole = 0, Slots = 0, None = 0;
	for (const FGFMineableEntry& E : All)
	{
		const FString Name = E.Asset.ToString();
		(E.Mode == EGFMineableMode::WholeMesh ? Whole : E.Mode == EGFMineableMode::Slots ? Slots : None) += 1;
		if (E.Mode == EGFMineableMode::WholeMesh)
		{
			TestTrue(Name + TEXT(" whole-mesh entry is a tree"), IsTreeName(Name));
		}
		else if (IsTreeName(Name))
		{
			AddError(Name + TEXT(" is a tree but not whole-mesh mineable"));
		}
		if (E.Mode == EGFMineableMode::Slots)
		{
			TestTrue(Name + TEXT(" mines slot 1 only"), E.Slots == TArray<int32>{ 1 });
			TestTrue(Name + TEXT(" slot 1 is Crystal"), E.MaterialSlots.IsValidIndex(1) && E.MaterialSlots[1] == FName(TEXT("Crystal")));
		}
		if (E.Mode == EGFMineableMode::None)
		{
			TestFalse(Name + TEXT(" refused mesh has no Crystal slot"), E.MaterialSlots.Contains(FName(TEXT("Crystal"))));
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
			TestEqual(Name + TEXT(" kit"), D->Kit, FName(TEXT("towns_v2")));
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
	TestEqual(TEXT("towns_v2 meshes"), All.Num(), 65);
	TestEqual(TEXT("whole-mesh (ash trees, snow pines)"), Whole, 4);
	TestEqual(TEXT("crystal-slot meshes"), Slots, 25);
	TestEqual(TEXT("not mineable"), None, 36);
	TestEqual(TEXT("mineable list (crystal slots plus trees)"), Whole + Slots, 29);
	for (const TCHAR* Refused : { TEXT("SM_Town_Cinderwright_LavaField_Pool"), TEXT("SM_Town_Cinderwright_LavaField_Straight"), TEXT("SM_Town_Cinderwright_YardPlate_Tile"), TEXT("SM_Town_Cinderwright_SmokePlume_A"), TEXT("SM_Town_Cinderwright_Chimney"), TEXT("SM_Town_Cinderwright_Crane"), TEXT("SM_Town_Cinderwright_PowerLine_Span"), TEXT("SM_Town_Cinderwright_CharcoalRock_A"), TEXT("SM_Town_Homestead_Glasshouse"), TEXT("SM_Town_Rime_IceGround_Tile"), TEXT("SM_Town_Road_T") })
	{
		const FGFMineableEntry* E = FindEntry(All, Refused);
		TestTrue(FString(Refused) + TEXT(" is listed and not mineable"), E && E->Mode == EGFMineableMode::None);
	}
	// town_v1 fix: the fountain is the v7 build (sittable 43.75 cm rim, 5470 tris) and stays refused.
	TArray<FGFMineableEntry> V1;
	if (TestTrue(TEXT("town_v1 whitelist loads"), FGFMineable::LoadTownWhitelist(V1, Err)))
	{
		const FGFMineableEntry* F = FindEntry(V1, TEXT("SM_Town_Fountain"));
		TestTrue(TEXT("fountain v7 is listed, refused, 5470 tris"), F && F->Mode == EGFMineableMode::None && F->TriCount == 5470);
		if (UStaticMesh* Mesh = F ? LoadMesh(F->Path) : nullptr)
		{
			TestEqual(TEXT("fountain v7 imported tris"), LodTris(Mesh), 5470);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownMineDamageTest, "GFGame.TownMineable.Damage", GFTownMineTestFlags)
bool FGFTownMineDamageTest::RunTest(const FString&)
{
	using namespace GFTownMineTest;
	FMineWorld W;
	if (!W.Open(*this, TEXT("GFTownMineDamage")))
	{
		return false;
	}
	TestEqual(TEXT("Regrow default is one crop stage"), W.Magic->GetTownRegrowSeconds(), FGFTownNodeRules::RegrowSeconds);

	UStaticMeshComponent* Tree = PlaceAsset(W, TEXT("SM_Town_ForestTree_A"), FVector::ZeroVector);
	UStaticMeshComponent* Crystal = PlaceAsset(W, TEXT("SM_Town_LampPost"), FVector(8000, 0, 0));
	UStaticMeshComponent* Wall = PlaceAsset(W, TEXT("SM_Town_Wall_Straight"), FVector(16000, 0, 0));
	if (!TestNotNull(TEXT("Tree"), Tree) || !TestNotNull(TEXT("Lamp"), Crystal) || !TestNotNull(TEXT("Wall"), Wall))
	{
		return false;
	}
	TestEqual(TEXT("A tree starts at log integrity"), W.Magic->GetTownNodeRemaining(Tree), 100.f);
	TestEqual(TEXT("A crystal slot starts at crystal-vein integrity"), W.Magic->GetTownNodeRemaining(Crystal), 200.f);
	TestEqual(TEXT("A wall has no node"), W.Magic->GetTownNodeRemaining(Wall), -1.f);

	auto AnyFace = [](const FHitResult&) { return true; };
	const FStand TreeStand = StandOutside(W.World, Tree, 200.f, AnyFace);
	const FStand CrystalStand = StandOutside(W.World, Crystal, 200.f, [](const FHitResult& Hit)
	{
		FString Why;
		return FGFMineable::CanMineHit(Hit, Why) && FGFMineable::SlotFromHit(Hit) == 1;
	});
	const FStand WallStand = StandOutside(W.World, Wall, 200.f, AnyFace);
	if (!TestTrue(TEXT("Tree face"), TreeStand.bOk) || !TestTrue(TEXT("Crystal face"), CrystalStand.bOk) || !TestTrue(TEXT("Wall face"), WallStand.bOk))
	{
		return false;
	}

	AGFMagicPlayerState* PS = Digger(W.World, TEXT("Digger"), TreeStand.At);
	if (!TestNotNull(TEXT("Digger"), PS))
	{
		return false;
	}
	Hold(PS, W.Magic, TreeStand.At, TreeStand.Impact, 0.1f);
	PS->Magic->SetFieldHeld(false);
	const float TreeLeft = W.Magic->GetTownNodeRemaining(Tree);
	TestTrue(TEXT("One Sparkcone step wounds the tree"), TreeLeft < 100.f && TreeLeft > 80.f);
	TestFalse(TEXT("One step does not fell the tree"), W.Magic->IsTownNodeDepleted(Tree));
	TestFalse(TEXT("The tree is still there"), Tree->GetOwner()->IsHidden());
	TestEqual(TEXT("No drop yet"), PS->Inventory->GetCount(TEXT("WoodPlank")), 0);

	Hold(PS, W.Magic, CrystalStand.At, CrystalStand.Impact, 0.1f);
	PS->Magic->SetFieldHeld(false);
	const float CrystalLeft = W.Magic->GetTownNodeRemaining(Crystal);
	TestTrue(TEXT("One Sparkcone step wounds the crystal"), CrystalLeft < 200.f && CrystalLeft > 160.f);
	TestFalse(TEXT("The crystal slot is not spent"), W.Magic->IsTownNodeDepleted(Crystal));
	TestEqual(TEXT("No crystal yet"), PS->Inventory->GetCount(TEXT("Crystal")), 0);

	const int32 Wood = PS->Inventory->GetCount(TEXT("WoodPlank"));
	Hold(PS, W.Magic, WallStand.At, WallStand.Impact, 0.5f);
	PS->Magic->SetFieldHeld(false);
	TestEqual(TEXT("The wall never opens a node"), W.Magic->GetTownNodeRemaining(Wall), -1.f);
	TestFalse(TEXT("The wall is not spent"), W.Magic->IsTownNodeDepleted(Wall));
	TestFalse(TEXT("The wall stays visible"), Wall->GetOwner()->IsHidden());
	TestEqual(TEXT("The wall pays nothing"), PS->Inventory->GetCount(TEXT("WoodPlank")), Wood);
	TestEqual(TEXT("The wall pays no crystal"), PS->Inventory->GetCount(TEXT("Crystal")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownMineBreakTest, "GFGame.TownMineable.Break", GFTownMineTestFlags)
bool FGFTownMineBreakTest::RunTest(const FString&)
{
	using namespace GFTownMineTest;
	FMineWorld W;
	if (!W.Open(*this, TEXT("GFTownMineBreak")))
	{
		return false;
	}
	UStaticMeshComponent* Tree = PlaceAsset(W, TEXT("SM_Town_ForestTree_B"), FVector::ZeroVector);
	UStaticMeshComponent* Crystal = PlaceAsset(W, TEXT("SM_Town_CaravanPen"), FVector(8000, 0, 0));
	UStaticMeshComponent* Fountain = PlaceAsset(W, TEXT("SM_Town_Fountain"), FVector(16000, 0, 0));
	UStaticMeshComponent* Planter = PlaceAsset(W, TEXT("SM_Town_Planter"), FVector(24000, 0, 0));
	UStaticMeshComponent* Span = PlaceAsset(W, TEXT("SM_Town_PowerLine_Span"), FVector(32000, 0, 0));
	UStaticMeshComponent* Canal = PlaceAsset(W, TEXT("SM_Town_Canal_Straight"), FVector(40000, 0, 0));
	if (!TestNotNull(TEXT("Tree"), Tree) || !TestNotNull(TEXT("Pen"), Crystal) || !TestNotNull(TEXT("Fountain"), Fountain) || !TestNotNull(TEXT("Planter"), Planter) || !TestNotNull(TEXT("Span"), Span) || !TestNotNull(TEXT("Canal"), Canal))
	{
		return false;
	}
	auto AnyFace = [](const FHitResult&) { return true; };
	const FStand TreeStand = StandOutside(W.World, Tree, 200.f, AnyFace);
	const FStand CrystalStand = StandOutside(W.World, Crystal, 200.f, [](const FHitResult& Hit)
	{
		FString Why;
		return FGFMineable::CanMineHit(Hit, Why) && FGFMineable::SlotFromHit(Hit) == 1;
	});
	if (!TestTrue(TEXT("Tree face"), TreeStand.bOk) || !TestTrue(TEXT("Crystal face"), CrystalStand.bOk))
	{
		return false;
	}
	AGFMagicPlayerState* PS = Digger(W.World, TEXT("Digger"), TreeStand.At);
	if (!TestNotNull(TEXT("Digger"), PS))
	{
		return false;
	}
	UMaterialInterface* Body = Crystal->GetMaterial(0);
	if (!Body)
	{
		Body = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	Hold(PS, W.Magic, TreeStand.At, TreeStand.Impact, 1.6f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("Sparkcone fells the tree"), W.Magic->IsTownNodeDepleted(Tree));
	TestTrue(TEXT("The tree is hidden"), Tree->GetOwner()->IsHidden());
	TestFalse(TEXT("The stump does not block"), Tree->GetOwner()->GetActorEnableCollision());
	TestEqual(TEXT("The bag took a wood plank"), PS->Inventory->GetCount(TEXT("WoodPlank")), 1);
	TestEqual(TEXT("Granted drops counts the plank"), PS->Magic->GetGrantedDrops(), 1);
	AGFMagicDropRibbon* Ribbon = nullptr;
	for (TActorIterator<AGFMagicDropRibbon> It(W.World); It; ++It)
	{
		Ribbon = *It;
		break;
	}
	if (!TestNotNull(TEXT("A ribbon spawned"), Ribbon))
	{
		return false;
	}
	const FVector RibbonFrom = Ribbon->GetActorLocation();
	const FVector Home = PS->Magic->CastOrigin();
	Ribbon->Tick(0.15f);
	TestTrue(TEXT("The ribbon flies toward the digger"), FVector::Dist(Ribbon->GetActorLocation(), Home) < FVector::Dist(RibbonFrom, Home) - 1.f);

	Hold(PS, W.Magic, CrystalStand.At, CrystalStand.Impact, 1.6f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("Sparkcone spends the crystal slot"), W.Magic->IsTownNodeDepleted(Crystal));
	TestFalse(TEXT("The pen stays visible"), Crystal->GetOwner()->IsHidden());
	TestTrue(TEXT("The pen still blocks"), Crystal->GetOwner()->GetActorEnableCollision());
	TestEqual(TEXT("Slot 1 wears the body material"), Crystal->GetMaterial(1), Body);
	TestEqual(TEXT("The bag took a crystal"), PS->Inventory->GetCount(TEXT("Crystal")), 1);
	const int32 CrystalCount = PS->Inventory->GetCount(TEXT("Crystal"));
	Hold(PS, W.Magic, CrystalStand.At, CrystalStand.Impact, 0.5f);
	PS->Magic->SetFieldHeld(false);
	TestEqual(TEXT("A spent slot does not pay twice"), PS->Inventory->GetCount(TEXT("Crystal")), CrystalCount);

	for (UStaticMeshComponent* Prop : { Fountain, Planter, Span, Canal })
	{
		const FStand Stand = StandOutside(W.World, Prop, 180.f, AnyFace);
		if (!TestTrue(TEXT("Prop face"), Stand.bOk))
		{
			return false;
		}
		const int32 BeforeWood = PS->Inventory->GetCount(TEXT("WoodPlank"));
		const int32 BeforeCrystal = PS->Inventory->GetCount(TEXT("Crystal"));
		Hold(PS, W.Magic, Stand.At, Stand.Impact, 0.4f);
		PS->Magic->SetFieldHeld(false);
		TestEqual(TEXT("A refused mesh has no node"), W.Magic->GetTownNodeRemaining(Prop), -1.f);
		TestFalse(TEXT("A refused mesh stays visible"), Prop->GetOwner()->IsHidden());
		TestEqual(TEXT("A refused mesh pays no wood"), PS->Inventory->GetCount(TEXT("WoodPlank")), BeforeWood);
		TestEqual(TEXT("A refused mesh pays no crystal"), PS->Inventory->GetCount(TEXT("Crystal")), BeforeCrystal);
	}
	TestTrue(TEXT("Ribbons flew"), Ribbons(W.World) >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownMineRegrowTest, "GFGame.TownMineable.Regrow", GFTownMineTestFlags)
bool FGFTownMineRegrowTest::RunTest(const FString&)
{
	using namespace GFTownMineTest;
	FMineWorld W;
	if (!W.Open(*this, TEXT("GFTownMineRegrow")))
	{
		return false;
	}
	TestEqual(TEXT("Regrow starts at 120s"), W.Magic->GetTownRegrowSeconds(), 120.f);
	W.Magic->SetTownRegrowSeconds(1.f);
	UStaticMeshComponent* Tree = PlaceAsset(W, TEXT("SM_Town_ForestTree_C"), FVector::ZeroVector);
	if (!TestNotNull(TEXT("Tree"), Tree))
	{
		return false;
	}
	const FStand Stand = StandOutside(W.World, Tree, 200.f, [](const FHitResult&) { return true; });
	if (!TestTrue(TEXT("Tree face"), Stand.bOk))
	{
		return false;
	}
	AGFMagicPlayerState* PS = Digger(W.World, TEXT("Digger"), Stand.At);
	if (!TestNotNull(TEXT("Digger"), PS))
	{
		return false;
	}
	Hold(PS, W.Magic, Stand.At, Stand.Impact, 2.f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("The tree is down"), W.Magic->IsTownNodeDepleted(Tree));
	TestTrue(TEXT("The tree is hidden"), Tree->GetOwner()->IsHidden());
	W.Magic->Advance(0.2f);
	TestTrue(TEXT("It is still down inside the timer"), W.Magic->IsTownNodeDepleted(Tree));
	TestTrue(TEXT("It stays hidden inside the timer"), Tree->GetOwner()->IsHidden());
	W.Magic->Advance(1.f);
	TestFalse(TEXT("The timer puts the tree back"), W.Magic->IsTownNodeDepleted(Tree));
	TestFalse(TEXT("The tree is visible again"), Tree->GetOwner()->IsHidden());
	TestTrue(TEXT("The tree blocks again"), Tree->GetOwner()->GetActorEnableCollision());
	TestEqual(TEXT("Integrity is full again"), W.Magic->GetTownNodeRemaining(Tree), 100.f);
	Hold(PS, W.Magic, Stand.At, Stand.Impact, 1.6f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("It can be felled again"), W.Magic->IsTownNodeDepleted(Tree));
	TestEqual(TEXT("The second fell pays another plank"), PS->Inventory->GetCount(TEXT("WoodPlank")), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownMineSpellsTest, "GFGame.TownMineable.Spells", GFTownMineTestFlags)
bool FGFTownMineSpellsTest::RunTest(const FString&)
{
	using namespace GFTownMineTest;
	auto AnyFace = [](const FHitResult&) { return true; };

	{
		FMineWorld W;
		if (!W.Open(*this, TEXT("GFTownMineMaul")))
		{
			return false;
		}
		UStaticMeshComponent* Tree = PlaceAsset(W, TEXT("SM_Town_ForestTree_A"), FVector::ZeroVector);
		UStaticMeshComponent* Wall = PlaceAsset(W, TEXT("SM_Town_Wall_Straight"), FVector(8000, 0, 0));
		if (!TestNotNull(TEXT("Maul tree"), Tree) || !TestNotNull(TEXT("Maul wall"), Wall))
		{
			return false;
		}
		const FStand TreeStand = StandOutside(W.World, Tree, 80.f, AnyFace);
		const FStand WallStand = StandOutside(W.World, Wall, 80.f, AnyFace);
		if (!TestTrue(TEXT("Maul tree face"), TreeStand.bOk) || !TestTrue(TEXT("Maul wall face"), WallStand.bOk))
		{
			return false;
		}
		AGFMagicPlayerState* PS = Digger(W.World, TEXT("Maul"), TreeStand.At);
		if (!TestNotNull(TEXT("Maul"), PS))
		{
			return false;
		}
		PS->FactionId = TEXT("Verdant");
		TestTrue(TEXT("Verdant takes Rootmaul"), PS->Magic->AuthorityToggle());
		TestEqual(TEXT("The field is root"), PS->Magic->GetElement(), EGFMagicElement::Root);
		PS->Magic->SetManualOrigin(TreeStand.At);
		PS->Magic->SetAim(TreeStand.At, TreeStand.Impact);
		PS->Magic->SetFieldHeld(true);
		W.Magic->Advance(0.2f);
		TestFalse(TEXT("Holding the club has not swung yet"), W.Magic->IsTownNodeDepleted(Tree));
		PS->Magic->SetFieldHeld(false);
		W.Magic->Advance(0.1f);
		TestTrue(TEXT("Swing1 fells the tree"), W.Magic->IsTownNodeDepleted(Tree));
		TestEqual(TEXT("Rootmaul pays a wood plank"), PS->Inventory->GetCount(TEXT("WoodPlank")), 1);
		TestTrue(TEXT("The ribbon flew"), Ribbons(W.World) >= 1);

		PS->Magic->SetManualOrigin(WallStand.At);
		PS->Magic->SetAim(WallStand.At, WallStand.Impact);
		PS->Magic->SetFieldHeld(true);
		W.Magic->Advance(0.2f);
		PS->Magic->SetFieldHeld(false);
		W.Magic->Advance(0.1f);
		TestEqual(TEXT("Rootmaul does not open a wall"), W.Magic->GetTownNodeRemaining(Wall), -1.f);
		TestFalse(TEXT("The wall stays up"), Wall->GetOwner()->IsHidden());
	}

	{
		FMineWorld W;
		if (!W.Open(*this, TEXT("GFTownMineMagma")))
		{
			return false;
		}
		UStaticMeshComponent* Tree = PlaceAsset(W, TEXT("SM_Town_ForestTree_B"), FVector::ZeroVector);
		if (!TestNotNull(TEXT("Magma tree"), Tree))
		{
			return false;
		}
		const FStand Stand = StandOutside(W.World, Tree, 80.f, AnyFace);
		if (!TestTrue(TEXT("Magma face"), Stand.bOk))
		{
			return false;
		}
		AGFMagicPlayerState* PS = Digger(W.World, TEXT("Spray"), Stand.At);
		if (!TestNotNull(TEXT("Spray"), PS))
		{
			return false;
		}
		PS->FactionId = TEXT("Cinderwrights");
		TestTrue(TEXT("Cinderwrights take Magma Spray"), PS->Magic->AuthorityToggle());
		TestEqual(TEXT("The field is magma"), PS->Magic->GetElement(), EGFMagicElement::Magma);
		Hold(PS, W.Magic, Stand.At, Stand.Impact, 0.2f);
		TestFalse(TEXT("The first globs wound without felling"), W.Magic->IsTownNodeDepleted(Tree));
		TestTrue(TEXT("Magma has started the wound"), W.Magic->GetTownNodeRemaining(Tree) < 100.f);
		Hold(PS, W.Magic, Stand.At, Stand.Impact, 1.5f);
		PS->Magic->SetFieldHeld(false);
		TestTrue(TEXT("Magma Spray fells the tree"), W.Magic->IsTownNodeDepleted(Tree));
		TestEqual(TEXT("Magma pays a wood plank"), PS->Inventory->GetCount(TEXT("WoodPlank")), 1);
		TestTrue(TEXT("The magma ribbon flew"), Ribbons(W.World) >= 1);
	}

	{
		FMineWorld W;
		if (!W.Open(*this, TEXT("GFTownMineFrost")))
		{
			return false;
		}
		UStaticMeshComponent* Tree = PlaceAsset(W, TEXT("SM_Town_ForestTree_C"), FVector::ZeroVector);
		UStaticMeshComponent* Wall = PlaceAsset(W, TEXT("SM_Town_Wall_Corner"), FVector(8000, 0, 0));
		if (!TestNotNull(TEXT("Frost tree"), Tree) || !TestNotNull(TEXT("Frost wall"), Wall))
		{
			return false;
		}
		const FStand TreeStand = StandOutside(W.World, Tree, 200.f, AnyFace);
		const FStand WallStand = StandOutside(W.World, Wall, 200.f, AnyFace);
		if (!TestTrue(TEXT("Frost face"), TreeStand.bOk) || !TestTrue(TEXT("Frost wall face"), WallStand.bOk))
		{
			return false;
		}
		AGFMagicPlayerState* PS = Digger(W.World, TEXT("Frost"), TreeStand.At);
		if (!TestNotNull(TEXT("Frost"), PS))
		{
			return false;
		}
		PS->FactionId = TEXT("Rime");
		TestTrue(TEXT("Rime takes Frostbind"), PS->Magic->AuthorityToggle());
		TestEqual(TEXT("The field is frost"), PS->Magic->GetElement(), EGFMagicElement::Frost);
		Hold(PS, W.Magic, TreeStand.At, TreeStand.Impact, 0.5f);
		TestFalse(TEXT("Frostbind has not shattered yet"), W.Magic->IsTownNodeDepleted(Tree));
		Hold(PS, W.Magic, TreeStand.At, TreeStand.Impact, 2.8f);
		PS->Magic->SetFieldHeld(false);
		TestTrue(TEXT("Frostbind shatters the tree"), W.Magic->IsTownNodeDepleted(Tree));
		TestEqual(TEXT("Frostbind pays a wood plank"), PS->Inventory->GetCount(TEXT("WoodPlank")), 1);
		TestTrue(TEXT("The frost ribbon flew"), Ribbons(W.World) >= 1);
		Hold(PS, W.Magic, WallStand.At, WallStand.Impact, 1.f);
		PS->Magic->SetFieldHeld(false);
		TestEqual(TEXT("Frostbind does not open a wall"), W.Magic->GetTownNodeRemaining(Wall), -1.f);
		TestFalse(TEXT("The corner stays visible"), Wall->GetOwner()->IsHidden());
	}
	return true;
}

// towns_v2 meshes ride the same node rules: whole trees fell and pay a plank, crystal slots spend slot 1 and pay a
// crystal, and refused v2 props (lava, crane, pipes) never open a node.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownsV2MineBreakTest, "GFGame.TownMineable.V2Break", GFTownMineTestFlags)
bool FGFTownsV2MineBreakTest::RunTest(const FString&)
{
	using namespace GFTownMineTest;
	FMineWorld W;
	if (!W.Open(*this, TEXT("GFTownsV2MineBreak")))
	{
		return false;
	}
	UStaticMeshComponent* Pine = PlaceAsset(W, TEXT("SM_Town_Rime_SnowPine_A"), FVector::ZeroVector);
	UStaticMeshComponent* Ash = PlaceAsset(W, TEXT("SM_Town_Cinderwright_AshTree_A"), FVector(8000, 0, 0));
	UStaticMeshComponent* House = PlaceAsset(W, TEXT("SM_Town_Rime_House_Small"), FVector(16000, 0, 0));
	UStaticMeshComponent* Lava = PlaceAsset(W, TEXT("SM_Town_Cinderwright_LavaField_Pool"), FVector(24000, 0, 0));
	UStaticMeshComponent* Crane = PlaceAsset(W, TEXT("SM_Town_Cinderwright_Crane"), FVector(32000, 0, 0));
	if (!TestNotNull(TEXT("Pine"), Pine) || !TestNotNull(TEXT("Ash"), Ash) || !TestNotNull(TEXT("House"), House) || !TestNotNull(TEXT("Lava"), Lava) || !TestNotNull(TEXT("Crane"), Crane))
	{
		return false;
	}
	TestEqual(TEXT("A v2 pine starts at log integrity"), W.Magic->GetTownNodeRemaining(Pine), 100.f);
	TestEqual(TEXT("A v2 crystal slot starts at crystal-vein integrity"), W.Magic->GetTownNodeRemaining(House), 200.f);
	auto AnyFace = [](const FHitResult&) { return true; };
	auto CrystalFace = [](const FHitResult& Hit)
	{
		FString Why;
		return FGFMineable::CanMineHit(Hit, Why) && FGFMineable::SlotFromHit(Hit) == 1;
	};
	const FStand PineStand = StandOutside(W.World, Pine, 200.f, AnyFace);
	const FStand AshStand = StandOutside(W.World, Ash, 200.f, AnyFace);
	const FStand HouseStand = StandOutside(W.World, House, 200.f, CrystalFace);
	if (!TestTrue(TEXT("Pine face"), PineStand.bOk) || !TestTrue(TEXT("Ash face"), AshStand.bOk) || !TestTrue(TEXT("House crystal face"), HouseStand.bOk))
	{
		return false;
	}
	AGFMagicPlayerState* PS = Digger(W.World, TEXT("Digger"), PineStand.At);
	if (!TestNotNull(TEXT("Digger"), PS))
	{
		return false;
	}
	Hold(PS, W.Magic, PineStand.At, PineStand.Impact, 1.6f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("Sparkcone fells the snow pine"), W.Magic->IsTownNodeDepleted(Pine));
	TestTrue(TEXT("The pine is hidden"), Pine->GetOwner()->IsHidden());
	TestEqual(TEXT("The pine pays a plank"), PS->Inventory->GetCount(TEXT("WoodPlank")), 1);
	Hold(PS, W.Magic, AshStand.At, AshStand.Impact, 1.6f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("Sparkcone fells the ash tree"), W.Magic->IsTownNodeDepleted(Ash));
	TestEqual(TEXT("The ash tree pays a plank"), PS->Inventory->GetCount(TEXT("WoodPlank")), 2);
	UMaterialInterface* Body = House->GetMaterial(0);
	Hold(PS, W.Magic, HouseStand.At, HouseStand.Impact, 1.6f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("Sparkcone spends the house crystal"), W.Magic->IsTownNodeDepleted(House));
	TestFalse(TEXT("The house stays visible"), House->GetOwner()->IsHidden());
	TestTrue(TEXT("The house still blocks"), House->GetOwner()->GetActorEnableCollision());
	TestEqual(TEXT("House slot 1 wears the body material"), House->GetMaterial(1), Body);
	TestEqual(TEXT("The house pays a crystal"), PS->Inventory->GetCount(TEXT("Crystal")), 1);
	for (UStaticMeshComponent* Prop : { Lava, Crane })
	{
		const FStand Stand = StandOutside(W.World, Prop, 180.f, AnyFace);
		if (!TestTrue(TEXT("Prop face"), Stand.bOk))
		{
			return false;
		}
		const int32 BeforeWood = PS->Inventory->GetCount(TEXT("WoodPlank"));
		const int32 BeforeCrystal = PS->Inventory->GetCount(TEXT("Crystal"));
		Hold(PS, W.Magic, Stand.At, Stand.Impact, 0.4f);
		PS->Magic->SetFieldHeld(false);
		TestEqual(TEXT("A refused v2 mesh has no node"), W.Magic->GetTownNodeRemaining(Prop), -1.f);
		TestFalse(TEXT("A refused v2 mesh stays visible"), Prop->GetOwner()->IsHidden());
		TestEqual(TEXT("A refused v2 mesh pays no wood"), PS->Inventory->GetCount(TEXT("WoodPlank")), BeforeWood);
		TestEqual(TEXT("A refused v2 mesh pays no crystal"), PS->Inventory->GetCount(TEXT("Crystal")), BeforeCrystal);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownsV2MineHitsTest, "GFGame.TownMineable.V2Hits", GFTownMineTestFlags)
bool FGFTownsV2MineHitsTest::RunTest(const FString&)
{
	TArray<FGFMineableEntry> All;
	FString Err;
	if (!TestTrue(TEXT("towns_v2 whitelist loads"), FGFMineable::LoadTownsV2Whitelist(All, Err)))
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
	for (const FGFMineableEntry& E : All)
	{
		const FString Name = E.Asset.ToString();
		// Smoke plumes have no collision, so mining traces never reach them.
		if (Name.Contains(TEXT("SmokePlume")))
		{
			continue;
		}
		UStaticMesh* Mesh = LoadMesh(E.Path);
		if (!TestNotNull(Name + TEXT(" loads"), Mesh))
		{
			continue;
		}
		// 120 m apart so traces only find the mesh under test (the largest pieces are about 30 m across).
		UStaticMeshComponent* C = W.Place(Mesh, FVector(12000.0 * Slot++, 0.0, 0.0));
		if (!C)
		{
			continue;
		}
		const int32 Every = SampleEvery(Mesh);
		if (E.Mode == EGFMineableMode::Slots)
		{
			const FTally T = TraceMesh(*this, W, *Name, C, Every, [](int32 S) { return S == 1; }, PS);
			TestTrue(Name + TEXT(": a crystal-slot hit is allowed"), T.AllowedCrystal > 0);
			TestTrue(Name + TEXT(": a main-slot hit is refused"), T.RefusedMain > 0);
		}
		else if (E.Mode == EGFMineableMode::WholeMesh)
		{
			const FTally T = TraceMesh(*this, W, *Name, C, Every, [](int32) { return true; }, PS);
			TestTrue(Name + TEXT(": a tree hit is allowed"), T.Allowed > 0 && T.Refused == 0);
		}
		else
		{
			const FTally T = TraceMesh(*this, W, *Name, C, Every, [](int32) { return false; }, PS);
			TestTrue(Name + TEXT(": hits are refused"), T.Refused > 0 && T.Allowed == 0);
		}
	}
	W.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetTownMineTest, "GFGame.Net.TownMineable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetTownMineTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFTownMineTest;
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> Key = MakeShared<FString>();
	TSharedRef<int32> WoodBefore = MakeShared<int32>(0);
	TSharedRef<bool> SawRibbon = MakeShared<bool>(false);

	Do(Ctx, [this, Key, WoodBefore]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFGameState* GS = GameState(Server);
		AGFPlayerState* Mine = Client ? LocalPlayerState(Client) : nullptr;
		AGFPlayerState* OnServer = Mine ? PlayerStateByKey(Server, Mine->PlayerKey) : nullptr;
		AGFVoxelWorld* Grid = Voxel(Server);
		if (!TestNotNull(TEXT("Server digger"), OnServer) || !TestNotNull(TEXT("Client digger"), Mine) || !GS || !Grid)
		{
			return;
		}
		*Key = Mine->PlayerKey;
		GS->bFreeBuild = true;
		GS->bEnforcePlotPermissions = false;
		GS->MaxEditReach = 0.f;
		OnServer->bCharacterReady = true;
		APawn* Pawn = OnServer->GetPawn();
		if (!TestNotNull(TEXT("Pawn"), Pawn))
		{
			return;
		}
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
				Move->Velocity = FVector::ZeroVector;
				Move->GravityScale = 0.f;
				Move->DisableMovement();
				Move->SetComponentTickEnabled(false);
			}
		}
		if (APawn* ClientPawn = Mine->GetPawn())
		{
			if (ACharacter* Character = Cast<ACharacter>(ClientPawn))
			{
				if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
				{
					Move->GravityScale = 0.f;
					Move->DisableMovement();
				}
			}
		}
		TArray<FGFMineableEntry> All;
		FString Err;
		const FGFMineableEntry* Entry = FGFMineable::LoadTownWhitelist(All, Err) ? FindEntry(All, TEXT("SM_Town_ForestTree_A")) : nullptr;
		UStaticMesh* Mesh = Entry ? LoadMesh(Entry->Path) : nullptr;
		if (!TestNotNull(TEXT("Tree mesh"), Mesh))
		{
			return;
		}
		const FVector Plant = Grid->CellToWorld(FIntVector(8, 8, 6)) + FVector(0, 0, 800);
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = Server->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Plant, FRotator::ZeroRotator, Params);
		if (!TestNotNull(TEXT("Tree actor"), Actor))
		{
			return;
		}
		Actor->SetReplicates(true);
		Actor->bAlwaysRelevant = true;
		Actor->FinishSpawning(FTransform(FRotator::ZeroRotator, Plant));
		UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Comp->RecreatePhysicsState();
		const FStand Stand = StandOutside(Server, Comp, 200.f, [](const FHitResult&) { return true; });
		if (!TestTrue(TEXT("Server can see the tree"), Stand.bOk))
		{
			return;
		}
		Pawn->SetActorLocation(Stand.At, false, nullptr, ETeleportType::TeleportPhysics);
		*WoodBefore = OnServer->Inventory ? OnServer->Inventory->GetCount(TEXT("WoodPlank")) : 0;
		if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
		{
			Magic->SetAim(Stand.At, Stand.Impact);
		}
	});
	WaitUntil(Ctx, TEXT("the server is aiming at the tree"), 5.f, [Key]()
	{
		AGFPlayerState* OnServer = PlayerStateByKey(ServerWorld(), *Key);
		UGFMagicComponent* Magic = OnServer ? OnServer->FindComponentByClass<UGFMagicComponent>() : nullptr;
		APawn* Pawn = OnServer ? OnServer->GetPawn() : nullptr;
		return Magic && Pawn && FVector::Dist(Magic->GetAimPoint(), Pawn->GetActorLocation()) > 50.f && FVector::Dist(Magic->GetAimPoint(), Pawn->GetActorLocation()) < 500.f;
	});
	Do(Ctx, []()
	{
		if (AGFPlayerState* Mine = LocalPlayerState(ClientWorlds()[0]))
		{
			if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
			{
				Magic->SetFieldHeld(true);
			}
		}
	});
	WaitUntil(Ctx, TEXT("the tree breaks and the owner receives the plank"), 10.f, [Key, WoodBefore, SawRibbon]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFPlayerState* OnServer = PlayerStateByKey(Server, *Key);
		AGFPlayerState* Mine = Client ? LocalPlayerState(Client) : nullptr;
		UGFBlockIntegritySubsystem* Magic = Server ? Server->GetSubsystem<UGFBlockIntegritySubsystem>() : nullptr;
		bool bDepleted = false;
		if (Server && Magic)
		{
			for (TActorIterator<AStaticMeshActor> It(Server); It; ++It)
			{
				if (UStaticMeshComponent* Comp = It->GetStaticMeshComponent())
				{
					if (Magic->IsTownNodeDepleted(Comp))
					{
						bDepleted = true;
					}
				}
			}
		}
		if (Ribbons(Server) > 0 || Ribbons(Client) > 0)
		{
			*SawRibbon = true;
		}
		bool bClientRelay = false;
		if (Client)
		{
			for (TActorIterator<AGFTownMineRelay> It(Client); It; ++It)
			{
				for (const FGFTownNodeRep& Node : It->GetNodes())
				{
					bClientRelay = bClientRelay || Node.bDepleted;
				}
			}
		}
		return OnServer && Mine && OnServer->Inventory && Mine->Inventory
			&& bDepleted
			&& OnServer->Inventory->GetCount(TEXT("WoodPlank")) == *WoodBefore + 1
			&& Mine->Inventory->GetCount(TEXT("WoodPlank")) == *WoodBefore + 1
			&& bClientRelay
			&& *SawRibbon;
	});
	Stop(Ctx);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
