// Block paint economy (VoxelFinish hookup): what a finish costs, and that dye items are burned through the ledger.
#include "Misc/AutomationTest.h"
#include "GFDyes.h"
#include "GFEconomy.h"
#include "GFPaintVolume.h"
#include "VoxelFinish.h"
#include "VoxelFinishTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFPaintTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFPaintTest
{
	static const FName Rose(TEXT("RoseDye"));
	static const FName Sky(TEXT("SkyDye"));
	static const FLinearColor Red(0.9f, 0.1f, 0.1f);
	static const FLinearColor Pink(1.f, 0.4f, 0.7f);
	static const FLinearColor Blue(0.1f, 0.3f, 0.9f);

	static FFinish Solid(const FLinearColor& C)
	{
		FFinish F;
		F.PatternId = VoxelFinish::SolidPatternId;
		F.ColorA = C;
		F.ColorB = FLinearColor::White;
		return F;
	}

	static FFinish Pattern(const FLinearColor& A, const FLinearColor& B)
	{
		FFinish F = Solid(A);
		F.PatternId = 3;
		F.ColorB = B;
		return F;
	}

	struct FBank
	{
		FGFMapLedgerAccounts Accounts;
		FGFTransferLedger Ledger;
		int32 Burns = 0;

		bool Spend(FGFPaintCredits& Credits, const FString& Who, const FFinish& Finish)
		{
			return Credits.TrySpend(Who, FGFPaintCost::VisibleColors(Finish), [this, &Who](const TArray<FGFItemStack>& Stacks)
			{
				++Burns;
				return Ledger.Burn(Accounts, FGuid::NewGuid(), Who, Stacks, AGFPaintVolume::BurnReason()) == EGFTxResult::Applied;
			});
		}
		int32 Count(const FString& Who, FName Item) const
		{
			const FGFInventory* Inv = Accounts.Accounts.Find(Who);
			return Inv ? Inv->GetCount(Item) : 0;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPaintCostTest, "GFGame.Paint.Cost", GFPaintTestFlags)
bool FGFPaintCostTest::RunTest(const FString&)
{
	using namespace GFPaintTest;

	TestEqual(TEXT("A solid finish shows one colour"), FGFPaintCost::VisibleColors(Solid(Red)).Num(), 1);
	TestEqual(TEXT("A pattern shows two"), FGFPaintCost::VisibleColors(Pattern(Red, Blue)).Num(), 2);
	TestTrue(TEXT("Solid red takes RoseDye"), FGFPaintCost::DyesFor(Solid(Red)) == TArray<FName>({ Rose }));
	TestTrue(TEXT("Red over blue takes RoseDye and SkyDye"), FGFPaintCost::DyesFor(Pattern(Red, Blue)) == TArray<FName>({ Rose, Sky }));
	TestTrue(TEXT("Red over pink is one family"), FGFPaintCost::DyesFor(Pattern(Red, Pink)) == TArray<FName>({ Rose }));
	FFinish Tinted = Solid(FLinearColor::White);
	Tinted.Tint = Blue;
	TestTrue(TEXT("The tint counts: white tinted blue takes SkyDye"), FGFPaintCost::DyesFor(Tinted) == TArray<FName>({ Sky }));
	FFinish HiddenB = Solid(Red);
	HiddenB.ColorB = Blue;
	TestTrue(TEXT("ColorB is not charged when no pattern shows it"), FGFPaintCost::DyesFor(HiddenB) == TArray<FName>({ Rose }));

	// One dye item covers FGFDyes::BlocksPerDye (8) blocks: the first block burns it, the next 7 use the credit.
	FBank Bank;
	FGFPaintCredits Credits;
	const FString P(TEXT("Painter"));
	Bank.Accounts.Accounts.FindOrAdd(P).Add(Rose, 2);
	TestEqual(TEXT("Batch size is CostForBlocks: 8 blocks for 1 dye"), FGFDyes::CostForBlocks(Red, FGFDyes::BlocksPerDye)[0].Count, 1);
	TestTrue(TEXT("Block 1 is paid"), Bank.Spend(Credits, P, Solid(Red)));
	TestEqual(TEXT("Block 1 burns one RoseDye"), Bank.Count(P, Rose), 1);
	TestEqual(TEXT("...and leaves 7 block credits"), Credits.GetCredits(P, Rose), 7);
	for (int32 i = 2; i <= 8; ++i)
	{
		TestTrue(*FString::Printf(TEXT("Block %d is paid"), i), Bank.Spend(Credits, P, Solid(Red)));
	}
	TestEqual(TEXT("8 blocks burned one RoseDye"), Bank.Ledger.GetBurned(Rose), static_cast<int64>(1));
	TestEqual(TEXT("One burn for 8 blocks"), Bank.Burns, 1);
	TestTrue(TEXT("Block 9 is paid"), Bank.Spend(Credits, P, Solid(Pink)));
	TestEqual(TEXT("Block 9 burns the second RoseDye"), Bank.Count(P, Rose), 0);
	for (int32 i = 10; i <= 16; ++i)
	{
		Bank.Spend(Credits, P, Solid(Red));
	}
	TestEqual(TEXT("16 blocks used both dyes"), Credits.GetCredits(P, Rose), 0);
	const int64 BurnedBefore = Bank.Ledger.GetBurned(Rose);
	TestFalse(TEXT("Block 17 without dye is refused"), Bank.Spend(Credits, P, Solid(Red)));
	TestEqual(TEXT("A refused block burns nothing"), Bank.Ledger.GetBurned(Rose), BurnedBefore);
	TestEqual(TEXT("...and takes no credit"), Credits.GetCredits(P, Rose), 0);

	// Two families: all or nothing.
	const FString Q(TEXT("Other"));
	Bank.Accounts.Accounts.FindOrAdd(Q).Add(Rose, 1);
	TestFalse(TEXT("Red over blue without SkyDye is refused"), Bank.Spend(Credits, Q, Pattern(Red, Blue)));
	TestEqual(TEXT("RoseDye was not burned on its own"), Bank.Count(Q, Rose), 1);
	TestEqual(TEXT("No RoseDye credit either"), Credits.GetCredits(Q, Rose), 0);
	Bank.Accounts.Accounts.FindOrAdd(Q).Add(Sky, 1);
	const int32 BurnsBefore = Bank.Burns;
	TestTrue(TEXT("With both dyes it is paid"), Bank.Spend(Credits, Q, Pattern(Red, Blue)));
	TestEqual(TEXT("Both dyes in one ledger burn"), Bank.Burns, BurnsBefore + 1);
	TestEqual(TEXT("That burn has two stacks"), Bank.Ledger.GetRecent().Last().Stacks.Num(), 2);
	TestEqual(TEXT("Reason is Paint.Dye"), Bank.Ledger.GetRecent().Last().Reason, AGFPaintVolume::BurnReason());
	TestEqual(TEXT("7 RoseDye credits"), Credits.GetCredits(Q, Rose), 7);
	TestEqual(TEXT("7 SkyDye credits"), Credits.GetCredits(Q, Sky), 7);
	TestTrue(TEXT("Solid red now uses the Rose credit only"), Bank.Spend(Credits, Q, Solid(Red)));
	TestEqual(TEXT("...without a burn"), Bank.Burns, BurnsBefore + 1);
	TestEqual(TEXT("Credits are per player"), Credits.GetCredits(P, Sky), 0);
	TestFalse(TEXT("No account, no paint"), Bank.Spend(Credits, FString(), Solid(Red)));
	return true;
}

#endif
