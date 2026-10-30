// GFGame.Dev.TestKit: the playtest kit names real items, places real blocks, and tops up without stacking.

#include "Misc/AutomationTest.h"
#include "GFTestKit.h"
#include "GFDataTables.h"
#include "GFFerryRules.h"
#include "GFItemTypes.h"
#include "GFVoxelTypes.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDevTestKitTest, "GFGame.Dev.TestKit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGFDevTestKitTest::RunTest(const FString& Parameters)
{
	const FName Pass = FGFTestKit::DefaultPassItem();
	TestFalse(TEXT("Ferry pass item is set"), Pass.IsNone());
	const TArray<FGFItemStack> Kit = FGFTestKit::Contents(Pass);
	TestTrue(TEXT("Kit is not empty"), Kit.Num() > 0);

	UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	UDataTable* Blocks = FGFDataTables::Get<FGFBlockRow>(TEXT("DT_Blocks"));
	if (!TestNotNull(TEXT("DT_Items"), Items) || !TestNotNull(TEXT("DT_Blocks"), Blocks))
	{
		return false;
	}

	int32 Outfits = 0;
	int32 Materials = 0;
	int32 PlaceableBlocks = 0;
	bool bHasPass = false;
	for (const FGFItemStack& S : Kit)
	{
		TestTrue(FString::Printf(TEXT("%s count > 0"), *S.ItemId.ToString()), S.Count > 0);
		const FGFItemRow* Row = Items->FindRow<FGFItemRow>(S.ItemId, TEXT("TestKit"), false);
		if (!TestNotNull(FString::Printf(TEXT("%s is in DT_Items"), *S.ItemId.ToString()), Row))
		{
			continue;
		}
		Outfits += Row->Category == EGFItemCategory::Outfit ? 1 : 0;
		Materials += Row->Category == EGFItemCategory::Material ? 1 : 0;
		bHasPass |= S.ItemId == Pass && S.Count == 1;
		const FGFBlockRow* Block = Blocks->FindRow<FGFBlockRow>(S.ItemId, TEXT("TestKit"), false);
		if (Block && Block->bPlaceable)
		{
			const FName Places = Block->DropItem.IsNone() ? S.ItemId : Block->DropItem;
			TestEqual(FString::Printf(TEXT("%s block uses its own item"), *S.ItemId.ToString()), Places, S.ItemId);
			++PlaceableBlocks;
		}
	}
	TestTrue(TEXT("At least three placeable blocks"), PlaceableBlocks >= 3);
	TestTrue(TEXT("At least one crafting material"), Materials >= 1);
	TestEqual(TEXT("Exactly one outfit item"), Outfits, 1);
	TestTrue(TEXT("One ferry pass"), bHasPass);

	// Top-up: first grant fills, second adds nothing, extra stock is kept.
	FGFInventory Inv;
	Inv.Add(TEXT("Dirt"), 100);
	const int32 First = FGFTestKit::TopUp(Inv, Pass);
	TestTrue(TEXT("First top-up adds items"), First > 0);
	TestEqual(TEXT("Second top-up adds nothing"), FGFTestKit::TopUp(Inv, Pass), 0);
	TestEqual(TEXT("Missing is empty after top-up"), FGFTestKit::Missing(Inv, Pass).Num(), 0);
	TestEqual(TEXT("Extra Dirt is kept"), Inv.GetCount(TEXT("Dirt")), 100);
	TestEqual(TEXT("Only one pass"), Inv.GetCount(Pass), 1);
	for (const FGFItemStack& S : Kit)
	{
		TestTrue(FString::Printf(TEXT("%s reaches its target"), *S.ItemId.ToString()), Inv.GetCount(S.ItemId) >= S.Count);
	}

	// With the pass the ferry is free.
	FGFFerryTuning Tuning;
	Tuning.FareItem = TEXT("Dirt");
	Tuning.FareCount = 3;
	Tuning.PassItem = Pass;
	TestEqual(TEXT("Ferry quote is 0 with the kit's pass"), FGFFerryRules::Quote(Inv, Tuning), 0);

	// The repo keeps auto-grant off; packages turn it on in their own config.
	TestFalse(TEXT("Auto-grant is off by default"), GetDefault<UGFTestKitSubsystem>()->bAutoGrantTestKit);
	return true;
}

#endif
