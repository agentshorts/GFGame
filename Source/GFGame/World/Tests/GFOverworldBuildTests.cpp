// Hub terrain follows the peak faction discs. Build rights follow the live discs.
// Where live Rime and Verdant overlap, both may build. Territory still names one
// owner there; these checks fail if the overworld copies that tie.

#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "GFOverworldBuild.h"
#include "GFOverworldGround.h"
#include "GFTerritorySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFOverworldBuildTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FLinearColor Ember(217.f / 255.f, 84.f / 255.f, 43.f / 255.f, 1.f);
	static const FLinearColor OldCinderGrey(0.55f, 0.52f, 0.48f, 1.f);

	static bool Load(FGFOverworldLayout& Layout, FGFTerritoryMap& Map)
	{
		const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
		const AGFOverworldGround* Ground = GetDefault<AGFOverworldGround>();
		if (!Defaults || !Ground)
		{
			return false;
		}
		Layout = FGFOverworldBuild::Make(Defaults->Curve, Defaults->FactionLayout, Defaults->Hazards, Ground->Margin);
		Map = FGFTerritoryMap();
		Map.Curve = Defaults->Curve;
		Map.Neutral = Defaults->NeutralZone;
		Map.Factions = Defaults->FactionLayout;
		Map.Hazards = Defaults->Hazards;
		return Map.FindFaction(GFOverworldFaction::Rime())
			&& Map.FindFaction(GFOverworldFaction::Verdant())
			&& Map.FindFaction(GFOverworldFaction::Cinderwrights());
	}

	static bool SetCounts(FGFTerritoryMap& Map, int32 RimeMembers, int32 VerdantMembers, int32 CinderMembers)
	{
		return Map.SetMembers(GFOverworldFaction::Rime(), RimeMembers)
			&& Map.SetMembers(GFOverworldFaction::Verdant(), VerdantMembers)
			&& Map.SetMembers(GFOverworldFaction::Cinderwrights(), CinderMembers);
	}

	static void DestroyWorld(UWorld* World)
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFOverworldThemeTest, "GFGame.Overworld.Theme", GFOverworldBuildTests::Flags)
bool FGFOverworldThemeTest::RunTest(const FString&)
{
	using namespace GFOverworldBuildTests;

	FGFOverworldLayout Layout;
	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped overworld layout"), Load(Layout, Map)))
	{
		return false;
	}

	TestEqual(TEXT("Grid X"), Layout.GridSize.X, 216);
	TestEqual(TEXT("Grid Y"), Layout.GridSize.Y, 108);
	TestEqual(TEXT("Grid Z"), Layout.GridSize.Z, 16);
	TestEqual(TEXT("Surface layer"), Layout.SurfaceZ, 3);
	TestTrue(TEXT("Origin"), Layout.Origin.Equals(FVector(-9400.f, -5400.f, -400.f), 0.1f));
	const float SurfaceTop = Layout.Origin.Z + static_cast<float>(Layout.SurfaceZ + 1) * FGFOverworldLayout::BlockCm;
	TestTrue(TEXT("Surface top is world Z 0"), FMath::IsNearlyEqual(SurfaceTop, 0.f));

	const FIntVector Rime = Layout.SurfaceCell(FVector(0.f, 0.f, 0.f));
	TestEqual(TEXT("Rime surface is ice"), Layout.BlockAt(Rime), GFOverworldBlock::Ice());
	TestEqual(TEXT("Rime fill is stone"), Layout.BlockAt(FIntVector(Rime.X, Rime.Y, Rime.Z - 1)), GFOverworldBlock::Stone());
	TestTrue(TEXT("Rime has air above the ice"), Layout.BlockAt(FIntVector(Rime.X, Rime.Y, Rime.Z + 1)).IsNone());

	const FIntVector Verdant = Layout.SurfaceCell(FVector(-4400.f, 0.f, 0.f));
	TestEqual(TEXT("Verdant surface is grass"), Layout.BlockAt(Verdant), GFOverworldBlock::Grass());
	TestEqual(TEXT("Verdant fill is dirt"), Layout.BlockAt(FIntVector(Verdant.X, Verdant.Y, Verdant.Z - 1)), GFOverworldBlock::Dirt());

	const FIntVector Cinder = Layout.SurfaceCell(FVector(7200.f, 0.f, 0.f));
	TestEqual(TEXT("Cinderwrights surface is basalt"), Layout.BlockAt(Cinder), GFOverworldBlock::Basalt());
	TestEqual(TEXT("Cinderwrights fill is basalt"), Layout.BlockAt(FIntVector(Cinder.X, Cinder.Y, Cinder.Z - 1)), GFOverworldBlock::Basalt());

	const FIntVector Farm = Layout.SurfaceCell(FVector(-2200.f, 0.f, 0.f));
	TestEqual(TEXT("Rime and Verdant overlap is farmland"), Layout.BlockAt(Farm), GFOverworldBlock::Farmland());
	TestTrue(TEXT("Farmland has no trunk"), Layout.BlockAt(FIntVector(Farm.X, Farm.Y, Farm.Z + 1)).IsNone());

	const FIntVector ForestFloor = Layout.SurfaceCell(FVector(-2300.f, 0.f, 0.f));
	TestEqual(TEXT("Peak overlap covers the forest centre"), Layout.BlockAt(ForestFloor), GFOverworldBlock::Farmland());

	const FIntVector Lava = Layout.SurfaceCell(FVector(3700.f, 0.f, 0.f));
	TestEqual(TEXT("Lava bridge surface is lava"), Layout.BlockAt(Lava), GFOverworldBlock::Lava());
	TestEqual(TEXT("Lava bridge fill is basalt"), Layout.BlockAt(FIntVector(Lava.X, Lava.Y, Lava.Z - 1)), GFOverworldBlock::Basalt());

	const FIntVector Wild = Layout.SurfaceCell(FVector(0.f, 5000.f, 0.f));
	TestTrue(TEXT("North skirt is on the grid"), Layout.IsInside(Wild));
	const FName WildBlock = Layout.BlockAt(Wild);
	TestTrue(TEXT("Wild surface is dirt or stone"), WildBlock == GFOverworldBlock::Dirt() || WildBlock == GFOverworldBlock::Stone());
	TestFalse(TEXT("Wild is not ice"), WildBlock == GFOverworldBlock::Ice());
	TestFalse(TEXT("Wild is not grass"), WildBlock == GFOverworldBlock::Grass());
	TestFalse(TEXT("Wild is not basalt"), WildBlock == GFOverworldBlock::Basalt());
	TestFalse(TEXT("Wild is not farmland"), WildBlock == GFOverworldBlock::Farmland());
	TestFalse(TEXT("Wild is not lava"), WildBlock == GFOverworldBlock::Lava());
	TestFalse(TEXT("Wild is not leaves"), WildBlock == GFOverworldBlock::Leaves());
	TestFalse(TEXT("Wild is not a log"), WildBlock == GFOverworldBlock::Log());

	TestTrue(TEXT("Cinder disc edge is on the grid"), Layout.IsInside(Layout.WorldToCell(FVector(10200.f, 0.f, 0.f))));
	const FVector ExclusiveEdge(Layout.Origin.X + Layout.GridSize.X * FGFOverworldLayout::BlockCm, 0.f, 0.f);
	TestFalse(TEXT("The far X edge is exclusive"), Layout.IsInside(Layout.WorldToCell(ExclusiveEdge)));

	const FIntVector Jail = Layout.WorldToCell(FVector(0.f, 0.f, -5000.f));
	TestTrue(TEXT("Jail is below the grid"), Jail.Z < 0);
	TestTrue(TEXT("Jail cell is air"), Layout.BlockAt(Jail).IsNone());

	auto ColumnSolid = [this, &Layout](const FVector& World, const TCHAR* Label)
	{
		const FIntVector Cell = Layout.SurfaceCell(World);
		TestTrue(*FString::Printf(TEXT("%s is on the grid"), Label), Layout.IsInside(Cell));
		TestFalse(*FString::Printf(TEXT("%s surface is solid"), Label), Layout.BlockAt(Cell).IsNone());
		TestFalse(*FString::Printf(TEXT("%s fill is solid"), Label), Layout.BlockAt(FIntVector(Cell.X, Cell.Y, 0)).IsNone());
	};
	ColumnSolid(FVector(1200.f, 600.f, 0.f), TEXT("West ferry"));
	ColumnSolid(FVector(6000.f, 600.f, 0.f), TEXT("East ferry"));

	int32 Trunks = 0;
	for (int32 Y = 0; Y < Layout.GridSize.Y; ++Y)
	{
		for (int32 X = 0; X < Layout.GridSize.X; ++X)
		{
			if (Layout.BlockAt(FIntVector(X, Y, Layout.SurfaceZ + 1)) == GFOverworldBlock::Log())
			{
				++Trunks;
			}
		}
	}
	TestTrue(TEXT("Verdant has some trunks"), Trunks > 0);
	TestTrue(TEXT("Trunks are not a solid slab"), Trunks < 400);

	FGFTerritoryFaction Far;
	Far.Id = GFOverworldFaction::Rime();
	Far.Center = FVector(50000.f, 0.f, 0.f);
	Far.RadiusByMembers = { 0.f, 100.f };
	FGFTerritoryHazard Forest;
	Forest.Id = TEXT("DeadlyForest");
	Forest.Kind = TEXT("DeadlyForest");
	Forest.Center = FVector::ZeroVector;
	Forest.HalfExtent = FVector2D(400.f, 400.f);
	const FGFOverworldLayout Synthetic = FGFOverworldBuild::Make(Map.Curve, { Far }, { Forest }, 0.f);
	const FIntVector ForestCell = Synthetic.SurfaceCell(FVector::ZeroVector);
	TestEqual(TEXT("A forest box outside the discs is leaves"), Synthetic.BlockAt(ForestCell), GFOverworldBlock::Leaves());
	TestEqual(TEXT("Forest fill is dirt"), Synthetic.BlockAt(FIntVector(ForestCell.X, ForestCell.Y, ForestCell.Z - 1)), GFOverworldBlock::Dirt());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFOverworldRadiusTest, "GFGame.Overworld.BuildableRadius", GFOverworldBuildTests::Flags)
