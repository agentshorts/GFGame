// Big Grumble's tame spends unique items from any three of the five classes, then the tamestone.
#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "GFItemTypes.h"
#include "../GFUnbinding.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags EndgamePathFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	const UDataTable* EndgameItems()
	{
		return FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEndgamePathsTest, "GFGame.Endgame.Paths", EndgamePathFlags)
bool FGFEndgamePathsTest::RunTest(const FString&)
{
	const UDataTable* Items = EndgameItems();
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}

	FGFUnbinding Gathering;
	Gathering.Bags.FindOrAdd(TEXT("A")).Add(TEXT("Crystal"), 2);
	Gathering.Bags.FindOrAdd(TEXT("B")).Add(TEXT("Scales"), 2);
	Gathering.Bags.FindOrAdd(TEXT("C")).Add(TEXT("FifthFind"), 2);
	Gathering.Bags.FindOrAdd(TEXT("A")).Add(GFUnbindingIds::MaterialId(), 1);
	Gathering.Bags.FindOrAdd(TEXT("A")).Add(TEXT("Wood"), 6);

	FGFUnbindingResult Early = Gathering.AttemptEndgame(TEXT("A"), Items);
	TestEqual(TEXT("Not spawned yet"), Early.Code, FGFUnbindingCodes::NotSpawned);
	TestEqual(TEXT("Crystal still there"), Gathering.Bags[TEXT("A")].GetCount(TEXT("Crystal")), 2);
	TestEqual(TEXT("Tamestone still there"), Gathering.MaterialOf(TEXT("A")), 1);

	TestTrue(TEXT("He arrives"), Gathering.NotifySpawned(GFUnbindingIds::GiantDisplayName()));
	FGFUnbinding Short = Gathering;
	Short.Bags[TEXT("A")].Remove(GFUnbindingIds::MaterialId(), 1);
	TestEqual(TEXT("No tamestone"), Short.AttemptEndgame(TEXT("A"), Items).Code, FGFUnbindingCodes::NoMaterial);
	TestEqual(TEXT("Path items stay when the tamestone is missing"), Short.Bags[TEXT("A")].GetCount(TEXT("Crystal")), 2);

	FGFUnbinding Two = Gathering;
	Two.Bags.Remove(TEXT("C"));
	TestEqual(TEXT("Two paths is not the endgame"), Two.AttemptEndgame(TEXT("A"), Items).Code, FGFUnbindingCodes::NeedPaths);
	TestEqual(TEXT("Tamestone kept"), Two.MaterialOf(TEXT("A")), 1);
	TestEqual(TEXT("Scales kept"), Two.Bags[TEXT("B")].GetCount(TEXT("Scales")), 2);

	const FGFUnbindingResult Done = Gathering.AttemptEndgame(TEXT("A"), Items);
	TestTrue(TEXT("Three bags, three classes"), Done.bOk && Done.bConsumed);
	TestEqual(TEXT("Tamer"), Done.RecipientKey, FString(TEXT("A")));
	TestEqual(TEXT("One crystal left"), Gathering.Bags[TEXT("A")].GetCount(TEXT("Crystal")), 1);
	TestEqual(TEXT("Tamestone spent"), Gathering.MaterialOf(TEXT("A")), 0);
	TestEqual(TEXT("Wood untouched"), Gathering.Bags[TEXT("A")].GetCount(TEXT("Wood")), 6);
	TestEqual(TEXT("Scales left one"), Gathering.Bags[TEXT("B")].GetCount(TEXT("Scales")), 1);
	TestEqual(TEXT("Fifth find left one"), Gathering.Bags[TEXT("C")].GetCount(TEXT("FifthFind")), 1);
	TestTrue(TEXT("A tamed him"), Gathering.HasTamed(TEXT("A")));

	FGFUnbinding Again;
	Again.NotifySpawned(GFUnbindingIds::GiantDisplayName());
	Again.Bags.FindOrAdd(TEXT("Solo")).Add(GFUnbindingIds::MaterialId(), 1);
	const FGFUnbindingResult StoneOnly = Again.AttemptTame(TEXT("Solo"));
	TestTrue(TEXT("AttemptTame still spends only the tamestone"), StoneOnly.bOk && StoneOnly.bConsumed);
	TestEqual(TEXT("No path items were required for the old tame"), Again.MaterialOf(TEXT("Solo")), 0);

	FGFUnbinding NamedLater;
	NamedLater.NotifySpawned(GFUnbindingIds::GiantDisplayName());
	NamedLater.Bags.FindOrAdd(TEXT("D")).Add(TEXT("FourthFind"), 1);
	NamedLater.Bags.FindOrAdd(TEXT("E")).Add(TEXT("FifthFind"), 1);
	NamedLater.Bags.FindOrAdd(TEXT("F")).Add(TEXT("Fossil"), 1);
	NamedLater.Bags.FindOrAdd(TEXT("F")).Add(GFUnbindingIds::MaterialId(), 1);
	TestTrue(TEXT("The two unnamed classes count"), NamedLater.AttemptEndgame(TEXT("F"), Items).bOk);
	TestEqual(TEXT("Fourth spent"), NamedLater.Bags[TEXT("D")].GetCount(TEXT("FourthFind")), 0);
	TestEqual(TEXT("Fifth spent"), NamedLater.Bags[TEXT("E")].GetCount(TEXT("FifthFind")), 0);
	TestEqual(TEXT("Fossil spent"), NamedLater.Bags[TEXT("F")].GetCount(TEXT("Fossil")), 0);
	return true;
}

#endif
