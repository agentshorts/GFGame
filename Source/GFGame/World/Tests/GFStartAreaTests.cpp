// The game starts in the world map, at the rest area on the Verdant to Cinderwright road (issue 335: Tools/rest_area.py
// reads the rest area layout and Tools/place_towns_v2.py places PlayerStart_RestArea, the caravan and its markers there).
// Every open area keeps a gate back to the hub map.

#include "Misc/AutomationTest.h"
#include "GFHubGateDirector.h"
#include "GFMiniGameCatalog.h"
#include "GFRestAreaSpawn.h"
#include "GFReturnToHub.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
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
#include "Misc/ConfigCacheIni.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFStartAreaTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static const TCHAR* WorldMap = TEXT("/Game/Maps/L_World_Factions_A");
	static const TCHAR* WorldObject = TEXT("/Game/Maps/L_World_Factions_A.L_World_Factions_A");
	// Rest_Area_Layout_v1 world placements (the art handoff): player start (24000, -2400, 0) yaw 0, lifted 120 cm like
	// every placed start; the Road_T replaces the link road straight at the city midpoint.
	static const FVector RestStart(24000.f, -2400.f, 120.f);
	static const FVector Midpoint(24000.f, 0.f, 0.f);
	static const FVector GatherExtent(700.f, 700.f, 200.f);
	static constexpr float RestRadiusCm = 2500.f;       // every caravan piece of the layout is within this of the start
	static constexpr float JunctionClearCm = 300.f;     // accel_hop_layout.JUNCTION_CLEAR_CM
	static const FName PlaceholderTag(TEXT("GFRestAreaPlaceholder"));  // rest_area.PLACEHOLDER_TAG

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStartAreaRestAreaTest, "GFGame.StartArea.RestArea", GFStartAreaTests::Flags)
bool FGFStartAreaRestAreaTest::RunTest(const FString&)
{
	using namespace GFStartAreaTests;
	UWorld* World = LoadObject<UWorld>(nullptr, WorldObject);
	if (!TestNotNull(TEXT("World map loads"), World) || !TestNotNull(TEXT("Persistent level"), World->PersistentLevel.Get()))
	{
		return false;
	}
	TArray<APlayerStart*> Starts;
	TArray<AStaticMeshActor*> Meshes;
	int32 NpcSpots = 0;
	int32 Placeholders = 0;
	int32 CampfireLights = 0;
	FVector LightAt = FVector::ZeroVector;
	const AActor* Gather = nullptr;
	TArray<FVector> HopTiles;
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (!Actor)
		{
			continue;
		}
		if (APlayerStart* Start = Cast<APlayerStart>(Actor))
		{
			Starts.Add(Start);
		}
		else if (AStaticMeshActor* SM = Cast<AStaticMeshActor>(Actor))
		{
			Meshes.Add(SM);
		}
		NpcSpots += Actor->ActorHasTag(FGFRestAreaSpawn::NpcSpotTag) ? 1 : 0;
		Placeholders += Actor->ActorHasTag(PlaceholderTag) ? 1 : 0;
		if (Actor->ActorHasTag(FGFRestAreaSpawn::CampfireLightTag) && Actor->FindComponentByClass<UPointLightComponent>())
		{
			++CampfireLights;
			LightAt = Actor->GetActorLocation();
		}
		if (Actor->ActorHasTag(FGFRestAreaSpawn::GatherZoneTag))
		{
			Gather = Actor;
		}
		for (const FName& Tag : Actor->Tags)
		{
			if (Tag.ToString().StartsWith(TEXT("GFAccelHop_")))
			{
				HopTiles.Add(Actor->GetActorLocation());
				break;
			}
		}
	}

	// One start, like every other placed map; AGFGameMode spreads co-op pawns 150 cm apart from it.
	if (!TestEqual(TEXT("One player start"), Starts.Num(), 1))
	{
		return false;
	}
	const APlayerStart* Start = Starts[0];
	TestEqual(TEXT("Start is tagged RestArea"), Start->PlayerStartTag, FGFRestAreaSpawn::StartTag);
	TestTrue(TEXT("Start at the rest area plaza, 120 cm up"), Start->GetActorLocation().Equals(RestStart, 1.f));
	TestTrue(TEXT("Start faces +X, toward Cinderwright"), FMath::IsNearlyZero(FRotator::NormalizeAxis(Start->GetActorRotation().Yaw), 0.5f));
	TestFalse(TEXT("Start is off the starter voxel plot"), FMath::Abs(RestStart.X) < 3400.f && FMath::Abs(RestStart.Y) < 3400.f);

	TestEqual(TEXT("Ten caravan NPC spots"), NpcSpots, 10);
	if (TestNotNull(TEXT("Gather zone"), Gather))
	{
		TestTrue(TEXT("Gather zone centred on the plaza"), FVector::Dist2D(Gather->GetActorLocation(), RestStart) < 1.f);
		const UBoxComponent* Box = Gather->FindComponentByClass<UBoxComponent>();
		TestTrue(TEXT("Gather zone is 700 x 700 x 200 cm half extents"), Box && Box->GetUnscaledBoxExtent().Equals(GatherExtent, 1.f));
	}

	int32 Pens = 0, Carts = 0, PensNearRest = 0, CartsNearRest = 0, RoadT = 0;
	int32 Campfires = 0, Boards = 0, Wagons = 0, Tents = 0;
	bool bStraightUnderT = false;
	FVector CampfireAt = FVector::ZeroVector;
	for (const AStaticMeshActor* SM : Meshes)
	{
		const UStaticMesh* Mesh = SM->GetStaticMeshComponent() ? SM->GetStaticMeshComponent()->GetStaticMesh() : nullptr;
		if (!Mesh)
		{
			continue;
		}
		const FString Name = Mesh->GetName();
		const FVector L = SM->GetActorLocation();
		const bool bNearRest = FVector::Dist2D(L, RestStart) < RestRadiusCm;
		if (Name == TEXT("SM_Town_CaravanPen"))
		{
			++Pens;
			PensNearRest += bNearRest ? 1 : 0;
		}
		else if (Name == TEXT("SM_Town_CaravanCart"))
		{
			++Carts;
			CartsNearRest += bNearRest ? 1 : 0;
		}
		else if (Name == TEXT("SM_Town_Road_T") && FVector::Dist2D(L, Midpoint) < 1.f)
		{
			++RoadT;
		}
		else if (Name == TEXT("SM_Town_Road_Straight") && FVector::Dist2D(L, Midpoint) < 1.f)
		{
			bStraightUnderT = true;
		}
		else if (Name == TEXT("SM_Town_Caravan_Campfire"))
		{
			++Campfires;
			CampfireAt = L;
		}
		else if (Name == TEXT("SM_Town_Caravan_NoticeBoard"))
		{
			++Boards;
		}
		else if (Name == TEXT("SM_Town_Caravan_CoveredWagon"))
		{
			++Wagons;
		}
		else if (Name == TEXT("SM_Town_Caravan_Tent"))
		{
			++Tents;
		}
	}
	// The caravan moved: one pen and two carts, all at the rest area; the Verdant yard is gone.
	TestEqual(TEXT("One caravan pen"), Pens, 1);
	TestEqual(TEXT("Two caravan carts"), Carts, 2);
	TestEqual(TEXT("The pen is at the rest area"), PensNearRest, 1);
	TestEqual(TEXT("Both carts are at the rest area"), CartsNearRest, 2);
	TestEqual(TEXT("Road_T at the city midpoint"), RoadT, 1);
	TestFalse(TEXT("No straight under the Road_T"), bStraightUnderT);
	for (const FVector& Tile : HopTiles)
	{
		if (FMath::Abs(Tile.Y - Midpoint.Y) < 300.f)
		{
			TestTrue(TEXT("No hop tile on the Road_T"), FMath::Abs(Tile.X - Midpoint.X) >= JunctionClearCm - 1.f);
		}
	}
	TestTrue(TEXT("Hop tiles are still on the road"), HopTiles.Num() > 0);

	// All four structures are real meshes; the one tent mesh stands twice.
	TestEqual(TEXT("Campfire mesh"), Campfires, 1);
	TestEqual(TEXT("Notice board mesh"), Boards, 1);
	TestEqual(TEXT("Covered wagon mesh"), Wagons, 1);
	TestEqual(TEXT("Two tents"), Tents, 2);
	TestEqual(TEXT("No structure placeholders left"), Placeholders, 0);
	TestEqual(TEXT("One campfire light"), CampfireLights, 1);
	TestTrue(TEXT("The light is over the campfire"), CampfireLights == 1 && Campfires == 1 && FVector::Dist2D(LightAt, CampfireAt) < 1.f && LightAt.Z > CampfireAt.Z);
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