bool FGFOverworldRadiusTest::RunTest(const FString&)
{
	using namespace GFOverworldBuildTests;

	FGFOverworldLayout Layout;
	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped overworld layout"), Load(Layout, Map)))
	{
		return false;
	}

	const FVector RimeCentre(0.f, 0.f, 0.f);
	const FVector Step(1500.f, 0.f, 0.f);
	const FVector Edge(1600.f, 0.f, 0.f);
	const FVector Past(1601.f, 0.f, 0.f);
	const FVector Outer(2500.f, 0.f, 0.f);
	const FVector Shared(-2200.f, 0.f, 0.f);
	const FVector CinderCentre(7200.f, 0.f, 0.f);
	const FVector CinderPast(5700.f, 0.f, 0.f);
	const FVector WestFerry(1200.f, 600.f, 0.f);
	const FVector EastFerry(6000.f, 600.f, 0.f);

	TestTrue(TEXT("Counts can be set"), SetCounts(Map, 0, 0, 0));
	TestEqual(TEXT("No members, Rime centre is closed"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), RimeCentre), EGFOverworldEdit::Deny);

	TestTrue(TEXT("One Rime member"), SetCounts(Map, 1, 0, 0));
	TestEqual(TEXT("1500 is inside the first radius"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Step), EGFOverworldEdit::Own);
	TestEqual(TEXT("1600 is inside the first radius"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Edge), EGFOverworldEdit::Own);
	TestEqual(TEXT("1601 is outside the first radius"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Past), EGFOverworldEdit::Deny);
	TestEqual(TEXT("2500 is outside at one member"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Outer), EGFOverworldEdit::Deny);
	TestEqual(TEXT("West ferry is Rime land at one member"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), WestFerry), EGFOverworldEdit::Own);

	TestTrue(TEXT("Two Rime members"), SetCounts(Map, 2, 0, 0));
	TestEqual(TEXT("2500 is outside at two members"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Outer), EGFOverworldEdit::Deny);
	TestTrue(TEXT("Three Rime members"), SetCounts(Map, 3, 0, 0));
	TestEqual(TEXT("2500 is outside at three members"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Outer), EGFOverworldEdit::Deny);
	TestTrue(TEXT("Four Rime members"), SetCounts(Map, 4, 0, 0));
	TestEqual(TEXT("2500 is inside at four members"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Outer), EGFOverworldEdit::Own);

	TestTrue(TEXT("Four members each"), SetCounts(Map, 4, 4, 4));
	TestEqual(TEXT("Overlap is shared for Rime"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Shared), EGFOverworldEdit::Shared);
	TestEqual(TEXT("Overlap is shared for Verdant"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Verdant(), Shared), EGFOverworldEdit::Shared);
	TestFalse(TEXT("Territory gives the tie to one owner"), Map.IsSafeFor(GFOverworldFaction::Verdant(), Shared));
	TestTrue(TEXT("Three members drop the overlap"), SetCounts(Map, 3, 3, 0));
	TestEqual(TEXT("Overlap is wild for Rime at three"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), Shared), EGFOverworldEdit::Deny);
	TestEqual(TEXT("Overlap is wild for Verdant at three"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Verdant(), Shared), EGFOverworldEdit::Deny);

	TestTrue(TEXT("One Cinderwrights member"), SetCounts(Map, 0, 0, 1));
	TestEqual(TEXT("Cinderwrights centre is theirs"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Cinderwrights(), CinderCentre), EGFOverworldEdit::Own);
	TestEqual(TEXT("5700 is outside the first Cinderwrights radius"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Cinderwrights(), CinderPast), EGFOverworldEdit::Deny);
	TestEqual(TEXT("A Rime member on Cinderwrights land is a visitor"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Rime(), CinderCentre), EGFOverworldEdit::Foreign);
	TestEqual(TEXT("East ferry is Cinderwrights land at one member"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Cinderwrights(), EastFerry), EGFOverworldEdit::Own);
	TestTrue(TEXT("Two Cinderwrights members"), SetCounts(Map, 0, 0, 2));
	TestEqual(TEXT("5700 is inside the second Cinderwrights radius"), FGFOverworldBuild::Classify(Map, GFOverworldFaction::Cinderwrights(), CinderPast), EGFOverworldEdit::Own);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFOverworldDrawTest, "GFGame.Overworld.DrawPolicy", GFOverworldBuildTests::Flags)
