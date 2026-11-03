// The game starts in the world map, in a clearing in the Verdant woods (Tools/town_clearing.py picks it, and
// Tools/place_towns_v2.py places PlayerStart_Clearing there). Every open area keeps a gate back to the hub map.

#include "Misc/AutomationTest.h"
#include "GFHubGateDirector.h"
#include "GFMiniGameCatalog.h"
#include "GFReturnToHub.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFStartAreaTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static const TCHAR* WorldMap = TEXT("/Game/Maps/L_World_Factions_A");
	static const TCHAR* WorldObject = TEXT("/Game/Maps/L_World_Factions_A.L_World_Factions_A");
	static constexpr float ClearCm = 1000.f;   // town_clearing.CLEAR_MIN_CM
	static constexpr float RingCm = 4000.f;    // town_clearing.WOOD_RING_CM
	static constexpr int32 PerQuadrant = 3;    // town_clearing.MIN_QUADRANT

	struct FBareWorld
	{
		UWorld* World = nullptr;

		FBareWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFStartAreaWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FBareWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** XY reach of a placed mesh from its actor origin (the placer's footprint test uses the same idea). */
	static float Reach(const AStaticMeshActor* Actor, const UStaticMesh* Mesh)
	{
		const FBoxSphereBounds B = Mesh->GetBounds();
		const FVector S = Actor->GetActorScale3D().GetAbs();
		return FMath::Sqrt(FMath::Square(FMath::Abs(B.Origin.X) + B.BoxExtent.X) + FMath::Square(FMath::Abs(B.Origin.Y) + B.BoxExtent.Y)) * FMath::Max(S.X, S.Y);
	}

	static bool IsGround(const UStaticMesh* Mesh)
	{
		const FString Path = Mesh->GetPathName();
		return Path.StartsWith(TEXT("/Engine/")) || Path.Contains(TEXT("GroundSlab"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStartAreaDefaultMapTest, "GFGame.StartArea.DefaultMap", GFStartAreaTests::Flags)
bool FGFStartAreaDefaultMapTest::RunTest(const FString&)
{
	FString Game, Editor;
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), Game, GEngineIni);
	GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("EditorStartupMap"), Editor, GEngineIni);
	TestEqual(TEXT("Game starts in the world map"), Game, FString(GFStartAreaTests::WorldObject));
	TestEqual(TEXT("Editor opens the world map"), Editor, FString(GFStartAreaTests::WorldObject));
	TestTrue(TEXT("World map is on disk"), FPackageName::DoesPackageExist(GFStartAreaTests::WorldMap));
	TestTrue(TEXT("World map is an area"), FGFReturnToHub::IsAreaMap(GFStartAreaTests::WorldMap));
	TestTrue(TEXT("PIE world map is an area"), FGFReturnToHub::IsAreaMap(TEXT("/Game/Maps/UEDPIE_0_L_World_Factions_A")));
	TestFalse(TEXT("Hub is not an area"), FGFReturnToHub::IsAreaMap(FGFMiniGameCatalog::HubMap().ToString()));
	for (const FName& Map : FGFReturnToHub::AreaMaps())
	{
		TestTrue(*FString::Printf(TEXT("%s is on disk"), *Map.ToString()), FPackageName::DoesPackageExist(Map.ToString()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStartAreaClearingTest, "GFGame.StartArea.Clearing", GFStartAreaTests::Flags)
bool FGFStartAreaClearingTest::RunTest(const FString&)
{
	using namespace GFStartAreaTests;
	UWorld* World = LoadObject<UWorld>(nullptr, WorldObject);
	if (!TestNotNull(TEXT("World map loads"), World) || !TestNotNull(TEXT("Persistent level"), World->PersistentLevel.Get()))
	{
		return false;
	}
	TArray<APlayerStart*> Starts;
	TArray<AStaticMeshActor*> Meshes;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (APlayerStart* Start = Cast<APlayerStart>(Actor))
		{
			Starts.Add(Start);
		}
		else if (AStaticMeshActor* SM = Cast<AStaticMeshActor>(Actor))
		{
			Meshes.Add(SM);
		}
	}
	// One start, like every other placed map; AGFGameMode spreads co-op pawns 150 cm apart from it.
	if (!TestEqual(TEXT("One player start"), Starts.Num(), 1))
	{
		return false;
	}
	const FVector At = Starts[0]->GetActorLocation();
	TestTrue(TEXT("Start stands 120 cm over the ground"), FMath::IsNearlyEqual(At.Z, 120.f, 1.f));
	TestFalse(TEXT("Start is off the starter voxel plot"), FMath::Abs(At.X) < 3400.f && FMath::Abs(At.Y) < 3400.f);

	int32 Trees = 0;
	int32 Quad[4] = {0, 0, 0, 0};
	bool bGround = false;
	float NearestOther = TNumericLimits<float>::Max();
	float NearestTree = TNumericLimits<float>::Max();
	for (const AStaticMeshActor* SM : Meshes)
	{
		const UStaticMesh* Mesh = SM->GetStaticMeshComponent() ? SM->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
		if (!Mesh)
		{
			continue;
		}
		const FVector L = SM->GetActorLocation();
		if (IsGround(Mesh))
		{
			const FBox Box = SM->GetStaticMeshComponent()->CalcBounds(SM->GetActorTransform()).GetBox();
			if (At.X >= Box.Min.X && At.X <= Box.Max.X && At.Y >= Box.Min.Y && At.Y <= Box.Max.Y && FMath::Abs(Box.Max.Z) < 5.f
				&& Mesh->GetPathName().StartsWith(TEXT("/Engine/")) && SM->GetStaticMeshComponent()->GetMaterial(0)
				&& SM->GetStaticMeshComponent()->GetMaterial(0)->GetPathName().Contains(TEXT("Grass")))
			{
				bGround = true;
			}
			continue;
		}
		const float D = FVector::Dist2D(L, At);
		const float Edge = D - Reach(SM, Mesh);
		if (Mesh->GetName().Contains(TEXT("ForestTree")))
		{
			NearestTree = FMath::Min(NearestTree, Edge);
			if (D <= RingCm)
			{
				++Trees;
				++Quad[(L.X >= At.X ? 1 : 0) + (L.Y >= At.Y ? 2 : 0)];
			}
		}
		else
		{
			NearestOther = FMath::Min(NearestOther, Edge);
		}
	}
	AddInfo(FString::Printf(TEXT("Start (%.0f, %.0f, %.0f): %d Verdant trees within 40 m (%d/%d/%d/%d), nearest tree edge %.0f cm, nearest other %.0f cm"),
		At.X, At.Y, At.Z, Trees, Quad[0], Quad[1], Quad[2], Quad[3], NearestTree, NearestOther));
	TestTrue(TEXT("Start is on the grass ground"), bGround);
	// 50 cm slack: the placer measures footprints from the kit manifests, this from the imported mesh bounds.
	TestTrue(TEXT("Open ground 10 m round the start"), NearestTree >= ClearCm - 50.f && NearestOther >= ClearCm - 50.f);
	TestTrue(TEXT("No walls, roads, water, lava or buildings within 20 m"), NearestOther >= 2 * ClearCm - 50.f);
	TestTrue(TEXT("Woods all round"), Quad[0] >= PerQuadrant && Quad[1] >= PerQuadrant && Quad[2] >= PerQuadrant && Quad[3] >= PerQuadrant);
	TestTrue(TEXT("A real wood"), Trees >= 30);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStartAreaReturnGateTest, "GFGame.StartArea.ReturnGate", GFStartAreaTests::Flags)
bool FGFStartAreaReturnGateTest::RunTest(const FString&)
{
	using namespace GFStartAreaTests;
	const TCHAR* Maps[] = {WorldMap, TEXT("/Game/Maps/UEDPIE_0_L_World_Factions_A"), TEXT("/Game/Maps/L_Town_Rime_A"), TEXT("/Game/Maps/L_Homestead_Verdant_A")};
	for (const TCHAR* Map : Maps)
	{
		FBareWorld W;
		if (!TestNotNull(TEXT("World"), W.World))
		{
			return false;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector StartAt(5746.f, 5727.f, 120.f);
		const FRotator Facing(0.f, -135.f, 0.f);
		APlayerStart* Start = W.World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), StartAt, Facing, Params);
		AGFHubGateDirector* Director = W.World->SpawnActor<AGFHubGateDirector>();
		if (!TestNotNull(TEXT("Start"), Start) || !TestNotNull(TEXT("Director"), Director))
		{
			return false;
		}
		Director->Place(Map, FVector::ZeroVector);
		Director->Place(Map, FVector::ZeroVector);
		int32 Gates = 0;
		for (TActorIterator<AGFReturnGate> It(W.World); It; ++It)
		{
			++Gates;
		}
		TestEqual(*FString::Printf(TEXT("%s: one return gate, placed twice"), Map), Gates, 1);
		AGFReturnGate* Gate = AGFReturnGate::Find(W.World, AGFReturnGate::AreaModeId());
		if (!TestNotNull(*FString::Printf(TEXT("%s return gate"), Map), Gate))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s gate travels to the hub map"), Map), Gate->IsMapTravel());
		const FVector G = Gate->GetActorLocation();
		TestTrue(*FString::Printf(TEXT("%s gate is on the ground"), Map), FMath::IsNearlyZero(G.Z, 1.f));
		TestTrue(*FString::Printf(TEXT("%s gate is 8 m from the start"), Map), FMath::IsNearlyEqual(FVector::Dist2D(G, StartAt), AGFReturnGate::AreaBehindCm, 1.f));
		TestTrue(*FString::Printf(TEXT("%s gate is behind the start"), Map), FVector::DotProduct((G - StartAt).GetSafeNormal2D(), Facing.Vector()) < -0.99f);
		// Co-op pawns land up to (450, 150) cm off the start; none of them spawns in the trigger.
		for (int32 Index = 0; Index < 10; ++Index)
		{
			const FVector Pawn = StartAt + FVector((Index % 4) * 150.f, (Index / 4) * 150.f, 0.f);
			TestTrue(TEXT("Co-op spawn clear of the gate"), FVector::Dist2D(Pawn, G) > 300.f);
		}

		ADefaultPawn* Walker = W.World->SpawnActor<ADefaultPawn>(G + FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator, Params);
		if (!TestNotNull(TEXT("Walker"), Walker))
		{
			continue;
		}
		const FGFReturnResult Walk = Gate->TryReturn(Walker, true);
		TestTrue(*FString::Printf(TEXT("%s gate moves you"), Map), Walk.bMoved);
		TestEqual(*FString::Printf(TEXT("%s gate goes to the hub map"), Map), Walk.TravelMap, FGFMiniGameCatalog::HubMap().ToString());
	}

	// No player start: the gate still stands at the mode-map spot.
	{
		FBareWorld W;
		AGFHubGateDirector* Director = W.World ? W.World->SpawnActor<AGFHubGateDirector>() : nullptr;
		if (TestNotNull(TEXT("Director without a start"), Director))
		{
			Director->Place(TEXT("/Game/Maps/L_Town_Verdant_A"), FVector::ZeroVector);
			AGFReturnGate* Gate = AGFReturnGate::Find(W.World, AGFReturnGate::AreaModeId());
			TestTrue(TEXT("Startless area gate at MapLocation"), Gate && Gate->GetActorLocation().Equals(AGFReturnGate::MapLocation(), 1.f));
		}
	}
	// The hub gets no area gate.
	{
		FBareWorld W;
		AGFHubGateDirector* Director = W.World ? W.World->SpawnActor<AGFHubGateDirector>() : nullptr;
		if (TestNotNull(TEXT("Hub director"), Director))
		{
			Director->Place(FGFMiniGameCatalog::HubMap().ToString(), FVector::ZeroVector);
			TestNull(TEXT("No area gate on the hub"), AGFReturnGate::Find(W.World, AGFReturnGate::AreaModeId()));
		}
	}
	return true;
}

#endif
