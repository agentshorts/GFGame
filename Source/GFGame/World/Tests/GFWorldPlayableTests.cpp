// Floor coverage is pure maths (no world). The sky test uses a transient world:
// a directional light that is already there must stay the only one.

#include "Misc/AutomationTest.h"
#include "GFEnvironmentDirector.h"
#include "GFOverworldGround.h"
#include "GFTerritorySubsystem.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFWorldPlayableTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FGFOverworldShape* FindShape(const TArray<FGFOverworldShape>& Shapes, EGFOverworldShapeKind Kind, FName Id = NAME_None)
	{
		for (const FGFOverworldShape& Shape : Shapes)
		{
			if (Shape.Kind == Kind && (Id.IsNone() || Shape.Id == Id))
			{
				return &Shape;
			}
		}
		return nullptr;
	}

	static bool AnyCover(const TArray<FGFOverworldShape>& Shapes, const FVector2D& Point)
	{
		for (const FGFOverworldShape& Shape : Shapes)
		{
			if (Shape.CoversXY(Point))
			{
				return true;
			}
		}
		return false;
	}

	static int32 CountKind(const TArray<FGFOverworldShape>& Shapes, EGFOverworldShapeKind Kind)
	{
		int32 Count = 0;
		for (const FGFOverworldShape& Shape : Shapes)
		{
			Count += Shape.Kind == Kind ? 1 : 0;
		}
		return Count;
	}

	static float LargestRadius(const TArray<float>& Radii)
	{
		float Best = 0.f;
		for (const float Radius : Radii)
		{
			Best = FMath::Max(Best, Radius);
		}
		return FMath::Max(0.f, Best);
	}

	static bool HasColour(const TArray<FGFOverworldTint>& Table, FName Id, const FLinearColor& Colour)
	{
		for (const FGFOverworldTint& Tint : Table)
		{
			if (Tint.Id == Id && Tint.Color.Equals(Colour))
			{
				return true;
			}
		}
		return false;
	}

	template <typename ComponentClass>
	static int32 CountComponents(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<ComponentClass*> Found;
			It->GetComponents(Found);
			Count += Found.Num();
		}
		return Count;
	}

	struct FEnvWorld
	{
		UWorld* World = nullptr;

		FEnvWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFEnvSkipWorld"));
			if (!World)
			{
				return;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FEnvWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFOverworldCoversLayoutTest, "GFGame.World.OverworldCoversLayout", GFWorldPlayableTests::Flags)
bool FGFOverworldCoversLayoutTest::RunTest(const FString&)
{
	using namespace GFWorldPlayableTests;

	FGFOverworldParams Params;
	Params.Margin = 200.f;
	Params.Thickness = 50.f;
	Params.SurfaceZ = -2.f;
	Params.OverlayLift = 1.f;
	FGFOverworldTint RimeTint;
	RimeTint.Id = TEXT("Rime");
	RimeTint.Color = FLinearColor(0.1f, 0.2f, 0.3f, 1.f);
	Params.FactionColours.Add(RimeTint);

	FGFTerritoryFaction Rime;
	Rime.Id = TEXT("Rime");
	Rime.Center = FVector(0.f, 0.f, 9.f);
	Rime.RadiusByMembers = { 0.f, 100.f, 40.f };

	FGFTerritoryFaction Bare;
	Bare.Id = TEXT("Bare");
	Bare.Center = FVector(800.f, 100.f, 0.f);
	Bare.RadiusByMembers = { 0.f };

	FGFTerritoryHazard Lava;
	Lava.Id = TEXT("LavaBridge");
	Lava.Kind = TEXT("LavaBridge");
	Lava.Center = FVector(500.f, 0.f, 4.f);
	Lava.HalfExtent = FVector2D(40.f, 20.f);

	const TArray<FGFOverworldShape> Shapes = AGFOverworldGround::BuildShapes({ Rime, Bare }, { Lava }, Params);
	const FGFOverworldShape* Slab = FindShape(Shapes, EGFOverworldShapeKind::Slab);
	const FGFOverworldShape* Disc = FindShape(Shapes, EGFOverworldShapeKind::FactionDisc, TEXT("Rime"));
	const FGFOverworldShape* Box = FindShape(Shapes, EGFOverworldShapeKind::HazardBox, TEXT("LavaBridge"));
	if (!TestNotNull(TEXT("Slab"), Slab) || !TestNotNull(TEXT("Rime disc"), Disc) || !TestNotNull(TEXT("Lava box"), Box))
	{
		return false;
	}

	TestNull(TEXT("Zero radius has no disc"), FindShape(Shapes, EGFOverworldShapeKind::FactionDisc, TEXT("Bare")));
	TestEqual(TEXT("One slab"), CountKind(Shapes, EGFOverworldShapeKind::Slab), 1);
	TestEqual(TEXT("One disc"), CountKind(Shapes, EGFOverworldShapeKind::FactionDisc), 1);
	TestEqual(TEXT("One box"), CountKind(Shapes, EGFOverworldShapeKind::HazardBox), 1);

	TestTrue(TEXT("Rime centre has ground"), AnyCover(Shapes, FVector2D(Rime.Center.X, Rime.Center.Y)));
	TestTrue(TEXT("Bare centre has ground"), AnyCover(Shapes, FVector2D(Bare.Center.X, Bare.Center.Y)));
	TestTrue(TEXT("Lava centre has ground"), AnyCover(Shapes, FVector2D(Lava.Center.X, Lava.Center.Y)));
	TestTrue(TEXT("Disc covers Rime"), Disc->CoversXY(FVector2D(Rime.Center.X, Rime.Center.Y)));
	TestTrue(TEXT("Slab covers Rime"), Slab->CoversXY(FVector2D(Rime.Center.X, Rime.Center.Y)));
	TestTrue(TEXT("Box covers lava"), Box->CoversXY(FVector2D(Lava.Center.X, Lava.Center.Y)));

	TestTrue(TEXT("Disc uses the largest radius entry"), FMath::IsNearlyEqual(Disc->DiscRadius, 100.f));
	TestTrue(TEXT("Cylinder scale is the diameter over 100 cm"), FMath::IsNearlyEqual(Disc->Scale.X, 2.f));
	TestTrue(TEXT("Cylinder scale is uniform in X and Y"), FMath::IsNearlyEqual(Disc->Scale.X, Disc->Scale.Y));
	TestTrue(TEXT("Box keeps the hazard half extent"), Box->HalfExtent.Equals(FVector2D(40.f, 20.f)));
	TestFalse(TEXT("Just outside the box"), Box->CoversXY(FVector2D(Lava.Center.X + 50.f, Lava.Center.Y)));
	TestTrue(TEXT("That point is still on the slab"), Slab->CoversXY(FVector2D(Lava.Center.X + 50.f, Lava.Center.Y)));

	TestTrue(TEXT("Slab top is just under Z=0"), FMath::IsNearlyEqual(Slab->TopZ, -2.f));
	TestTrue(TEXT("Rings sit above the slab"), FMath::IsNearlyEqual(Disc->TopZ, -1.f) && FMath::IsNearlyEqual(Box->TopZ, -1.f));
	TestTrue(TEXT("Tops stay under the voxel ground"), Slab->TopZ < 0.f && Disc->TopZ < 0.f && Box->TopZ < 0.f);
	TestTrue(TEXT("Config tint is used"), Disc->Color.Equals(RimeTint.Color));
	TestTrue(TEXT("Known hazard tint fallback"), Box->Color.Equals(FLinearColor(0.95f, 0.40f, 0.08f, 1.f)));

	// Rime ±100, bare (800, 100), lava x 460..540. Bounds X -100..800, Y -100..100. Margin 200 → X -300..1000, Y -300..300.
	TestTrue(TEXT("Slab centre X"), FMath::IsNearlyEqual(Slab->Center.X, 350.f));
	TestTrue(TEXT("Slab centre Y"), FMath::IsNearlyEqual(Slab->Center.Y, 0.f));
	TestTrue(TEXT("Slab half X includes the margin"), FMath::IsNearlyEqual(Slab->HalfExtent.X, 650.f));
	TestTrue(TEXT("Slab half Y includes the margin"), FMath::IsNearlyEqual(Slab->HalfExtent.Y, 300.f));
	TestTrue(TEXT("Slab edge is ground"), Slab->CoversXY(FVector2D(Slab->Center.X + Slab->HalfExtent.X, Slab->Center.Y)));
	TestFalse(TEXT("Past the margin is not ground"), AnyCover(Shapes, FVector2D(Slab->Center.X + Slab->HalfExtent.X + 25.f, Slab->Center.Y)));
	TestTrue(TEXT("Cube scale matches the slab"), FMath::IsNearlyEqual(Slab->Scale.X, Slab->HalfExtent.X * 2.f / 100.f));
	TestTrue(TEXT("Thickness scales the 100 cm mesh"), FMath::IsNearlyEqual(Slab->Scale.Z, 0.5f));

	const TArray<FGFOverworldShape> Empty = AGFOverworldGround::BuildShapes({}, {}, Params);
	const FGFOverworldShape* EmptySlab = FindShape(Empty, EGFOverworldShapeKind::Slab);
	if (TestNotNull(TEXT("Empty layout still has a slab"), EmptySlab))
	{
		TestTrue(TEXT("Empty slab covers the origin"), EmptySlab->CoversXY(FVector2D::ZeroVector));
		TestTrue(TEXT("Empty slab is the margin"), FMath::IsNearlyEqual(EmptySlab->HalfExtent.X, Params.Margin));
	}

	FGFOverworldParams Untinted;
	Untinted.Margin = 10.f;
	Untinted.Thickness = 10.f;
	Untinted.SurfaceZ = -2.f;
	Untinted.OverlayLift = 1.f;
	FGFTerritoryFaction Plain;
	Plain.Id = TEXT("Rime");
	Plain.Center = FVector::ZeroVector;
	Plain.RadiusByMembers = { 30.f };
	const TArray<FGFOverworldShape> UntintedShapes = AGFOverworldGround::BuildShapes({ Plain }, {}, Untinted);
	const FGFOverworldShape* FallbackDisc = FindShape(UntintedShapes, EGFOverworldShapeKind::FactionDisc, TEXT("Rime"));
	if (TestNotNull(TEXT("Fallback disc"), FallbackDisc))
	{
		TestTrue(TEXT("Rime defaults to pale ice blue"), FallbackDisc->Color.Equals(FLinearColor(0.75f, 0.88f, 0.96f, 1.f)));
	}

	const UGFTerritorySubsystem* Territory = GetDefault<UGFTerritorySubsystem>();
	const AGFOverworldGround* Ground = GetDefault<AGFOverworldGround>();
	if (!TestNotNull(TEXT("Territory defaults"), Territory) || !TestNotNull(TEXT("Ground defaults"), Ground))
	{
		return false;
	}

	TestTrue(TEXT("Rime colour is configured"), HasColour(Ground->FactionColours, TEXT("Rime"), FLinearColor(0.75f, 0.88f, 0.96f, 1.f)));
	TestTrue(TEXT("Verdant colour is configured"), HasColour(Ground->FactionColours, TEXT("Verdant"), FLinearColor(0.22f, 0.62f, 0.28f, 1.f)));
	TestTrue(TEXT("Cinderwrights colour is configured"), HasColour(Ground->FactionColours, TEXT("Cinderwrights"), FLinearColor(0.55f, 0.52f, 0.48f, 1.f)));
	TestTrue(TEXT("LavaBridge colour is configured"), HasColour(Ground->HazardColours, TEXT("LavaBridge"), FLinearColor(0.95f, 0.40f, 0.08f, 1.f)));
	TestTrue(TEXT("DeadlyForest colour is configured"), HasColour(Ground->HazardColours, TEXT("DeadlyForest"), FLinearColor(0.05f, 0.22f, 0.08f, 1.f)));

	const FGFOverworldParams Shipped = Ground->CurrentParams();
	const TArray<FGFOverworldShape> Layout = AGFOverworldGround::BuildShapes(Territory->FactionLayout, Territory->Hazards, Shipped);
	const FGFOverworldShape* ShippedSlab = FindShape(Layout, EGFOverworldShapeKind::Slab);
	if (!TestNotNull(TEXT("Shipped slab"), ShippedSlab))
	{
		return false;
	}

	TestTrue(TEXT("Shipped layout has factions"), Territory->FactionLayout.Num() > 0);
	TestTrue(TEXT("Shipped layout has hazards"), Territory->Hazards.Num() > 0);
	TestTrue(TEXT("Shipped slab top"), FMath::IsNearlyEqual(ShippedSlab->TopZ, -2.f));

	int32 ExpectedDiscs = 0;
	for (const FGFTerritoryFaction& Faction : Territory->FactionLayout)
	{
		const FVector2D Centre(Faction.Center.X, Faction.Center.Y);
		TestTrue(*FString::Printf(TEXT("%s centre has ground"), *Faction.Id.ToString()), AnyCover(Layout, Centre));
		TestTrue(*FString::Printf(TEXT("%s centre is on the slab"), *Faction.Id.ToString()), ShippedSlab->CoversXY(Centre));
		const float Radius = GFWorldPlayableTests::LargestRadius(Faction.RadiusByMembers);
		const FGFOverworldShape* Ring = FindShape(Layout, EGFOverworldShapeKind::FactionDisc, Faction.Id);
		if (Radius > 0.f)
		{
			++ExpectedDiscs;
			if (TestNotNull(*FString::Printf(TEXT("%s disc"), *Faction.Id.ToString()), Ring))
			{
				TestTrue(*FString::Printf(TEXT("%s uses its largest radius"), *Faction.Id.ToString()), FMath::IsNearlyEqual(Ring->DiscRadius, Radius));
				TestTrue(*FString::Printf(TEXT("%s disc scale"), *Faction.Id.ToString()), FMath::IsNearlyEqual(Ring->Scale.X, Radius * 2.f / 100.f));
				TestTrue(*FString::Printf(TEXT("%s ring covers its centre"), *Faction.Id.ToString()), Ring->CoversXY(Centre));
				TestTrue(*FString::Printf(TEXT("%s ring is under Z=0"), *Faction.Id.ToString()), Ring->TopZ < 0.f && Ring->TopZ > ShippedSlab->TopZ);
			}
		}
		else
		{
			TestNull(*FString::Printf(TEXT("%s has no disc"), *Faction.Id.ToString()), Ring);
		}
	}
	TestEqual(TEXT("One disc per faction with a radius"), CountKind(Layout, EGFOverworldShapeKind::FactionDisc), ExpectedDiscs);

	for (const FGFTerritoryHazard& Hazard : Territory->Hazards)
	{
		const FVector2D Centre(Hazard.Center.X, Hazard.Center.Y);
		TestTrue(*FString::Printf(TEXT("%s centre has ground"), *Hazard.Id.ToString()), AnyCover(Layout, Centre));
		const FGFOverworldShape* HazardBox = FindShape(Layout, EGFOverworldShapeKind::HazardBox, Hazard.Id);
		if (TestNotNull(*FString::Printf(TEXT("%s box"), *Hazard.Id.ToString()), HazardBox))
		{
			TestTrue(*FString::Printf(TEXT("%s half extent"), *Hazard.Id.ToString()),
				HazardBox->HalfExtent.Equals(FVector2D(FMath::Max(0.f, Hazard.HalfExtent.X), FMath::Max(0.f, Hazard.HalfExtent.Y))));
			TestTrue(*FString::Printf(TEXT("%s box covers its centre"), *Hazard.Id.ToString()), HazardBox->CoversXY(Centre));
		}
	}
	TestEqual(TEXT("One box per hazard"), CountKind(Layout, EGFOverworldShapeKind::HazardBox), Territory->Hazards.Num());

	TestFalse(TEXT("Outside the shipped margin"), AnyCover(Layout, FVector2D(ShippedSlab->Center.X + ShippedSlab->HalfExtent.X + 50.f, ShippedSlab->Center.Y)));
	for (const FGFOverworldShape& Shape : Layout)
	{
		TestTrue(TEXT("Every shipped top is under Z=0"), Shape.TopZ < 0.f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEnvironmentSkipsPlacedTest, "GFGame.World.EnvironmentSkipsPlaced", GFWorldPlayableTests::Flags)
bool FGFEnvironmentSkipsPlacedTest::RunTest(const FString&)
{
	using namespace GFWorldPlayableTests;

	FEnvWorld Env;
	if (!TestNotNull(TEXT("World created"), Env.World))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADirectionalLight* Placed = Env.World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Placed sun"), Placed))
	{
		return false;
	}

	TestEqual(TEXT("One placed sun"), CountComponents<UDirectionalLightComponent>(Env.World), 1);
	TestEqual(TEXT("No sky atmosphere yet"), CountComponents<USkyAtmosphereComponent>(Env.World), 0);
	TestEqual(TEXT("No sky light yet"), CountComponents<USkyLightComponent>(Env.World), 0);
	TestEqual(TEXT("No height fog yet"), CountComponents<UExponentialHeightFogComponent>(Env.World), 0);

	AGFEnvironmentDirector* Director = Env.World->SpawnActor<AGFEnvironmentDirector>(AGFEnvironmentDirector::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Director"), Director))
	{
		return false;
	}

	TestEqual(TEXT("Placed sun is not duplicated"), CountComponents<UDirectionalLightComponent>(Env.World), 1);
	TestEqual(TEXT("Sky atmosphere added"), CountComponents<USkyAtmosphereComponent>(Env.World), 1);
	TestEqual(TEXT("Sky light added"), CountComponents<USkyLightComponent>(Env.World), 1);
	TestEqual(TEXT("Height fog added"), CountComponents<UExponentialHeightFogComponent>(Env.World), 1);

	Director->EnsureEnvironment();
	TestEqual(TEXT("Second pass adds no sun"), CountComponents<UDirectionalLightComponent>(Env.World), 1);
	TestEqual(TEXT("Second pass adds no sky atmosphere"), CountComponents<USkyAtmosphereComponent>(Env.World), 1);
	TestEqual(TEXT("Second pass adds no sky light"), CountComponents<USkyLightComponent>(Env.World), 1);
	TestEqual(TEXT("Second pass adds no height fog"), CountComponents<UExponentialHeightFogComponent>(Env.World), 1);

	return true;
}

#endif