bool FGFOverworldDrawTest::RunTest(const FString&)
{
	using namespace GFOverworldBuildTests;

	TestFalse(TEXT("No world is not the hub"), AGFOverworldGround::IsVoxelOverworldMap(nullptr));
	TestFalse(TEXT("Hub draws no slab"), AGFOverworldGround::ShouldDrawShape(EGFOverworldShapeKind::Slab, true));
	TestFalse(TEXT("Hub draws no disc"), AGFOverworldGround::ShouldDrawShape(EGFOverworldShapeKind::FactionDisc, true));
	TestFalse(TEXT("Hub draws no hazard box"), AGFOverworldGround::ShouldDrawShape(EGFOverworldShapeKind::HazardBox, true));
	TestTrue(TEXT("Other maps draw the slab"), AGFOverworldGround::ShouldDrawShape(EGFOverworldShapeKind::Slab, false));
	TestTrue(TEXT("Other maps draw discs"), AGFOverworldGround::ShouldDrawShape(EGFOverworldShapeKind::FactionDisc, false));
	TestFalse(TEXT("Other maps skip hazard boxes"), AGFOverworldGround::ShouldDrawShape(EGFOverworldShapeKind::HazardBox, false));

	if (GEngine)
	{
		const uint32 Salt = FPlatformTime::Cycles();
		UPackage* HubPackage = CreatePackage(*FString::Printf(TEXT("/Temp/GFOverworldProbeL_Hub_%u"), Salt));
		UWorld* HubWorld = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ProbeHub"), HubPackage, false);
		UPackage* OtherPackage = CreatePackage(*FString::Printf(TEXT("/Temp/GFOverworldProbePlain_%u"), Salt));
		UWorld* OtherWorld = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ProbePlain"), OtherPackage, false);
		if (TestNotNull(TEXT("Hub probe world"), HubWorld))
		{
			TestTrue(TEXT("A L_Hub package is the voxel map"), AGFOverworldGround::IsVoxelOverworldMap(HubWorld));
		}
		if (TestNotNull(TEXT("Other probe world"), OtherWorld))
		{
			TestFalse(TEXT("A plain package is not the voxel map"), AGFOverworldGround::IsVoxelOverworldMap(OtherWorld));
		}
		DestroyWorld(HubWorld);
		DestroyWorld(OtherWorld);
	}

	FGFOverworldParams Params;
	Params.Margin = 10.f;
	Params.Thickness = 10.f;
	Params.SurfaceZ = -2.f;
	Params.OverlayLift = 1.f;
	FGFTerritoryFaction Cinder;
	Cinder.Id = GFOverworldFaction::Cinderwrights();
	Cinder.Center = FVector::ZeroVector;
	Cinder.RadiusByMembers = { 100.f };
	const TArray<FGFOverworldShape> Shapes = AGFOverworldGround::BuildShapes({ Cinder }, {}, Params);
	const FGFOverworldShape* Disc = nullptr;
	for (const FGFOverworldShape& Shape : Shapes)
	{
		if (Shape.Kind == EGFOverworldShapeKind::FactionDisc)
		{
			Disc = &Shape;
		}
	}
	if (TestNotNull(TEXT("Cinderwrights disc"), Disc))
	{
		TestTrue(TEXT("Fallback disc is ember #D9542B"), Disc->Color.Equals(Ember));
		TestFalse(TEXT("Fallback disc is not grey"), Disc->Color.Equals(OldCinderGrey));
	}

	const AGFOverworldGround* Ground = GetDefault<AGFOverworldGround>();
	if (!TestNotNull(TEXT("Ground defaults"), Ground))
	{
		return false;
	}
	bool bEmber = false;
	bool bGrey = false;
	for (const FGFOverworldTint& Tint : Ground->FactionColours)
	{
		if (Tint.Id == GFOverworldFaction::Cinderwrights())
		{
			bEmber = Tint.Color.Equals(Ember);
			bGrey = Tint.Color.Equals(OldCinderGrey);
		}
	}
	TestTrue(TEXT("Configured Cinderwrights colour is ember #D9542B"), bEmber);
	TestFalse(TEXT("Configured Cinderwrights colour is not grey"), bGrey);
	TestTrue(TEXT("Helper matches #D9542B"), GFOverworldCinderColour().Equals(Ember));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFOverworldBlocksTest, "GFGame.Overworld.Blocks", GFOverworldBuildTests::Flags)
