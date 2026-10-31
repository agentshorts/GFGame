// Magic mining, first slice. Sparkfield, the faction elements, Clawcall, the magnet, and the tier-1 trees.
// Pure cases share the rules with the server. The world cases prove the voxels, the bag, and the summon.
#include "Misc/AutomationTest.h"
#include "GFMagicRules.h"
#include "GFMagicTables.h"
#include "GFMagicTypes.h"
#include "GFDataTables.h"
#include "GFPathRules.h"
#include "GFMineTypes.h"
#include "GFVoxelTypes.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFMagicTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFMagicTest
{
	struct FGrid
	{
		TMap<FIntVector, FName> Blocks;
		TMap<FName, float> Hardness;
		TSet<FIntVector> Placed;

		FMagicWorldView View()
		{
			FMagicWorldView Out;
			Out.BlockAt = [this](const FIntVector& Cell)
			{
				const FName* Found = Blocks.Find(Cell);
				return Found ? *Found : NAME_None;
			};
			Out.IsPlaced = [this](const FIntVector& Cell) { return Placed.Contains(Cell); };
			Out.Hardness = [this](FName Id)
			{
				const float* Found = Hardness.Find(Id);
				return Found ? *Found : FGFMagicCatalog::BlockHardness(Id);
			};
			Out.DropOf = [](FName Id) { return FGFMagicCatalog::BlockDrop(Id); };
			Out.Affinity = [](FName Id) { return FGFMagicCatalog::AffinityOrDefault(Id); };
			return Out;
		}
	};

	static FMagicHitRequest Hit(std::initializer_list<FIntVector> Cells, EGFMagicElement Element, float Dt, bool bFlat = false)
	{
		FMagicHitRequest Request;
		Request.Cells = Cells;
		Request.Element = Element;
		Request.Contributors.Add(TEXT("Digger"));
		Request.Dt = Dt;
		Request.bFlat = bFlat;
		Request.FlatIntegrity = 250.f;
		Request.Budget = GFMagic::CellsPerTick;
		return Request;
	}

	static bool Broke(const FMagicHitOutput& Out, const FIntVector& Cell)
	{
		return Out.Breaks.ContainsByPredicate([&Cell](const FMagicBreak& Break) { return Break.Cell == Cell; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicAffinityTest, "GFGame.MagicMining.Affinity", GFMagicTestFlags)
bool FGFMagicAffinityTest::RunTest(const FString&)
{
	const FGFBlockAffinityRow Stone = FGFMagicCatalog::AffinityOrDefault(TEXT("Stone"));
	TestEqual(TEXT("Spark on stone"), Stone.Spark, 1.f);
	TestEqual(TEXT("Magma on stone"), Stone.Magma, 2.f);
	const FGFBlockAffinityRow Fabric = FGFMagicCatalog::AffinityOrDefault(TEXT("Fabric"));
	TestTrue(TEXT("Fabric is spell-proof"), Fabric.bSpellProof);
	TestEqual(TEXT("Spark cannot touch fabric"), FGFMagicRules::Multiplier(Fabric, EGFMagicElement::Spark), 0.f);
	const FGFBlockAffinityRow Lava = FGFMagicCatalog::AffinityOrDefault(TEXT("Lava"));
	TestEqual(TEXT("Frost is the only lava multiplier"), FGFMagicRules::Multiplier(Lava, EGFMagicElement::Frost), 2.f);
	TestEqual(TEXT("Spark leaves lava alone"), FGFMagicRules::Multiplier(Lava, EGFMagicElement::Spark), 0.f);
	TestEqual(TEXT("Magma leaves lava alone"), FGFMagicRules::Multiplier(Lava, EGFMagicElement::Magma), 0.f);
	TestEqual(TEXT("Root leaves lava alone"), FGFMagicRules::Multiplier(Lava, EGFMagicElement::Root), 0.f);
	TestEqual(TEXT("Frost quenches lava to basalt"), Lava.FrostTransform, FName(TEXT("Basalt")));
	TestEqual(TEXT("Clay bed hardness"), FGFMagicCatalog::BlockHardness(TEXT("ClayBed")), 0.5f);
	TestEqual(TEXT("Clay bed drops clay"), FGFMagicCatalog::BlockDrop(TEXT("ClayBed")), FName(TEXT("Clay")));
	TestEqual(TEXT("Metal ore hardness"), FGFMagicCatalog::BlockHardness(TEXT("OreMetal")), 2.5f);
	TestEqual(TEXT("Metal ore drops ore metal"), FGFMagicCatalog::BlockDrop(TEXT("OreMetal")), FName(TEXT("OreMetal")));
	TestEqual(TEXT("Coalnut seam drops coalnut"), FGFMagicCatalog::BlockDrop(TEXT("CoalnutSeam")), FName(TEXT("Coalnut")));
	TestEqual(TEXT("Magma smelts metal ore"), FGFMagicCatalog::AffinityOrDefault(TEXT("OreMetal")).MagmaDrop, FName(TEXT("Metal")));
	TestEqual(TEXT("Magma smelts the seam"), FGFMagicCatalog::AffinityOrDefault(TEXT("CoalnutSeam")).MagmaDrop, FName(TEXT("Coalnut")));
	const UDataTable* Blocks = FGFDataTables::Get<FGFBlockRow>(TEXT("DT_Blocks"));
	const FGFBlockRow* Vein = Blocks ? Blocks->FindRow<FGFBlockRow>(TEXT("CrystalVein"), TEXT("MagicTest"), false) : nullptr;
	if (TestNotNull(TEXT("Crystal vein row"), Vein))
	{
		TestEqual(TEXT("Crystal vein hardness"), Vein->Hardness, 2.f);
		TestEqual(TEXT("Crystal vein drops crystal"), Vein->DropItem, FName(TEXT("Crystal")));
		TestTrue(TEXT("Crystal vein is emissive"), Vein->bEmissive);
	}
	TestEqual(TEXT("Integrity is hardness times 100"), FGFMagicRules::IntegrityOf(0.5f), 50.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicSparkfieldTest, "GFGame.MagicMining.Sparkfield", GFMagicTestFlags)
bool FGFMagicSparkfieldTest::RunTest(const FString&)
{
	using namespace GFMagicTest;
	const TArray<FIntVector> Sphere = FGFMagicRules::FieldCells(FIntVector(4, 4, 4), 1);
	TestEqual(TEXT("Radius 1 is the centre and six neighbours"), Sphere.Num(), 7);
	TestTrue(TEXT("The centre is in the field"), Sphere.Contains(FIntVector(4, 4, 4)));
	TestTrue(TEXT("A face neighbour is in the field"), Sphere.Contains(FIntVector(5, 4, 4)));
	TestFalse(TEXT("A corner is outside the sphere"), Sphere.Contains(FIntVector(5, 5, 5)));

	const FVector Clamped = FGFMagicRules::ClampRange(FVector::ZeroVector, FVector(2000.f, 0.f, 0.f), 800.f);
	TestEqual(TEXT("The field stops at 8 m"), Clamped.Size(), 800.0);

	FGrid Grid;
	Grid.Blocks.Add(FIntVector(0, 0, 0), TEXT("Dirt"));
	Grid.Blocks.Add(FIntVector(1, 0, 0), TEXT("Fabric"));
	Grid.Blocks.Add(FIntVector(0, 1, 0), TEXT("Stone"));
	Grid.Placed.Add(FIntVector(0, 1, 0));
	TMap<FIntVector, FMagicWound> Wounds;
	FMagicHitOutput Last;
	for (int32 Tick = 0; Tick < 5; ++Tick)
	{
		Last = FGFMagicRules::Apply(Wounds, Hit({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0) }, EGFMagicElement::Spark, 0.1f), Grid.View());
	}
	TestTrue(TEXT("Dirt breaks in half a second"), Broke(Last, FIntVector(0, 0, 0)));
	TestFalse(TEXT("Fabric is untouched"), Broke(Last, FIntVector(1, 0, 0)));
	TestFalse(TEXT("A placed block is untouched"), Broke(Last, FIntVector(0, 1, 0)));
	TestTrue(TEXT("The wound is gone once it breaks"), !Wounds.Contains(FIntVector(0, 0, 0)));

	FGrid Many;
	TArray<FIntVector> Cells;
	for (int32 Index = 0; Index < 70; ++Index)
	{
		const FIntVector Cell(Index, 0, 0);
		Many.Blocks.Add(Cell, TEXT("Stone"));
		Many.Hardness.Add(TEXT("Stone"), 100.f);
		Cells.Add(Cell);
	}
	FMagicHitRequest Wide = Hit({}, EGFMagicElement::Spark, 0.1f);
	Wide.Cells = Cells;
	Wide.Contributors = { TEXT("Digger") };
	TMap<FIntVector, FMagicWound> WideWounds;
	const FMagicHitOutput Capped = FGFMagicRules::Apply(WideWounds, Wide, Many.View());
	TestEqual(TEXT("A tick spends 64 cells"), Capped.Spent, 64);
	TestEqual(TEXT("The rest waits"), Capped.Deferred.Num(), 6);
	TestEqual(TEXT("Nothing broke under that cap"), Capped.Breaks.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicFactionFieldTest, "GFGame.MagicMining.FactionFields", GFMagicTestFlags)
bool FGFMagicFactionFieldTest::RunTest(const FString&)
{
	using namespace GFMagicTest;
	TestEqual(TEXT("Cinderwrights are magma"), FGFMagicRules::FactionElement(TEXT("Cinderwrights")), EGFMagicElement::Magma);
	TestEqual(TEXT("Verdant is root"), FGFMagicRules::FactionElement(TEXT("Verdant")), EGFMagicElement::Root);
	TestEqual(TEXT("Rime is frost"), FGFMagicRules::FactionElement(TEXT("Rime")), EGFMagicElement::Frost);
	TestEqual(TEXT("An unknown faction has no element"), FGFMagicRules::FactionElement(TEXT("Nope")), EGFMagicElement::Spark);
	TestEqual(TEXT("Scythepaw"), FGFMagicRules::ClawForm(TEXT("Cinderwrights")), FName(TEXT("Scythepaw")));
	TestEqual(TEXT("Tusksaw"), FGFMagicRules::ClawForm(TEXT("Verdant")), FName(TEXT("Tusksaw")));
	TestEqual(TEXT("Raptor"), FGFMagicRules::ClawForm(TEXT("Rime")), FName(TEXT("Raptor")));

	FGrid Grid;
	Grid.Blocks.Add(FIntVector(0, 0, 0), TEXT("Stone"));
	TMap<FIntVector, FMagicWound> Wounds;
	const FMagicHitOutput Smelt = FGFMagicRules::Apply(Wounds, Hit({ FIntVector(0, 0, 0) }, EGFMagicElement::Magma, 1.f), Grid.View());
	if (TestTrue(TEXT("Magma breaks stone in one long tick"), Broke(Smelt, FIntVector(0, 0, 0))))
	{
		TestEqual(TEXT("The drop is metal, not stone"), Smelt.Breaks[0].DropItem, FName(TEXT("Metal")));
		TestEqual(TEXT("One drop"), Smelt.Breaks[0].DropCount, 1);
	}

	FGrid Tree;
	Tree.Blocks.Add(FIntVector(0, 0, 0), TEXT("Log"));
	Tree.Blocks.Add(FIntVector(0, 0, 1), TEXT("Leaves"));
	Tree.Blocks.Add(FIntVector(1, 0, 1), TEXT("Leaves"));
	TMap<FIntVector, FMagicWound> TreeWounds;
	const FMagicHitOutput Fell = FGFMagicRules::Apply(TreeWounds, Hit({ FIntVector(0, 0, 0) }, EGFMagicElement::Root, 1.f), Tree.View());
	TestTrue(TEXT("Root breaks the log"), Broke(Fell, FIntVector(0, 0, 0)));
	TestTrue(TEXT("The trunk takes the canopy with it"), Broke(Fell, FIntVector(0, 0, 1)) && Broke(Fell, FIntVector(1, 0, 1)));

	FGrid Cold;
	Cold.Blocks.Add(FIntVector(0, 0, 0), TEXT("Glass"));
	Cold.Blocks.Add(FIntVector(2, 0, 0), TEXT("Lava"));
	TMap<FIntVector, FMagicWound> ColdWounds;
	const FMagicHitOutput Shatter = FGFMagicRules::Apply(ColdWounds, Hit({ FIntVector(0, 0, 0) }, EGFMagicElement::Frost, 1.f), Cold.View());
	TestTrue(TEXT("Frost shatters glass"), Broke(Shatter, FIntVector(0, 0, 0)));
	TMap<FIntVector, FMagicWound> QuenchWounds;
	const FMagicHitOutput Quench = FGFMagicRules::Apply(QuenchWounds, Hit({ FIntVector(2, 0, 0) }, EGFMagicElement::Frost, 1.f), Cold.View());
	if (TestTrue(TEXT("Frost quenches lava"), Broke(Quench, FIntVector(2, 0, 0))))
	{
		TestEqual(TEXT("Lava becomes basalt"), Quench.Breaks[0].TransformTo, FName(TEXT("Basalt")));
		TestEqual(TEXT("A quench does not drop"), Quench.Breaks[0].DropCount, 0);
	}
	TMap<FIntVector, FMagicWound> Denied;
	const FMagicHitOutput Nope = FGFMagicRules::Apply(Denied, Hit({ FIntVector(2, 0, 0) }, EGFMagicElement::Spark, 5.f), Cold.View());
	TestEqual(TEXT("Spark still cannot mine lava"), Nope.Breaks.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicClawcallTest, "GFGame.MagicMining.Clawcall", GFMagicTestFlags)
bool FGFMagicClawcallTest::RunTest(const FString&)
{
	using namespace GFMagicTest;
	const TArray<FIntVector> Tunnel = FGFMagicRules::ClawTunnel(FIntVector::ZeroValue, FVector(1.f, 0.f, 0.f), 6, 3, 2);
	TestEqual(TEXT("A claw is 6 by 3 by 2"), Tunnel.Num(), 36);
	TestTrue(TEXT("The tunnel starts on the aim cell"), Tunnel.Contains(FIntVector(0, 0, 0)));
	TestTrue(TEXT("It runs six cells along +X"), Tunnel.Contains(FIntVector(5, 0, 0)));
	TestFalse(TEXT("It does not run backward"), Tunnel.Contains(FIntVector(-1, 0, 0)));

	FGrid Grid;
	Grid.Blocks.Add(FIntVector(0, 0, 0), TEXT("OreMetal"));
	Grid.Blocks.Add(FIntVector(1, 0, 0), TEXT("Fabric"));
	Grid.Blocks.Add(FIntVector(2, 0, 0), TEXT("Stone"));
	Grid.Placed.Add(FIntVector(2, 0, 0));
	Grid.Blocks.Add(FIntVector(3, 0, 0), TEXT("Lava"));
	Grid.Blocks.Add(FIntVector(4, 0, 0), TEXT("Hard"));
	Grid.Hardness.Add(TEXT("Hard"), 3.f);
	TMap<FIntVector, FMagicWound> Wounds;
	FMagicHitRequest Request = Hit({ FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0), FIntVector(3, 0, 0), FIntVector(4, 0, 0) }, EGFMagicElement::Spark, 0.1f, true);
	const FMagicHitOutput Out = FGFMagicRules::Apply(Wounds, Request, Grid.View());
	TestTrue(TEXT("Flat 250 one-shots metal ore"), Broke(Out, FIntVector(0, 0, 0)));
	TestFalse(TEXT("The claw skips fabric"), Broke(Out, FIntVector(1, 0, 0)));
	TestFalse(TEXT("The claw skips a placed cell"), Broke(Out, FIntVector(2, 0, 0)));
	TestFalse(TEXT("The claw skips lava"), Broke(Out, FIntVector(3, 0, 0)));
	TestFalse(TEXT("Hardness 3 survives one swipe"), Broke(Out, FIntVector(4, 0, 0)));
	const FMagicWound* Left = Wounds.Find(FIntVector(4, 0, 0));
	TestTrue(TEXT("That cell is half a swipe from breaking"), Left && FMath::IsNearlyEqual(Left->Remaining, 50.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicMagnetTest, "GFGame.MagicMining.Magnet", GFMagicTestFlags)
bool FGFMagicMagnetTest::RunTest(const FString&)
{
	using namespace GFMagicTest;
	FGrid Grid;
	Grid.Blocks.Add(FIntVector(1, 1, 1), TEXT("Dirt"));
	TMap<FIntVector, FMagicWound> Wounds;
	FMagicHitRequest Request = Hit({ FIntVector(1, 1, 1) }, EGFMagicElement::Spark, 1.f);
	Request.Contributors = { TEXT("Digger"), TEXT("Partner") };
	const FMagicHitOutput Out = FGFMagicRules::Apply(Wounds, Request, Grid.View());
	if (TestTrue(TEXT("The cell breaks"), Out.Breaks.Num() == 1))
	{
		TestEqual(TEXT("The drop is credited in the same call"), Out.Breaks[0].DropItem, FName(TEXT("Dirt")));
		TestEqual(TEXT("Both diggers are on the break"), Out.Breaks[0].Contributors.Num(), 2);
		TestTrue(TEXT("The first digger gets the full drop"), Out.Breaks[0].Contributors.Contains(TEXT("Digger")));
		TestTrue(TEXT("The partner gets the full drop"), Out.Breaks[0].Contributors.Contains(TEXT("Partner")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicUpgradeTest, "GFGame.MagicMining.Upgrades", GFMagicTestFlags)
bool FGFMagicUpgradeTest::RunTest(const FString&)
{
	TestEqual(TEXT("Pending paths still say the name is pending"), FGFPathRules::DisplayName(EGFPath::UnnamedFourth), FString(TEXT("Name pending")));
	TestEqual(TEXT("The fifth path too"), FGFPathRules::DisplayName(EGFPath::UnnamedFifth), FString(TEXT("Name pending")));
	const FGFSpellUpgradeRow* Focus = FGFMagicCatalog::Upgrade(TEXT("PendingA_SparkFocusReach"));
	TestTrue(TEXT("The pending pick shows as Spark Focus"), Focus && Focus->DisplayName == TEXT("Spark Focus"));
	TestEqual(TEXT("Picks keeps its id"), GFMineUpgrades::Picks, FName(TEXT("Picks")));
	TestEqual(TEXT("The mine screen says Spark Focus"), GFMineUpgrades::Label(GFMineUpgrades::Picks), FString(TEXT("Spark Focus")));
	TestEqual(TEXT("Other mine buys keep their ids"), GFMineUpgrades::Label(GFMineUpgrades::Satchels), GFMineUpgrades::Satchels.ToString());

	FMagicUpgradeState State;
	State.FactionId = TEXT("Cinderwrights");
	State.Path = EGFPath::Delver;
	State.Bag.Add(TEXT("SpellPage"), 1);
	State.Bag.Add(TEXT("Crystal"), 1);
	TestEqual(TEXT("Verdant's pick is the wrong tree"), FGFMagicRules::TryBuy(State, TEXT("RootNetwork")).Code, FName(TEXT("WrongTree")));
	TestEqual(TEXT("A missing id stays unknown"), FGFMagicRules::TryBuy(State, TEXT("NotASpell")).Code, FName(TEXT("Unknown")));
	FMagicUpgradeState Broke = State;
	Broke.Bag.Reset();
	TestEqual(TEXT("A page and a crystal, or no sale"), FGFMagicRules::TryBuy(Broke, TEXT("LavaLance")).Code, FName(TEXT("CantAfford")));
	const FMagicUpgradeDecision Bought = FGFMagicRules::TryBuy(State, TEXT("LavaLance"));
	TestEqual(TEXT("Lava Lance is the tier pick"), Bought.Code, FName(TEXT("Ok")));
	TestTrue(TEXT("The page was spent"), !Bought.Bag.Contains(TEXT("SpellPage")));
	TestTrue(TEXT("The crystal was spent"), !Bought.Bag.Contains(TEXT("Crystal")));
	const FGFMagicModifiers Mods = FGFMagicRules::ModifiersFor(Bought.Owned);
	TestEqual(TEXT("Lava Lance adds 2 m"), Mods.RangeBonusCm, 200.f);
	FMagicUpgradeState Again = State;
	Again.Owned = Bought.Owned;
	Again.Bag.Add(TEXT("SpellPage"), 1);
	Again.Bag.Add(TEXT("Crystal"), 1);
	TestEqual(TEXT("The other tier-1 pick is closed"), FGFMagicRules::TryBuy(Again, TEXT("FissureRake")).Code, FName(TEXT("TierPicked")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicComboTest, "GFGame.MagicMining.Combos", GFMagicTestFlags)
bool FGFMagicComboTest::RunTest(const FString&)
{
	const UDataTable* Table = FGFMagicCatalog::ComboTable();
	if (!TestNotNull(TEXT("Combo table"), Table))
	{
		return true;
	}
	TArray<FName> Rows = Table->GetRowNames();
	TestEqual(TEXT("Six named combos"), Rows.Num(), 6);
	for (const FName& Id : Rows)
	{
		const FGFMiningComboRow* Row = Table->FindRow<FGFMiningComboRow>(Id, TEXT("MagicCombo"), false);
		TestTrue(FString::Printf(TEXT("%s is data only"), *Id.ToString()), Row && !Row->bImplemented);
	}
	return true;
}

#endif
