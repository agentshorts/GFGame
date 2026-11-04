// Decision #320. Whitelisted trees and crystals ignore faction land and visitor gates.
// Voxel edits keep those gates. Mining FX rows point at VoxelFinish starter graphs,
// and the hit / crack / break event fires from the integrity subsystem and town nodes.
#include "Misc/AutomationTest.h"
#include "GFBlockIntegritySubsystem.h"
#include "GFGameState.h"
#include "GFMagicActors.h"
#include "GFMagicComponent.h"
#include "GFMagicPlayerState.h"
#include "GFMineable.h"
#include "GFMiningFX.h"
#include "GFPlayerState.h"
#include "GFSafeZone.h"
#include "GFSocialDirector.h"
#include "GFSocialTypes.h"
#include "GFVoxelWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFMiningLandFXFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

namespace GFMiningLandFX
{
	static UStaticMesh* LoadMesh(const TCHAR* Path)
	{
		const FString Name = FPackageName::GetLongPackageAssetName(Path);
		return LoadObject<UStaticMesh>(nullptr, *(FString(Path) + TEXT(".") + Name));
	}

	static int32 FaceInSlot(const UStaticMesh* Mesh, int32 Want)
	{
		const FStaticMeshRenderData* RD = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!RD || !RD->LODResources.IsValidIndex(Mesh->LODForCollision))
		{
			return INDEX_NONE;
		}
		int32 Faces = 0;
		for (const FStaticMeshSection& Section : RD->LODResources[Mesh->LODForCollision].Sections)
		{
			if (!Section.bEnableCollision)
			{
				continue;
			}
			if (Section.MaterialIndex == Want && Section.NumTriangles > 0)
			{
				return Faces;
			}
			Faces += Section.NumTriangles;
		}
		return INDEX_NONE;
	}

	static FHitResult MakeHit(UPrimitiveComponent* Comp, const FVector& At, int32 Face)
	{
		FHitResult Hit;
		Hit.Component = Comp;
		Hit.bBlockingHit = true;
		Hit.ImpactPoint = At;
		Hit.Location = At;
		Hit.FaceIndex = Face;
		Hit.ImpactNormal = FVector::UpVector;
		return Hit;
	}

	struct FLands : public IGFSafeZoneQuery
	{
		virtual bool IsInsideSafe(const FVector& Location) const override { return !OwnerAt(Location).IsNone(); }
		virtual FVector ClosestSafePoint(const FVector& Location) const override { return Location; }
		virtual bool ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const override
		{
			OutPoint = Location;
			OutOutward = FVector::ForwardVector;
			return false;
		}
		virtual bool HasAnySafe() const override { return true; }
		virtual FName OwnerAt(const FVector& Location) const override
		{
			return Location.X >= 10000.f ? FName(TEXT("Verdant")) : FName(TEXT("Rime"));
		}
	};

	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		AGFVoxelWorld* Voxel = nullptr;
		UGFBlockIntegritySubsystem* Magic = nullptr;

		bool Open(FAutomationTestBase& Test, const TCHAR* Name, bool bVoxel, bool bGameState)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World || !GEngine)
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
			if (bGameState)
			{
				GS = World->SpawnActor<AGFGameState>();
				if (GS)
				{
					World->SetGameState(GS);
				}
			}
			if (bVoxel)
			{
				FActorSpawnParameters Params;
				Params.bDeferConstruction = true;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Voxel = World->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform::Identity, Params);
				if (Voxel)
				{
					Voxel->PlotSizeInBlocks = FIntVector(16, 16, 8);
					Voxel->StarterGroundLayers = 0;
					Voxel->FinishSpawning(FTransform::Identity);
				}
			}
			Magic = World->GetSubsystem<UGFBlockIntegritySubsystem>();
			return Test.TestNotNull(TEXT("World"), World)
				&& (!bGameState || Test.TestNotNull(TEXT("Game state"), GS))
				&& (!bVoxel || Test.TestNotNull(TEXT("Voxel"), Voxel))
				&& Test.TestNotNull(TEXT("Integrity subsystem"), Magic);
		}

		UStaticMeshComponent* Place(UStaticMesh* Mesh, const FVector& At) const
		{
			if (!World || !Mesh)
			{
				return nullptr;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator, Params);
			if (!Actor)
			{
				return nullptr;
			}
			UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetStaticMesh(Mesh);
			Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Comp->RecreatePhysicsState();
			return Comp;
		}

		void Destroy()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		~FWorld() { Destroy(); }
	};

	struct FListen
	{
		TArray<FGFMiningFXEvent> Events;
		FDelegateHandle Handle;

		FListen()
		{
			Handle = FGFMiningFX::OnPlayed().AddLambda([this](const FGFMiningFXEvent& Event) { Events.Add(Event); });
		}

		~FListen()
		{
			FGFMiningFX::OnPlayed().Remove(Handle);
		}

		int32 Count(EGFMiningFXKind Kind, EGFMiningFXKit Kit) const
		{
			int32 N = 0;
			for (const FGFMiningFXEvent& Event : Events)
			{
				if (Event.Kind == Kind && Event.Kit == Kit)
				{
					++N;
				}
			}
			return N;
		}
	};

	struct FTri
	{
		FVector A, B, C;
	};

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
		for (const FTriangleID Id : MD->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> V = MD->GetTriangleVertices(Id);
			FTri Tri;
			Tri.A = FVector(Pos[V[0]]);
			Tri.B = FVector(Pos[V[1]]);
			Tri.C = FVector(Pos[V[2]]);
			Out.Add(Tri);
		}
		return Out;
	}

	struct FStand
	{
		FVector At = FVector::ZeroVector;
		FVector Impact = FVector::ZeroVector;
		bool bOk = false;
	};

	static FStand StandOutside(UWorld* World, UStaticMeshComponent* Comp, float Distance)
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
		auto Accept = [&](const FHitResult& Hit) -> bool
		{
			if (Hit.GetComponent() != Comp)
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
			if (Check.GetComponent() != Comp)
			{
				return false;
			}
			Out.Impact = Check.ImpactPoint;
			Out.At = Stand;
			Out.bOk = true;
			return true;
		};
		for (const FVector& Dir : Dirs)
		{
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Center + Dir * Reach, Center, ECC_Visibility, FGFMineable::QueryParams()) && Accept(Hit))
			{
				return Out;
			}
		}
		const TArray<FTri> Tris = Triangles(Comp->GetStaticMesh());
		const FTransform Xform = Comp->GetComponentTransform();
		const int32 Step = FMath::Max(1, Tris.Num() / 48);
		for (int32 Index = 0; Index < Tris.Num() && !Out.bOk; Index += Step)
		{
			const FTri& Tri = Tris[Index];
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
				if (World->LineTraceSingleByChannel(Hit, Start, Mid - N * (Side * 2.f), ECC_Visibility, FGFMineable::QueryParams()) && Accept(Hit))
				{
					return Out;
				}
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningLandRulesTest, "GFGame.MagicMining.LandRules", GFMiningLandFXFlags)
bool FGFMiningLandRulesTest::RunTest(const FString&)
{
	using namespace GFMiningLandFX;
	FWorld W;
	if (!W.Open(*this, TEXT("GFMiningLand"), true, true))
	{
		return false;
	}
	W.GS->OverworldEditQuery.BindLambda([](FName FactionId, const FVector& Where)
	{
		if (Where.X >= 800.f)
		{
			return EGFOverworldEdit::Deny;
		}
		return FactionId == FName(TEXT("Rime")) ? EGFOverworldEdit::Own : EGFOverworldEdit::Foreign;
	});
	UGFSocialComponent* Social = AGFSocialDirector::GetSocial(W.World, true);
	if (!TestNotNull(TEXT("Social"), Social))
	{
		return false;
	}
	UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(W.World);
	if (!TestNotNull(TEXT("Zones"), Zones))
	{
		return false;
	}
	Zones->SetOverride(MakeShared<FLands>());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Rime player"), PS))
	{
		return false;
	}
	PS->PlayerKey = TEXT("RimeMiner");
	PS->FactionId = TEXT("Rime");
	PS->bIsHost = false;
	PS->bCharacterReady = true;

	const FIntVector Home(2, 2, 2);
	const FIntVector Wild(12, 2, 2);
	FString Why;
	TestTrue(TEXT("Rime may edit a voxel on Rime land"), PS->CanMineAt(W.Voxel->CellToWorld(Home), Home, Why));
	TestFalse(TEXT("Rime may not edit a voxel outside Rime land"), PS->CanMineAt(W.Voxel->CellToWorld(Wild), Wild, Why));
	TestEqual(TEXT("The refusal is faction land"), Why, FString(TEXT("outside faction land")));

	UStaticMesh* TreeMesh = LoadMesh(TEXT("/Game/Art/Town/Outskirts/Forest/SM_Town_ForestTree_A"));
	UStaticMesh* CrystalMesh = LoadMesh(TEXT("/Game/Art/Town/TownsV2/Cinderwright/SM_Town_Cinderwright_Stall_Coilback"));
	UStaticMesh* WallMesh = LoadMesh(TEXT("/Game/Art/Town/Walls/SM_Town_Wall_Straight"));
	if (!TestNotNull(TEXT("Verdant forest tree mesh"), TreeMesh) || !TestNotNull(TEXT("Other town crystal mesh"), CrystalMesh) || !TestNotNull(TEXT("Wall mesh"), WallMesh))
	{
		return false;
	}
	const FVector TreeAt(20000.f, 0.f, 0.f);
	const FVector CrystalAt(22000.f, 4000.f, 0.f);
	const FVector WallAt(24000.f, 0.f, 0.f);
	UStaticMeshComponent* Tree = W.Place(TreeMesh, TreeAt);
	UStaticMeshComponent* Crystal = W.Place(CrystalMesh, CrystalAt);
	UStaticMeshComponent* Wall = W.Place(WallMesh, WallAt);
	if (!TestNotNull(TEXT("Tree placed"), Tree) || !TestNotNull(TEXT("Crystal placed"), Crystal) || !TestNotNull(TEXT("Wall placed"), Wall))
	{
		return false;
	}
	TestEqual(TEXT("Verdant owns the forest"), Zones->OwnerAt(TreeAt), FName(TEXT("Verdant")));
	TestEqual(TEXT("A Rime visitor has no rights there"), W.GS->EditBlockReason(PS, TreeAt), FGFSocialCodes::VisitorNoRights);
	TestEqual(TEXT("The forest is outside Rime land"), W.GS->OverworldEditQuery.Execute(PS->FactionId, TreeAt), EGFOverworldEdit::Deny);

	FHitResult TreeHit = MakeHit(Tree, TreeAt, INDEX_NONE);
	TestTrue(TEXT("Tree hit keeps its component"), TreeHit.GetComponent() == Tree);
	TestTrue(TEXT("Rime can mine a Verdant forest tree"), PS->CanMineHit(TreeHit, Why));

	const int32 CrystalFace = FaceInSlot(CrystalMesh, 1);
	if (!TestTrue(TEXT("Crystal slot has a collision face"), CrystalFace != INDEX_NONE))
	{
		return false;
	}
	FHitResult CrystalHit = MakeHit(Crystal, CrystalAt, CrystalFace);
	TestEqual(TEXT("The face is the crystal slot"), FGFMineable::SlotFromHit(CrystalHit), 1);
	TestEqual(TEXT("The stall is another town"), W.GS->EditBlockReason(PS, CrystalAt), FGFSocialCodes::VisitorNoRights);
	TestTrue(TEXT("Rime can mine another town's crystal"), PS->CanMineHit(CrystalHit, Why));

	FHitResult WallHit = MakeHit(Wall, WallAt, 0);
	TestFalse(TEXT("A wall is still not mineable"), PS->CanMineHit(WallHit, Why));
	TestFalse(TEXT("Wild voxels stay denied after the mesh hits"), PS->CanMineAt(W.Voxel->CellToWorld(Wild), Wild, Why));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineableTagAuditTest, "GFGame.TownMineable.TagAudit", GFMiningLandFXFlags)