bool FGFOverworldBlocksTest::RunTest(const FString&)
{
	FString Csv;
	if (!TestTrue(TEXT("DT_Blocks.csv loads"), FFileHelper::LoadFileToString(Csv, *FGFDataTables::CsvPath(TEXT("DT_Blocks")))))
	{
		return false;
	}

	const int32 GrassAt = Csv.Find(TEXT("\nGrass,"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
	const int32 GrassEnd = GrassAt == INDEX_NONE ? INDEX_NONE : Csv.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, GrassAt + 1);
	if (!TestTrue(TEXT("Grass row"), GrassAt != INDEX_NONE && GrassEnd != INDEX_NONE))
	{
		return false;
	}
	const FString GrassLine = Csv.Mid(GrassAt, GrassEnd - GrassAt);
	TestTrue(TEXT("Grass uses the art mesh"), GrassLine.Contains(TEXT("/Game/Art/Blocks/SM_Block_Grass.SM_Block_Grass")));
	TestFalse(TEXT("Grass mesh is not None"), GrassLine.Contains(TEXT("None,None")));

	TestTrue(TEXT("Ice row"), Csv.Contains(TEXT("\nIce,")));
	TestTrue(TEXT("Lava is unplaceable and emissive"), Csv.Contains(TEXT("False,False,True,Stone")));

	auto ExpectMesh = [this](const TCHAR* Path, const TCHAR* Label)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		TestNotNull(Label, Mesh);
	};
	ExpectMesh(TEXT("/Game/Art/Blocks/SM_Block_Grass.SM_Block_Grass"), TEXT("Grass mesh"));
	ExpectMesh(TEXT("/Game/Art/Blocks/SM_Block_Cobblestone.SM_Block_Cobblestone"), TEXT("Cobblestone mesh"));
	ExpectMesh(TEXT("/Game/Art/Blocks/SM_Block_Farmland.SM_Block_Farmland"), TEXT("Farmland mesh"));
	ExpectMesh(TEXT("/Game/Art/Blocks/SM_Block_Log.SM_Block_Log"), TEXT("Log mesh"));
	ExpectMesh(TEXT("/Game/Art/Blocks/SM_Block_Leaves.SM_Block_Leaves"), TEXT("Leaves mesh"));
	return true;
}

#endif
