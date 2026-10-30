// Landlord rules (PLAN.md section 2a), no world: layout, rent status ladder, accrual cap, permissions, rent caps,
// and blueprint math on a bare voxel grid.
#include "Misc/AutomationTest.h"
#include "GFLandTypes.h"
#include "GFEconomy.h"
#include "GFDataTables.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFLandTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

template <typename E>
static bool CheckLandEnum(FAutomationTestBase& Test, const TCHAR* What, E Actual, E Expected)
{
	return Test.TestEqual(What, UEnum::GetValueAsString(Actual), UEnum::GetValueAsString(Expected));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLandRulesTest, "GFGame.Land.Rules", GFLandTestFlags)
bool FGFLandRulesTest::RunTest(const FString&)
{
	// Layout: the host's home + 2 rentable plots, 24 x 24 full height, no overlaps; the rest is the Commons.
	FGFLandState Land = FGFLandRules::MakeDefaultLayout(FIntVector(64, 64, 32));
	TestEqual(TEXT("3 plots"), Land.Plots.Num(), 3);
	const FGFLandPlot* Home = Land.Find(FGFLandRules::HomePlotId());
	FGFLandPlot* A = Land.Find(FGFLandRules::PlotAId());
	const FGFLandPlot* B = Land.Find(FGFLandRules::PlotBId());
	if (!TestTrue(TEXT("Home, PlotA, PlotB exist"), Home && A && B))
	{
		return false;
	}
	CheckLandEnum(*this, TEXT("Home is home"), Home->Kind, EGFPlotKind::Home);
	TestTrue(TEXT("Plot edge 24"), A->Size == FIntVector(24, 24, 32));
	TestTrue(TEXT("Rentable plots are for rent at the default rent"), A->bForRent && A->RentItem == FName(TEXT("Wood")) && A->RentAmount == 5 && A->Kind == EGFPlotKind::Rentable);
	for (const FGFLandPlot& P : Land.Plots)
	{
		TestTrue(*FString::Printf(TEXT("%s inside the world"), *P.PlotId.ToString()), P.Min.X >= 0 && P.Min.Y >= 0 && P.Max().X < 64 && P.Max().Y < 64 && P.Max().Z == 31);
		for (const FGFLandPlot& Q : Land.Plots)
		{
			if (&P != &Q)
			{
				const bool bOverlap = P.Min.X <= Q.Max().X && Q.Min.X <= P.Max().X && P.Min.Y <= Q.Max().Y && Q.Min.Y <= P.Max().Y;
				TestFalse(TEXT("Plots don't overlap"), bOverlap);
			}
		}
	}
	TestTrue(TEXT("Cell in PlotA"), Land.PlotAt(FIntVector(40, 10, 5)) == A);
	TestNull(TEXT("Commons cell"), Land.PlotAt(FIntVector(50, 50, 1)));
	CheckLandEnum(*this, TEXT("Vacant"), A->Status, EGFRentStatus::Vacant);
	CheckLandEnum(*this, TEXT("Home is Free"), Home->Status, EGFRentStatus::Free);

	// Status ladder: Paid -> Due -> Overgrown -> Faded; Free when there's no rent.
	A->TenantKey = TEXT("guest");
	A->LastAccruedDay = 100;
	FGFLandRules::RefreshStatus(*A);
	CheckLandEnum(*this, TEXT("Tenant, nothing owed: Paid"), A->Status, EGFRentStatus::Paid);
	TestEqual(TEXT("Same day: nothing accrues"), FGFLandRules::Accrue(*A, 100), 0);
	TestEqual(TEXT("1 day"), FGFLandRules::Accrue(*A, 101), 1);
	CheckLandEnum(*this, TEXT("1 missed: Due"), A->Status, EGFRentStatus::Due);
	FGFLandRules::Accrue(*A, 102);
	CheckLandEnum(*this, TEXT("2 missed: Overgrown"), A->Status, EGFRentStatus::Overgrown);
	TestEqual(TEXT("Many days: capped"), FGFLandRules::Accrue(*A, 150), 1);
	TestEqual(TEXT("Capped at 3 periods"), A->OwedPeriods, 3);
	CheckLandEnum(*this, TEXT("3 missed: Faded"), A->Status, EGFRentStatus::Faded);
	TestEqual(TEXT("Going back in time never charges"), FGFLandRules::Accrue(*A, 140), 0);
	TestEqual(TEXT("Affordable: 12 wood at 5/day = 2 days"), FGFLandRules::AffordablePeriods(*A, 12), 2);
	TestEqual(TEXT("Affordable: 4 wood = 0"), FGFLandRules::AffordablePeriods(*A, 4), 0);
	TestEqual(TEXT("Affordable: lots = what's owed"), FGFLandRules::AffordablePeriods(*A, 999), 3);
	A->OwedPeriods = 0;
	A->bRentPaused = true;
	TestEqual(TEXT("Paused: nothing accrues"), FGFLandRules::Accrue(*A, 160), 0);
	A->bRentPaused = false;
	TestEqual(TEXT("Unpaused: no back-charge for the holiday"), FGFLandRules::Accrue(*A, 160), 0);
	A->RentAmount = 0;
	TestEqual(TEXT("Free plot: nothing accrues"), FGFLandRules::Accrue(*A, 170), 0);
	CheckLandEnum(*this, TEXT("Free plot status"), A->Status, EGFRentStatus::Free);
	A->RentAmount = 5;
	FGFLandRules::Accrue(*A, 170);

	// Permissions.
	Land.Builders.Add(TEXT("builder"));
	FString Why;
	TestTrue(TEXT("Host edits their home"), FGFLandRules::CanEdit(Land, TEXT("host"), true, FIntVector(5, 5, 1), true, Why));
	TestTrue(TEXT("Host edits a guest plot"), FGFLandRules::CanEdit(Land, TEXT("host"), true, FIntVector(40, 10, 1), true, Why));
	TestTrue(TEXT("Host edits the commons"), FGFLandRules::CanEdit(Land, TEXT("host"), true, FIntVector(50, 50, 1), true, Why));
	TestTrue(TEXT("Tenant edits own plot"), FGFLandRules::CanEdit(Land, TEXT("guest"), false, FIntVector(40, 10, 1), true, Why));
	TestFalse(TEXT("Tenant can't edit the host's home"), FGFLandRules::CanEdit(Land, TEXT("guest"), false, FIntVector(5, 5, 1), false, Why));
	TestFalse(TEXT("Tenant can't edit a vacant plot"), FGFLandRules::CanEdit(Land, TEXT("guest"), false, FIntVector(10, 40, 1), true, Why));
	TestFalse(TEXT("Other guest can't edit PlotA"), FGFLandRules::CanEdit(Land, TEXT("other"), false, FIntVector(40, 10, 1), false, Why));
	TestFalse(TEXT("Guest can't edit the commons"), FGFLandRules::CanEdit(Land, TEXT("guest"), false, FIntVector(50, 50, 1), true, Why));
	TestTrue(TEXT("Builder edits the commons"), FGFLandRules::CanEdit(Land, TEXT("builder"), false, FIntVector(50, 50, 1), true, Why));
	TestFalse(TEXT("Builder can't edit a guest plot"), FGFLandRules::CanEdit(Land, TEXT("builder"), false, FIntVector(40, 10, 1), true, Why));
	A->OwedPeriods = 2;
	FGFLandRules::RefreshStatus(*A);
	TestFalse(TEXT("Overgrown: can't place"), FGFLandRules::CanEdit(Land, TEXT("guest"), false, FIntVector(40, 10, 1), true, Why));
	TestTrue(TEXT("Overgrown: can remove"), FGFLandRules::CanEdit(Land, TEXT("guest"), false, FIntVector(40, 10, 1), false, Why));
	A->OwedPeriods = 1;
	FGFLandRules::RefreshStatus(*A);
	TestTrue(TEXT("Due (grace day): can still place"), FGFLandRules::CanEdit(Land, TEXT("guest"), false, FIntVector(40, 10, 1), true, Why));

	// Rent limits from the item table (value per period <= 30, rentable materials only).
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (TestNotNull(TEXT("DT_Items"), Items))
	{
		CheckLandEnum(*this, TEXT("Free is fine"), FGFLandRules::ValidateRent(Items, NAME_None, 0), EGFLandResult::Ok);
		CheckLandEnum(*this, TEXT("30 wood ok"), FGFLandRules::ValidateRent(Items, TEXT("Wood"), 30), EGFLandResult::Ok);
		CheckLandEnum(*this, TEXT("31 wood too high"), FGFLandRules::ValidateRent(Items, TEXT("Wood"), 31), EGFLandResult::RentTooHigh);
		CheckLandEnum(*this, TEXT("3 amber ok (24)"), FGFLandRules::ValidateRent(Items, TEXT("Amber"), 3), EGFLandResult::Ok);
		CheckLandEnum(*this, TEXT("4 amber too high (32)"), FGFLandRules::ValidateRent(Items, TEXT("Amber"), 4), EGFLandResult::RentTooHigh);
		CheckLandEnum(*this, TEXT("Moonstone isn't rentable"), FGFLandRules::ValidateRent(Items, TEXT("Moonstone"), 1), EGFLandResult::NotRentable);
		CheckLandEnum(*this, TEXT("Unknown item isn't rentable"), FGFLandRules::ValidateRent(Items, TEXT("NotAThing"), 1), EGFLandResult::NotRentable);
		TestEqual(TEXT("Suggested wood"), FGFLandRules::SuggestedAmount(Items, TEXT("Wood")), 10);
		TestEqual(TEXT("Suggested amber"), FGFLandRules::SuggestedAmount(Items, TEXT("Amber")), 1);
		TestEqual(TEXT("No suggestion for moonstone"), FGFLandRules::SuggestedAmount(Items, TEXT("Moonstone")), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLandBlueprintMathTest, "GFGame.Land.BlueprintMath", GFLandTestFlags)
bool FGFLandBlueprintMathTest::RunTest(const FString&)
{
	FGFVoxelGrid Grid(FIntVector(64, 64, 32));
	const FGFLandState Land = FGFLandRules::MakeDefaultLayout(Grid.GetSize());
	const FGFLandPlot& A = *Land.Find(FGFLandRules::PlotAId());
	const FGFLandPlot& B = *Land.Find(FGFLandRules::PlotBId());
	for (int32 Y = 0; Y < 64; ++Y)
	{
		for (int32 X = 0; X < 64; ++X)
		{
			Grid.Set(FIntVector(X, Y, 0), TEXT("Grass"));
		}
	}
	TestFalse(TEXT("Bare ground isn't 'built on'"), FGFLandRules::IsBuiltOn(Grid, A));
	TestEqual(TEXT("Same ground hashes equal on both plots"), FGFLandRules::PlotHash(Grid, A), FGFLandRules::PlotHash(Grid, B));

	// A little house on PlotA.
	auto Put = [&Grid, &A](int32 X, int32 Y, int32 Z, const TCHAR* Block) { Grid.Set(A.Min + FIntVector(X, Y, Z), Block); };
	for (int32 i = 0; i < 4; ++i)
	{
		Put(i, 0, 1, TEXT("WoodPlank"));
		Put(i, 1, 1, TEXT("Stone"));
	}
	Put(2, 2, 2, TEXT("Stone"));
	TestTrue(TEXT("Built on"), FGFLandRules::IsBuiltOn(Grid, A));
	TestFalse(TEXT("Neighbour untouched"), FGFLandRules::IsBuiltOn(Grid, B));

	const FGFPlotBlueprint BP = FGFLandRules::MakeBlueprint(Grid, A, TEXT("world-1"), FDateTime(2026, 10, 29));
	TestEqual(TEXT("Blueprint hash = plot hash"), BP.Hash, FGFLandRules::PlotHash(Grid, A));
	TestEqual(TEXT("Blueprint hash = hash of its data"), BP.Hash, static_cast<int64>(FGFVoxelGrid::HashSaveData(BP.Voxels, BP.Size)));
	TestEqual(TEXT("Blocks: 24x24 ground + 9"), BP.BlockCount, 24 * 24 + 9);
	TestTrue(TEXT("Size"), BP.Size == A.Size);

	const TArray<FGFItemStack> Cost = FGFLandRules::RebuildCost(BP, [](FName Block) { return Block == FName(TEXT("Grass")) ? FName(TEXT("Dirt")) : Block; });
	TestEqual(TEXT("Rebuild cost: 2 materials (ground is free)"), Cost.Num(), 2);
	for (const FGFItemStack& S : Cost)
	{
		TestEqual(*FString::Printf(TEXT("Cost of %s"), *S.ItemId.ToString()), S.Count, S.ItemId == FName(TEXT("Stone")) ? 5 : 4);
	}

	// Book of blueprints: one per world + plot, replaced when it changes.
	TArray<FGFPlotBlueprint> Book;
	TestTrue(TEXT("Stored"), FGFLandRules::StoreBlueprint(Book, BP));
	TestFalse(TEXT("Same backup again: no change"), FGFLandRules::StoreBlueprint(Book, BP));
	FGFPlotBlueprint Other = BP;
	Other.HostWorldId = TEXT("world-2");
	TestTrue(TEXT("Other world: second slot"), FGFLandRules::StoreBlueprint(Book, Other));
	Put(3, 3, 3, TEXT("Stone"));
	TestTrue(TEXT("Changed build replaces the slot"), FGFLandRules::StoreBlueprint(Book, FGFLandRules::MakeBlueprint(Grid, A, TEXT("world-1"), FDateTime(2026, 10, 30))));
	TestEqual(TEXT("Still 2 slots"), Book.Num(), 2);
	const FGFPlotBlueprint* Found = FGFLandRules::FindBlueprint(Book, TEXT("world-1"), A.PlotId);
	TestTrue(TEXT("Latest backup found"), Found && Found->BlockCount == 24 * 24 + 10);

	// The same build stamped onto PlotB hashes the same as on PlotA.
	FGFVoxelGrid Temp(BP.Size);
	Temp.FromSaveData(BP.Voxels);
	Temp.ForEachSolid([&Grid, &B](const FIntVector& C, FName Id) { Grid.Set(B.Min + C, Id); });
	TestEqual(TEXT("Restored copy on PlotB hashes like the blueprint"), FGFLandRules::PlotHash(Grid, B), BP.Hash);
	return true;
}

#endif