bool FGFMineableTagAuditTest::RunTest(const FString&)
{
	FGFMineable::StartKitAudit();
	UStaticMesh* Tree = GFMiningLandFX::LoadMesh(TEXT("/Game/Art/Town/Outskirts/Forest/SM_Town_ForestTree_A"));
	if (!TestNotNull(TEXT("Forest tree"), Tree))
	{
		return false;
	}
	TestNotNull(TEXT("The imported tree carries its tag"), FGFMineable::Find(Tree));
	TestFalse(TEXT("A tagged kit mesh is not a missing-tag warning"), FGFMineable::ListedWithoutTag(Tree));

	UStaticMesh* Bare = FindObject<UStaticMesh>(GetTransientPackage(), TEXT("SM_Town_Barrel"));
	if (!Bare)
	{
		Bare = NewObject<UStaticMesh>(GetTransientPackage(), TEXT("SM_Town_Barrel"));
	}
	if (!TestNotNull(TEXT("Untagged barrel name"), Bare))
	{
		return false;
	}
	Bare->RemoveUserDataOfClass(UGFMineableAssetUserData::StaticClass());
	TestTrue(TEXT("A listed mesh with no tag is the warning case"), FGFMineable::ListedWithoutTag(Bare));
	TestTrue(TEXT("The warning reports that mesh"), FGFMineable::WarnIfListedWithoutTag(Bare));
	TestTrue(TEXT("A second check still reports it"), FGFMineable::WarnIfListedWithoutTag(Bare));

	UStaticMesh* Other = NewObject<UStaticMesh>(GetTransientPackage());
	TestFalse(TEXT("An unlisted mesh is not a kit warning"), FGFMineable::ListedWithoutTag(Other));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFXTableTest, "GFGame.MagicMining.FXTable", GFMiningLandFXFlags)
