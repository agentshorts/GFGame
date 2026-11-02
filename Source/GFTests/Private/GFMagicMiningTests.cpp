// Magic mining, first slice. Sparkfield, the faction elements, Clawcall, the magnet, and the tier-1 trees.
// Pure cases share the rules with the server. The world cases prove the voxels, the bag, and the summon.
#include "Misc/AutomationTest.h"
#include "GFMagicRules.h"
#include "GFMagicShapes.h"
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
	Grid.Blocks.Add(FIntVector(0, 0, 0), TEXT("OreMetal"));
	TMap<FIntVector, FMagicWound> Wounds;
	// Hardness 2.5 is 250 integrity. Magma is 2x, so two seconds of the field clears it.
	const FMagicHitOutput Smelt = FGFMagicRules::Apply(Wounds, Hit({ FIntVector(0, 0, 0) }, EGFMagicElement::Magma, 2.f), Grid.View());
	if (TestTrue(TEXT("Magma breaks metal ore in one long tick"), Broke(Smelt, FIntVector(0, 0, 0))))
	{
		TestEqual(TEXT("The drop is metal, not stone"), Smelt.Breaks[0].DropItem, FName(TEXT("Metal")));
		TestEqual(TEXT("One drop"), Smelt.Breaks[0].DropCount, 1);
	}
	FGrid Plain;
	Plain.Blocks.Add(FIntVector(1, 0, 0), TEXT("Stone"));
	TMap<FIntVector, FMagicWound> PlainWounds;
	const FMagicHitOutput StoneHit = FGFMagicRules::Apply(PlainWounds, Hit({ FIntVector(1, 0, 0) }, EGFMagicElement::Magma, 1.f), Plain.View());
	if (TestTrue(TEXT("Magma still breaks plain stone"), Broke(StoneHit, FIntVector(1, 0, 0))))
	{
		TestEqual(TEXT("Plain stone stays stone"), StoneHit.Breaks[0].DropItem, FName(TEXT("Stone")));
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
	TestEqual(TEXT("Verdant's pick is the wrong tree"), FGFMagicRules::TryBuy(State, TEXT("BarkAxe")).Code, FName(TEXT("WrongTree")));
	TestEqual(TEXT("A missing id stays unknown"), FGFMagicRules::TryBuy(State, TEXT("NotASpell")).Code, FName(TEXT("Unknown")));
	FMagicUpgradeState Broke = State;
	Broke.Bag.Reset();
	TestEqual(TEXT("A page and a crystal, or no sale"), FGFMagicRules::TryBuy(Broke, TEXT("SmelterPlus")).Code, FName(TEXT("CantAfford")));
	const FMagicUpgradeDecision Bought = FGFMagicRules::TryBuy(State, TEXT("SmelterPlus"));
	TestEqual(TEXT("Smelter Plus is the tier pick"), Bought.Code, FName(TEXT("Ok")));
	TestTrue(TEXT("The page was spent"), !Bought.Bag.Contains(TEXT("SpellPage")));
	TestTrue(TEXT("The crystal was spent"), !Bought.Bag.Contains(TEXT("Crystal")));
	const FGFMagicModifiers Mods = FGFMagicRules::ModifiersFor(Bought.Owned);
	TestEqual(TEXT("Smelter Plus adds a 30 percent smelt"), Mods.SmeltChance, 0.3f);
	FMagicUpgradeState Again = State;
	Again.Owned = Bought.Owned;
	Again.Bag.Add(TEXT("SpellPage"), 1);
	Again.Bag.Add(TEXT("Crystal"), 1);
	TestEqual(TEXT("The other tier-1 pick is closed"), FGFMagicRules::TryBuy(Again, TEXT("StickyMagma")).Code, FName(TEXT("TierPicked")));
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
		if (!Row)
		{
			AddError(FString::Printf(TEXT("%s row is missing"), *Id.ToString()));
			continue;
		}
		if (Id == TEXT("Shatterfrost"))
		{
			TestTrue(TEXT("Shatterfrost is played"), Row->bImplemented);
		}
		else
		{
			TestFalse(FString::Printf(TEXT("%s stays data only"), *Id.ToString()), Row->bImplemented);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicSparkconeTest, "GFGame.MagicMining.Sparkcone", GFMagicTestFlags)
bool FGFMagicSparkconeTest::RunTest(const FString&)
{
	using namespace GFMagicTest;
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::Spell(TEXT("Sparkcone"));
	if (!TestNotNull(TEXT("Sparkcone row"), Spell))
	{
		return false;
	}
	TestEqual(TEXT("The formula yields 29 rays, not a hand count"), FGFMagicShapes::ConeRayCount(Spell->RangeCm, Spell->ConeHalfAngle, GFBlockSize, Spell->MinRays, Spell->MaxRays), 29);
	TestEqual(TEXT("Full power inside 3 m"), FGFMagicShapes::SparkFalloff(0.f, Spell->FalloffStartCm, Spell->RangeCm, Spell->FalloffMin), 1.f);
	TestEqual(TEXT("Full power at the falloff start"), FGFMagicShapes::SparkFalloff(Spell->FalloffStartCm, Spell->FalloffStartCm, Spell->RangeCm, Spell->FalloffMin), 1.f);
	TestTrue(TEXT("35 percent at 8 m"), FMath::IsNearlyEqual(FGFMagicShapes::SparkFalloff(Spell->RangeCm, Spell->FalloffStartCm, Spell->RangeCm, Spell->FalloffMin), 0.35f));
	TestEqual(TEXT("The first break does not scale the chain"), FGFMagicRules::ChainScale(1, 20, 0.5f), 1.f);
	TestTrue(TEXT("Twenty breaks reach the bonus"), FMath::IsNearlyEqual(FGFMagicRules::ChainScale(20, 20, 0.5f), 1.5f));

	const FVector Origin(50.f, 50.f, 150.f);
	TSet<FIntVector> SolidCells;
	SolidCells.Add(FIntVector(3, 0, 1));
	SolidCells.Add(FIntVector(4, 0, 1));
	SolidCells.Add(FIntVector(5, 0, 1));
	SolidCells.Add(FIntVector(3, 2, 1));
	SolidCells.Add(FIntVector(0, 0, 1));
	auto Solid = [&SolidCells](const FIntVector& Cell) { return SolidCells.Contains(Cell); };
	auto Blocker = [](const FIntVector&) { return false; };
	const TArray<FMagicConeHit> Hits = FGFMagicShapes::GatherCone(Origin, FVector::ForwardVector, Spell->RangeCm, Spell->ConeHalfAngle, Spell->Penetration, Spell->CellCap, FVector::ZeroVector, Solid, Blocker);
	auto Has = [&Hits](const FIntVector& Cell)
	{
		return Hits.ContainsByPredicate([&Cell](const FMagicConeHit& Hit) { return Hit.Cell == Cell; });
	};
	TestTrue(TEXT("The cone hits the cell on axis"), Has(FIntVector(3, 0, 1)));
	TestTrue(TEXT("Penetration keeps one cell behind it"), Has(FIntVector(4, 0, 1)));
	TestFalse(TEXT("A second cell behind is dark"), Has(FIntVector(5, 0, 1)));
	TestFalse(TEXT("A cell outside the half-angle is dark"), Has(FIntVector(3, 2, 1)));
	const TArray<FMagicConeHit> Point = FGFMagicShapes::GatherCone(Origin, FVector::ZeroVector, Spell->RangeCm, Spell->ConeHalfAngle, Spell->Penetration, Spell->CellCap, FVector::ZeroVector, Solid, Blocker);
	TestEqual(TEXT("A zero aim is the one solid under the crystal"), Point.Num(), 1);
	TestTrue(TEXT("That cell is the origin cell"), Point.Num() == 1 && Point[0].Cell == FIntVector(0, 0, 1));

	FGrid Grid;
	for (int32 Step = 0; Step < 6; ++Step)
	{
		Grid.Blocks.Add(FIntVector(Step, 0, 0), TEXT("OreMetal"));
	}
	TMap<FIntVector, FMagicWound> Wounds;
	FMagicHitRequest Request = Hit({ FIntVector(0, 0, 0) }, EGFMagicElement::Spark, 0.1f);
	Request.ConductiveCap = 3;
	const FMagicHitOutput Chain = FGFMagicRules::Apply(Wounds, Request, Grid.View());
	TestEqual(TEXT("Conductive cap walks three extra cells"), Chain.Spent, 4);
	TestFalse(TEXT("The fifth neighbour waits for another cast"), Wounds.Contains(FIntVector(4, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicMagmaSprayTest, "GFGame.MagicMining.MagmaSpray", GFMagicTestFlags)
bool FGFMagicMagmaSprayTest::RunTest(const FString&)
{
	using namespace GFMagicTest;
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::Spell(TEXT("MagmaSpray"));
	if (!TestNotNull(TEXT("Magma spray row"), Spell))
	{
		return false;
	}
	FGrid Grid;
	Grid.Blocks.Add(FIntVector(0, 0, 2), TEXT("Stone"));
	Grid.Blocks.Add(FIntVector(0, 0, 1), TEXT("Stone"));
	Grid.Blocks.Add(FIntVector(0, 0, 0), TEXT("Stone"));
	TArray<FMagicPuddle> Puddles;
	TMap<FIntVector, FMagicWound> Wounds;
	FGFMagicShapes::FeedPuddle(Puddles, FIntVector(0, 0, 2), 2.5f, { TEXT("Digger") }, FIntVector(0, 0, -1), 1.f, 0.0, *Spell, Grid.View());
	TestEqual(TEXT("One puddle sits on the top cell"), Puddles.Num(), 1);
	const FMagicHitOutput First = FGFMagicShapes::TickPuddles(Puddles, Wounds, 0.0, 1.f, *Spell, Grid.View(), 8);
	TestTrue(TEXT("The top cell dissolves"), Broke(First, FIntVector(0, 0, 2)));
	TestFalse(TEXT("The dissolve is not a lava write"), First.Breaks.Num() > 0 && !First.Breaks[0].TransformTo.IsNone());
	TestEqual(TEXT("The puddle sinks"), Puddles.Num(), 1);
	TestEqual(TEXT("It sinks onto the cell below"), Puddles.Num() == 1 ? Puddles[0].Cell : FIntVector::ZeroValue, FIntVector(0, 0, 1));
	const FMagicHitOutput Second = FGFMagicShapes::TickPuddles(Puddles, Wounds, 0.1, 1.f, *Spell, Grid.View(), 8);
	TestTrue(TEXT("The next cell down dissolves"), Broke(Second, FIntVector(0, 0, 1)));
	TestFalse(TEXT("The floor under a spent puddle stays"), Broke(Second, FIntVector(0, 0, 0)));

	TArray<FMagicSmear> Smears;
	FMagicSmear Smear;
	Smear.Cell = FIntVector(0, 0, 0);
	Smear.Until = 2.0;
	Smear.PerSecond = 0.5f * Spell->PuddleDissolve;
	Smear.Owners = { TEXT("Digger") };
	Smear.Rate = 1.f;
	Smears.Add(Smear);
	TMap<FIntVector, FMagicWound> SmearWounds;
	FGFMagicShapes::TickSmears(Smears, SmearWounds, 0.0, 1.f, Grid.View(), 4);
	const FMagicWound* Wound = SmearWounds.Find(FIntVector(0, 0, 0));
	TestTrue(TEXT("A wall smear wounds the cell"), Wound && Wound->Remaining > 0.f && Wound->Remaining < FGFMagicRules::IntegrityOf(1.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicRootmaulTest, "GFGame.MagicMining.Rootmaul", GFMagicTestFlags)
bool FGFMagicRootmaulTest::RunTest(const FString&)
{
	using namespace GFMagicTest;
	const FGFRootmaulMoveRow* Swing = FGFMagicShapes::Move(TEXT("Rootmaul"), TEXT("Swing1"));
	const FGFRootmaulMoveRow* Axe = FGFMagicShapes::Move(TEXT("BarkAxe"), TEXT("Swing1"));
	if (!TestNotNull(TEXT("Swing1"), Swing) || !TestNotNull(TEXT("Bark axe swing"), Axe))
	{
		return false;
	}
	TestEqual(TEXT("A light swing is 3 by 2 by 1"), Swing->Width, 3);
	TestEqual(TEXT("Swing height"), Swing->Height, 2);
	TestEqual(TEXT("Swing depth"), Swing->Depth, 1);
	TestEqual(TEXT("Swing power"), Swing->Power, 120.f);
	TestEqual(TEXT("Bark axe is a narrower chop"), Axe->Width, 1);
	TestEqual(TEXT("Bark axe reaches two deep"), Axe->Depth, 2);
	TestEqual(TEXT("Idle light starts at Swing1"), FGFMagicShapes::NextMove(TEXT("Rootmaul"), NAME_None, false), FName(TEXT("Swing1")));
	TestEqual(TEXT("The second light is Swing2"), FGFMagicShapes::NextMove(TEXT("Rootmaul"), TEXT("Swing1"), false), FName(TEXT("Swing2")));
	TestEqual(TEXT("Idle heavy is the overhead"), FGFMagicShapes::NextMove(TEXT("Rootmaul"), NAME_None, true), FName(TEXT("HeavyOverhead")));
	TestTrue(TEXT("A notify inside the slack lands"), FGFMagicShapes::AcceptMeleeTiming(0.0, 0.40, 0.50, 0.15));
	TestFalse(TEXT("A late notify is dropped"), FGFMagicShapes::AcceptMeleeTiming(0.0, 0.40, 1.0, 0.15));

	const TArray<FIntVector> Cells = FGFMagicShapes::MeleeCells(FIntVector::ZeroValue, FVector::ForwardVector, Swing->Width, Swing->Height, Swing->Depth);
	TestEqual(TEXT("Six cells in the swing"), Cells.Num(), 6);
	TestTrue(TEXT("The swing starts one cell forward"), Cells.Contains(FIntVector(1, 0, 0)));
	TestFalse(TEXT("It does not reach two deep"), Cells.Contains(FIntVector(2, 0, 0)));
	TestFalse(TEXT("Height grows up, not down"), Cells.Contains(FIntVector(1, 0, -1)));

	FMagicRhythm Rhythm;
	Rhythm.Window = 0.15f;
	Rhythm.Step = 0.25f;
	Rhythm.MaxBonus = 0.75f;
	TestEqual(TEXT("The first note is free"), Rhythm.Note(0.0, true), 1.f);
	TestTrue(TEXT("The second note inside the window stacks"), FMath::IsNearlyEqual(Rhythm.Note(0.1, true), 1.25f));

	FGrid Grid;
	Grid.Blocks.Add(FIntVector(1, 0, 0), TEXT("Dirt"));
	Grid.Blocks.Add(FIntVector(0, 0, 0), TEXT("Dirt"));
	TMap<FIntVector, FMagicWound> Wounds;
	FMagicHitRequest Request = Hit({ FIntVector(1, 0, 0) }, EGFMagicElement::Root, 0.1f);
	Request.bImpulse = true;
	Request.ImpulsePower = Swing->Power;
	const FMagicHitOutput Out = FGFMagicRules::Apply(Wounds, Request, Grid.View());
	TestTrue(TEXT("One swing breaks the dirt in front"), Broke(Out, FIntVector(1, 0, 0)));
	TestFalse(TEXT("The cell under the feet was not in the swing"), Broke(Out, FIntVector(0, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicFrostbindTest, "GFGame.MagicMining.Frostbind", GFMagicTestFlags)
bool FGFMagicFrostbindTest::RunTest(const FString&)
{
	using namespace GFMagicTest;
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::Spell(TEXT("Frostbind"));
	if (!TestNotNull(TEXT("Frostbind row"), Spell))
	{
		return false;
	}
	FGrid Grid;
	for (int32 Step = 0; Step < 7; ++Step)
	{
		Grid.Blocks.Add(FIntVector(Step, 0, 0), TEXT("Ice"));
	}
	Grid.Blocks.Add(FIntVector(0, 2, 0), TEXT("Lava"));
	Grid.Blocks.Add(FIntVector(0, 4, 0), TEXT("Lava"));
	TMap<FIntVector, FMagicFreezeCell> Cells;
	TMap<int32, int32> Chains;
	int32 NextId = 1;
	TArray<FString> Who = { TEXT("Digger") };
	const FMagicHitOutput Quench = FGFMagicShapes::TickFreeze(Cells, Chains, { FIntVector(0, 0, 0) }, {}, FIntVector(0, 0, 0), Who, 0.0, 0.1f, 1.f, Spell->QuenchRadius, 0, *Spell, Grid.View(), NextId);
	TestTrue(TEXT("Lava inside the quench radius becomes basalt"), Broke(Quench, FIntVector(0, 2, 0)));
	TestFalse(TEXT("Lava outside the radius stays"), Broke(Quench, FIntVector(0, 4, 0)));
	if (const FMagicBreak* Basalt = Quench.Breaks.FindByPredicate([](const FMagicBreak& Break) { return Break.Cell == FIntVector(0, 2, 0); }))
	{
		TestEqual(TEXT("The quench writes basalt"), Basalt->TransformTo, FName(TEXT("Basalt")));
		TestEqual(TEXT("The quench pays nothing"), Basalt->DropCount, 0);
	}

	Cells.Reset();
	Chains.Reset();
	FGrid Line;
	for (int32 Step = 0; Step < 6; ++Step)
	{
		Line.Blocks.Add(FIntVector(Step, 0, 0), TEXT("Ice"));
	}
	Line.Blocks.Add(FIntVector(1, 0, 0), TEXT("Stone"));
	FGFMagicShapes::TickFreeze(Cells, Chains, { FIntVector(0, 0, 0) }, {}, FIntVector(0, 0, 0), Who, 0.0, 1.f, 1.f, 0, 0, *Spell, Line.View(), NextId);
	TestTrue(TEXT("The aimed ice locks"), Cells.Contains(FIntVector(0, 0, 0)) && Cells[FIntVector(0, 0, 0)].bLocked);
	const FMagicHitOutput Burst = FGFMagicShapes::TickFreeze(Cells, Chains, {}, { FIntVector(0, 0, 0) }, FIntVector(0, 0, 0), Who, 0.0, 0.1f, 1.f, 0, 0, *Spell, Line.View(), NextId);
	TestTrue(TEXT("Shatterfrost pops the locked cell now"), Broke(Burst, FIntVector(0, 0, 0)));
	TestTrue(TEXT("Full spread locks the stone neighbour"), Cells.Contains(FIntVector(1, 0, 0)) && Cells[FIntVector(1, 0, 0)].bLocked);

	Cells.Reset();
	Chains.Reset();
	FGrid ChainGrid;
	for (int32 Step = 0; Step < 6; ++Step)
	{
		ChainGrid.Blocks.Add(FIntVector(Step, 0, 0), TEXT("Ice"));
	}
	double Now = 0.0;
	FGFMagicShapes::TickFreeze(Cells, Chains, { FIntVector(0, 0, 0) }, {}, FIntVector(0, 0, 0), Who, Now, 1.f, 1.f, -1, 0, *Spell, ChainGrid.View(), NextId);
	TArray<FIntVector> Broken;
	for (int32 Gen = 0; Gen < 6; ++Gen)
	{
		Now += Spell->ContractionSeconds;
		const FMagicHitOutput Pop = FGFMagicShapes::TickFreeze(Cells, Chains, {}, {}, FIntVector(0, 0, 0), Who, Now, 0.1f, 1.f, -1, 0, *Spell, ChainGrid.View(), NextId);
		for (const FMagicBreak& Break : Pop.Breaks)
		{
			Broken.Add(Break.Cell);
		}
	}
	TestTrue(TEXT("The chain shatters the aimed cell"), Broken.Contains(FIntVector(0, 0, 0)));
	TestTrue(TEXT("The chain reaches four cells out"), Broken.Contains(FIntVector(4, 0, 0)));
	TestFalse(TEXT("The fifth cell past the generation cap stays"), Broken.Contains(FIntVector(5, 0, 0)));
	return true;
}

#endif