bool FGFMiningFXTableTest::RunTest(const FString&)
{
	const UDataTable* Table = FGFMiningFX::Table();
	if (!TestNotNull(TEXT("DT_MiningFX"), Table))
	{
		return false;
	}
	bool bSparkle = false;
	bool bLava = false;
	bool bFrost = false;
	bool bPulse = false;
	const TArray<FName> Names = Table->GetRowNames();
	TestTrue(TEXT("FX table has rows"), Names.Num() >= 4);
	for (const FName& Name : Names)
	{
		const FGFMiningFXRow* Row = Table->FindRow<FGFMiningFXRow>(Name, TEXT("FXTable"), false);
		if (!TestNotNull(*Name.ToString(), Row))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s resolves to a file"), *Name.ToString()), FGFMiningFX::RowResolves(*Row));
		const FString File = FGFMiningFX::ResolveFile(Row->SystemPath);
		const FSoftObjectPath Soft = FGFMiningFX::SoftPathFor(Row->SystemPath);
		TestFalse(*FString::Printf(TEXT("%s has a soft path"), *Name.ToString()), Soft.IsNull());
		if (File.Contains(TEXT("fx_sparkle")))
		{
			bSparkle = true;
			TestTrue(TEXT("Sparkle soft path"), Soft.ToString().Contains(TEXT("/VoxelFinish/Effects/Starter/fx_sparkle_rev2.fx_sparkle_rev2")));
		}
		bLava |= File.Contains(TEXT("fx_lava_embers"));
		bFrost |= File.Contains(TEXT("fx_frosted_sparkle"));
		bPulse |= File.Contains(TEXT("fx_pulse_glow"));
	}
	TestTrue(TEXT("fx_sparkle"), bSparkle);
	TestTrue(TEXT("fx_lava_embers"), bLava);
	TestTrue(TEXT("fx_frosted_sparkle"), bFrost);
	TestTrue(TEXT("fx_pulse_glow"), bPulse);

	TestNull(TEXT("Spark beam has no Niagara asset yet"), FGFMiningFX::LoadFor(EGFMiningFXKind::Beam, EGFMagicElement::Spark, EGFMiningFXKit::Any));
	TestNull(TEXT("Magma beam has no Niagara asset yet"), FGFMiningFX::LoadFor(EGFMiningFXKind::Beam, EGFMagicElement::Magma, EGFMiningFXKit::Any));
	TestNull(TEXT("Frost beam has no Niagara asset yet"), FGFMiningFX::LoadFor(EGFMiningFXKind::Beam, EGFMagicElement::Frost, EGFMiningFXKit::Any));
	TestNull(TEXT("Root beam has no Niagara asset yet"), FGFMiningFX::LoadFor(EGFMiningFXKind::Beam, EGFMagicElement::Root, EGFMiningFXKit::Any));
	TestNull(TEXT("A second load still returns null"), FGFMiningFX::LoadFor(EGFMiningFXKind::Beam, EGFMagicElement::Spark, EGFMiningFXKit::Any));

	const FVector At(1.f, 2.f, 3.f);
	TArray<FGFMiningFXEvent> Events;
	FGFMiningFX::Collect(50.f, 40.f, 50.f, false, EGFMagicElement::Spark, EGFMiningFXKit::None, At, FVector::UpVector, Events);
	TestEqual(TEXT("First chip is a hit"), Events.Num(), 1);
	TestEqual(TEXT("Kind is hit"), static_cast<int32>(Events[0].Kind), static_cast<int32>(EGFMiningFXKind::Hit));
	Events.Reset();
	FGFMiningFX::Collect(40.f, 30.f, 50.f, false, EGFMagicElement::Spark, EGFMiningFXKit::None, At, FVector::UpVector, Events);
	TestEqual(TEXT("Crossing 75% is hit plus crack"), Events.Num(), 2);
	TestEqual(TEXT("Crack stage 1"), Events[1].CrackStage, 1);
	Events.Reset();
	FGFMiningFX::Collect(10.f, 0.f, 50.f, true, EGFMagicElement::Spark, EGFMiningFXKit::Tree, At, FVector::UpVector, Events);
	TestEqual(TEXT("A break is one event"), Events.Num(), 1);
	TestEqual(TEXT("Kind is break"), static_cast<int32>(Events[0].Kind), static_cast<int32>(EGFMiningFXKind::Break));
	TestEqual(TEXT("Kit is tree"), static_cast<int32>(Events[0].Kit), static_cast<int32>(EGFMiningFXKit::Tree));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFXEventTest, "GFGame.MagicMining.FXEvents", GFMiningLandFXFlags)
bool FGFMiningFXEventTest::RunTest(const FString&)
{
	using namespace GFMiningLandFX;
	FWorld W;
	if (!W.Open(*this, TEXT("GFMiningFX"), true, false))
	{
		return false;
	}
	const FIntVector Dirt(2, 2, 2);
	W.Voxel->ServerApplyEdit(Dirt, TEXT("Dirt"));
	const FVector DirtAt = W.Voxel->CellToWorld(Dirt);
	AGFMagicPlayerState* PS = W.World->SpawnActor<AGFMagicPlayerState>();
	if (!TestNotNull(TEXT("Digger"), PS) || !TestNotNull(TEXT("Magic component"), PS->Magic))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Digger");
	PS->Magic->BindAvatar(nullptr);
	PS->Magic->SetManualOrigin(DirtAt);
	PS->Magic->SetAim(DirtAt, DirtAt);
	PS->Magic->SetFieldHeld(true);

	FListen Listen;
	W.Magic->Advance(0.1f);
	TestTrue(TEXT("A voxel hit fires"), Listen.Count(EGFMiningFXKind::Hit, EGFMiningFXKit::None) >= 1);
	TestEqual(TEXT("Dirt has not broken yet"), Listen.Count(EGFMiningFXKind::Break, EGFMiningFXKit::None), 0);
	W.Magic->Advance(0.1f);
	TestTrue(TEXT("The next hit crosses a crack stage"), Listen.Events.ContainsByPredicate([](const FGFMiningFXEvent& Event)
	{
		return Event.Kind == EGFMiningFXKind::Crack && Event.CrackStage >= 1 && Event.Kit == EGFMiningFXKit::None;
	}));
	W.Magic->Advance(0.4f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("The dirt break fires"), Listen.Count(EGFMiningFXKind::Break, EGFMiningFXKit::None) >= 1);
	TestTrue(TEXT("The dirt cell is gone"), W.Voxel->GetBlock(Dirt).IsNone());

	UStaticMesh* TreeMesh = LoadMesh(TEXT("/Game/Art/Town/Outskirts/Forest/SM_Town_ForestTree_A"));
	if (!TestNotNull(TEXT("Tree mesh"), TreeMesh))
	{
		return false;
	}
	UStaticMeshComponent* Tree = W.Place(TreeMesh, FVector(8000.f, 0.f, 0.f));
	if (!TestNotNull(TEXT("Tree"), Tree))
	{
		return false;
	}
	const FStand Stand = StandOutside(W.World, Tree, 80.f);
	if (!TestTrue(TEXT("A face to mine"), Stand.bOk))
	{
		return false;
	}
	const int32 HitsBefore = Listen.Count(EGFMiningFXKind::Hit, EGFMiningFXKit::Tree);
	const int32 BreaksBefore = Listen.Count(EGFMiningFXKind::Break, EGFMiningFXKit::Tree);
	PS->Magic->SetManualOrigin(Stand.At);
	PS->Magic->SetAim(Stand.At, Stand.Impact);
	PS->Magic->SetFieldHeld(true);
	W.Magic->Advance(0.2f);
	TestTrue(TEXT("A tree hit fires"), Listen.Count(EGFMiningFXKind::Hit, EGFMiningFXKit::Tree) > HitsBefore);
	W.Magic->Advance(1.4f);
	PS->Magic->SetFieldHeld(false);
	TestTrue(TEXT("The tree break fires"), Listen.Count(EGFMiningFXKind::Break, EGFMiningFXKit::Tree) > BreaksBefore);
	TestTrue(TEXT("The tree is spent"), W.Magic->IsTownNodeDepleted(Tree));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFMagicFieldMarker* Marker = W.World->SpawnActor<AGFMagicFieldMarker>(AGFMagicFieldMarker::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Field marker"), Marker))
	{
		return false;
	}
	Marker->SetBeam(EGFMagicElement::Spark, FVector::ForwardVector, 15.f, 800.f, 5000.f);
	const bool bResolved = FGFMiningFX::LoadFor(EGFMiningFXKind::Beam, EGFMagicElement::Spark, EGFMiningFXKit::Any) != nullptr;
	TestEqual(TEXT("Missing Niagara keeps the mesh"), Marker->IsUsingMeshFallback(), !bResolved);
	Marker->SetBeam(EGFMagicElement::Root, FVector::ForwardVector, 15.f, 250.f, 0.f);
	TestEqual(TEXT("Rootmaul keeps the mesh when its system is missing"), Marker->IsUsingMeshFallback(), FGFMiningFX::LoadFor(EGFMiningFXKind::Beam, EGFMagicElement::Root, EGFMiningFXKit::Any) == nullptr);
	return true;
}

#endif
