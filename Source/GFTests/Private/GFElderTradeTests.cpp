// Elder prices, idempotent trades, and a quest accept that sticks the first time.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Elder;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "GFElderTalk.h"
#include "GFElderTrade.h"
#include "GFItemTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFElderTradeTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	int32 KnownValue(FName Id)
	{
		if (Id == TEXT("Wood") || Id == TEXT("Fiber") || Id == TEXT("Flowers") || Id == TEXT("Berry") || Id == TEXT("Coalnut"))
		{
			return 1;
		}
		if (Id == TEXT("Metal") || Id == TEXT("Hide") || Id == TEXT("Scales") || Id == TEXT("Fabric") || Id == TEXT("Bone"))
		{
			return 3;
		}
		return 0;
	}

	int64 StackValue(const TArray<FGFItemStack>& Stacks, TFunctionRef<int32(FName)> ValueOf)
	{
		int64 Total = 0;
		for (const FGFItemStack& Stack : Stacks)
		{
			Total += int64(ValueOf(Stack.ItemId)) * int64(Stack.Count);
		}
		return Total;
	}

	int64 CountAll(const FGFMapLedgerAccounts& Accounts, FName ItemId)
	{
		int64 Total = 0;
		for (const TPair<FString, FGFInventory>& Pair : Accounts.Accounts)
		{
			Total += Pair.Value.GetCount(ItemId);
		}
		return Total;
	}

	bool SameStacks(const TArray<FGFItemStack>& A, const TArray<FGFItemStack>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].ItemId != B[Index].ItemId || A[Index].Count != B[Index].Count)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderTradePricesTest, "GFGame.Elder.Trade.Prices", GFElderTradeTest::Flags)
bool FGFElderTradePricesTest::RunTest(const FString&)
{
	using namespace GFElderTradeTest;
	TestTrue(TEXT("Buy multiplier"), FMath::IsNearlyEqual(FGFElderTrade::BuyMultiplier, 1.25));
	TestTrue(TEXT("Sell multiplier"), FMath::IsNearlyEqual(FGFElderTrade::SellMultiplier, 0.8));
	TestEqual(TEXT("Buy 1 rounds to 1"), FGFElderTrade::Quote(EGFElderTradeKind::Buy, 1), 1);
	TestEqual(TEXT("Buy 2 rounds to 3"), FGFElderTrade::Quote(EGFElderTradeKind::Buy, 2), 3);
	TestEqual(TEXT("Buy 3 rounds to 4"), FGFElderTrade::Quote(EGFElderTradeKind::Buy, 3), 4);
	TestEqual(TEXT("Buy 4 rounds to 5"), FGFElderTrade::Quote(EGFElderTradeKind::Buy, 4), 5);
	TestEqual(TEXT("Buy 6 rounds to 8"), FGFElderTrade::Quote(EGFElderTradeKind::Buy, 6), 8);
	TestEqual(TEXT("Sell 1 rounds to 1"), FGFElderTrade::Quote(EGFElderTradeKind::Sell, 1), 1);
	TestEqual(TEXT("Sell 3 rounds to 2"), FGFElderTrade::Quote(EGFElderTradeKind::Sell, 3), 2);
	TestEqual(TEXT("Sell 6 rounds to 5"), FGFElderTrade::Quote(EGFElderTradeKind::Sell, 6), 5);

	const FGFElderTradeCatalog& Builtin = FGFElderTradeCatalog::Builtin();
	const FGFElderTradeCatalog Loaded = FGFElderTradeCatalog::Load();
	TestEqual(TEXT("Nine trades"), Builtin.Rows.Num(), 9);
	TestEqual(TEXT("CSV matches the builtin count"), Loaded.Rows.Num(), Builtin.Rows.Num());

	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	const FName Ids[] = { TEXT("Wood"), TEXT("Fiber"), TEXT("Flowers"), TEXT("Berry"), TEXT("Coalnut"), TEXT("Metal"), TEXT("Hide"), TEXT("Scales"), TEXT("Fabric"), TEXT("Bone") };
	for (const FName Id : Ids)
	{
		TestEqual(*FString::Printf(TEXT("%s value"), *Id.ToString()), FGFEconomy::UnitValue(Items, Id), KnownValue(Id));
	}

	auto ValueOf = [Items](FName Id) { return FGFEconomy::UnitValue(Items, Id); };
	const FName Towns[] = { TEXT("Rime"), TEXT("Verdant"), TEXT("Cinderwrights") };
	for (const FName Town : Towns)
	{
		TestEqual(*FString::Printf(TEXT("%s has 3 rows"), *Town.ToString()), Loaded.ForTown(Town).Num(), 3);
	}
	for (int32 Index = 0; Index < Loaded.Rows.Num(); ++Index)
	{
		const FGFElderTradeListing& Row = Loaded.Rows[Index];
		const FGFElderTradeListing* Built = Builtin.Find(Row.RowId);
		if (!TestNotNull(*FString::Printf(TEXT("Builtin %s"), *Row.RowId.ToString()), Built))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s kind"), *Row.RowId.ToString()), Built->Kind == Row.Kind && Built->TownId == Row.TownId);
		TestTrue(*FString::Printf(TEXT("%s give"), *Row.RowId.ToString()), SameStacks(Built->Give, Row.Give));
		TestTrue(*FString::Printf(TEXT("%s get"), *Row.RowId.ToString()), SameStacks(Built->Get, Row.Get));
		const int64 GiveValue = StackValue(Row.Give, ValueOf);
		const int64 GetValue = StackValue(Row.Get, ValueOf);
		if (Row.Kind == EGFElderTradeKind::Buy)
		{
			TestEqual(*FString::Printf(TEXT("%s buy price"), *Row.RowId.ToString()), int32(GiveValue), FGFElderTrade::Quote(EGFElderTradeKind::Buy, GetValue));
		}
		else
		{
			TestEqual(*FString::Printf(TEXT("%s sell price"), *Row.RowId.ToString()), int32(GetValue), FGFElderTrade::Quote(EGFElderTradeKind::Sell, GiveValue));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderTradeIdempotentTest, "GFGame.Elder.Trade.Idempotent", GFElderTradeTest::Flags)
bool FGFElderTradeIdempotentTest::RunTest(const FString&)
{
	using namespace GFElderTradeTest;
	const FString Player = TEXT("P");
	const FString Elder = TEXT("elder:Rime");
	const FName RowId = TEXT("Rime_Fiber");
	const TArray<FGFItemStack> Give = { FGFItemStack(TEXT("Wood"), 5) };
	const TArray<FGFItemStack> Get = { FGFItemStack(TEXT("Fiber"), 4) };
	const FGuid Session(1, 2, 3, 4);
	const FGuid TxId = FGFElderTrade::MakeTxId(Session, Player, Elder, RowId, 1);
	TestTrue(TEXT("TxId is stable"), TxId == FGFElderTrade::MakeTxId(Session, Player, Elder, RowId, 1));
	TestFalse(TEXT("A new request is a new TxId"), TxId == FGFElderTrade::MakeTxId(Session, Player, Elder, RowId, 2));
	TestTrue(TEXT("TxId is valid"), TxId.IsValid());
	TestFalse(TEXT("Mint child is not the parent"), FGFElderTrade::ChildId(TxId, 1) == TxId);
	TestFalse(TEXT("Get child is not the mint child"), FGFElderTrade::ChildId(TxId, 1) == FGFElderTrade::ChildId(TxId, 2));

	{
		FGFMapLedgerAccounts Accounts;
		FGFInventory Pocket;
		Accounts.Accounts.Add(Player, Pocket);
		FGFTransferLedger Ledger;
		FString Why;
		const EGFTxResult Short = FGFElderTrade::Apply(Ledger, Accounts, TxId, Player, Elder, Give, Get, Why);
		TestTrue(TEXT("Short is refused"), Short == EGFTxResult::InsufficientItems);
		TestFalse(TEXT("Why names the gap"), Why.IsEmpty());
		TestFalse(TEXT("A refusal does not consume the id"), Ledger.WasApplied(TxId));
		TestEqual(TEXT("Wood untouched"), CountAll(Accounts, TEXT("Wood")), int64(0));
		TestEqual(TEXT("Nothing minted"), Ledger.GetMinted(TEXT("Fiber")), int64(0));

		Accounts.Accounts[Player].Add(TEXT("Wood"), 5);
		const int64 WoodBefore = CountAll(Accounts, TEXT("Wood"));
		const int64 FiberBefore = CountAll(Accounts, TEXT("Fiber"));
		Why.Reset();
		const EGFTxResult Applied = FGFElderTrade::Apply(Ledger, Accounts, TxId, Player, Elder, Give, Get, Why);
		TestTrue(TEXT("The same id now applies"), Applied == EGFTxResult::Applied);
		TestTrue(TEXT("No toast on success"), Why.IsEmpty());
		TestEqual(TEXT("Player paid the wood"), Accounts.Accounts[Player].GetCount(TEXT("Wood")), 0);
		TestEqual(TEXT("Player received the fiber"), Accounts.Accounts[Player].GetCount(TEXT("Fiber")), 4);
		TestEqual(TEXT("Elder holds the wood"), Accounts.Accounts[Elder].GetCount(TEXT("Wood")), 5);
		TestEqual(TEXT("Wood total is unchanged"), CountAll(Accounts, TEXT("Wood")), WoodBefore);
		TestEqual(TEXT("Fiber total is the mint"), CountAll(Accounts, TEXT("Fiber")), FiberBefore + Ledger.GetMinted(TEXT("Fiber")) - Ledger.GetBurned(TEXT("Fiber")));
		TestEqual(TEXT("Fiber was minted"), Ledger.GetMinted(TEXT("Fiber")), int64(4));
		TestEqual(TEXT("Fiber was not burned"), Ledger.GetBurned(TEXT("Fiber")), int64(0));
		TestEqual(TEXT("Wood was not minted"), Ledger.GetMinted(TEXT("Wood")), int64(0));
		bool bMintToElder = false;
		bool bBadBurn = false;
		for (const FGFLedgerEntry& Entry : Ledger.GetRecent())
		{
			if (Entry.Kind == EGFTxKind::Mint)
			{
				bMintToElder = Entry.To == Elder;
			}
			if (Entry.Kind == EGFTxKind::Burn && Entry.From != Elder)
			{
				bBadBurn = true;
			}
		}
		TestTrue(TEXT("Mint lands on the elder account"), bMintToElder);
		TestFalse(TEXT("No burn outside the elder account"), bBadBurn);

		const int32 WoodAfter = Accounts.Accounts[Player].GetCount(TEXT("Wood"));
		const int32 FiberAfter = Accounts.Accounts[Player].GetCount(TEXT("Fiber"));
		Why.Reset();
		const EGFTxResult Replay = FGFElderTrade::Apply(Ledger, Accounts, TxId, Player, Elder, Give, Get, Why);
		TestTrue(TEXT("Resend does not pay twice"), Replay == EGFTxResult::AlreadyApplied);
		TestEqual(TEXT("Wood stays"), Accounts.Accounts[Player].GetCount(TEXT("Wood")), WoodAfter);
		TestEqual(TEXT("Fiber stays"), Accounts.Accounts[Player].GetCount(TEXT("Fiber")), FiberAfter);
		TestEqual(TEXT("Mint stays"), Ledger.GetMinted(TEXT("Fiber")), int64(4));

		Accounts.Accounts[Player].Add(TEXT("Wood"), 5);
		const FGuid Again = FGFElderTrade::MakeTxId(Session, Player, Elder, RowId, 2);
		Why.Reset();
		TestTrue(TEXT("A new request pays again"), FGFElderTrade::Apply(Ledger, Accounts, Again, Player, Elder, Give, Get, Why) == EGFTxResult::Applied);
		TestEqual(TEXT("Second fiber pile"), Accounts.Accounts[Player].GetCount(TEXT("Fiber")), 8);
		TestEqual(TEXT("Second mint"), Ledger.GetMinted(TEXT("Fiber")), int64(8));
	}

	{
		FGFMapLedgerAccounts Accounts;
		FGFInventory Pocket;
		Pocket.Add(TEXT("Wood"), 5);
		FGFInventory Shelf;
		Shelf.Add(TEXT("Fiber"), 4);
		Accounts.Accounts.Add(Player, Pocket);
		Accounts.Accounts.Add(Elder, Shelf);
		FGFTransferLedger Ledger;
		const int64 WoodBefore = CountAll(Accounts, TEXT("Wood"));
		const int64 FiberBefore = CountAll(Accounts, TEXT("Fiber"));
		FString Why;
		const FGuid StockedId = FGFElderTrade::MakeTxId(Session, Player, Elder, RowId, 7);
		TestTrue(TEXT("Stocked elder needs no mint"), FGFElderTrade::Apply(Ledger, Accounts, StockedId, Player, Elder, Give, Get, Why) == EGFTxResult::Applied);
		TestEqual(TEXT("Wood still conserved"), CountAll(Accounts, TEXT("Wood")), WoodBefore);
		TestEqual(TEXT("Fiber still conserved"), CountAll(Accounts, TEXT("Fiber")), FiberBefore);
		TestEqual(TEXT("No mint when the shelf had the goods"), Ledger.GetMinted(TEXT("Fiber")), int64(0));
		TestEqual(TEXT("No burn when the shelf had the goods"), Ledger.GetBurned(TEXT("Fiber")), int64(0));
		TestEqual(TEXT("Player took the fiber"), Accounts.Accounts[Player].GetCount(TEXT("Fiber")), 4);
		TestEqual(TEXT("Elder took the wood"), Accounts.Accounts[Elder].GetCount(TEXT("Wood")), 5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderTradeSessionScopedTest, "GFGame.Elder.Trade.SessionScopedTxId", GFElderTradeTest::Flags)
bool FGFElderTradeSessionScopedTest::RunTest(const FString&)
{
	using namespace GFElderTradeTest;
	// #304 pass 2: the client request counter starts over with every controller, but the ledger (saved) remembers
	// every applied id. The menu session id keeps a rejoin's "request 1" from matching last session's "request 1".
	const FString Player = TEXT("P");
	const FString Elder = TEXT("elder:Rime");
	const FName RowId = TEXT("Rime_Fiber");
	const TArray<FGFItemStack> Give = { FGFItemStack(TEXT("Wood"), 5) };
	const TArray<FGFItemStack> Get = { FGFItemStack(TEXT("Fiber"), 4) };
	const FGuid Before = FGuid::NewGuid();
	const FGuid After = FGuid::NewGuid();
	TestFalse(TEXT("same request in a new session is a new id"),
		FGFElderTrade::MakeTxId(Before, Player, Elder, RowId, 1) == FGFElderTrade::MakeTxId(After, Player, Elder, RowId, 1));
	TestTrue(TEXT("same request in the same session is the same id"),
		FGFElderTrade::MakeTxId(Before, Player, Elder, RowId, 1) == FGFElderTrade::MakeTxId(Before, Player, Elder, RowId, 1));

	FGFMapLedgerAccounts Accounts;
	FGFInventory Pocket;
	Pocket.Add(TEXT("Wood"), 10);
	Accounts.Accounts.Add(Player, Pocket);
	FGFTransferLedger Ledger;
	FString Why;
	TestTrue(TEXT("first session trade applies"),
		FGFElderTrade::Apply(Ledger, Accounts, FGFElderTrade::MakeTxId(Before, Player, Elder, RowId, 1), Player, Elder, Give, Get, Why) == EGFTxResult::Applied);

	// The ledger survives (save, rejoin); the counter does not. Request 1 again, new session.
	FGFTransferLedger Reloaded = Ledger;
	Reloaded.RebuildIndex();
	Why.Reset();
	TestTrue(TEXT("rejoin trade applies instead of AlreadyApplied"),
		FGFElderTrade::Apply(Reloaded, Accounts, FGFElderTrade::MakeTxId(After, Player, Elder, RowId, 1), Player, Elder, Give, Get, Why) == EGFTxResult::Applied);
	TestEqual(TEXT("both trades paid out"), Accounts.Accounts[Player].GetCount(TEXT("Fiber")), 8);
	Why.Reset();
	TestTrue(TEXT("a resend inside the new session still dedupes"),
		FGFElderTrade::Apply(Reloaded, Accounts, FGFElderTrade::MakeTxId(After, Player, Elder, RowId, 1), Player, Elder, Give, Get, Why) == EGFTxResult::AlreadyApplied);
	TestEqual(TEXT("no third pile"), Accounts.Accounts[Player].GetCount(TEXT("Fiber")), 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderQuestAcceptOnceTest, "GFGame.Elder.Quest.AcceptOnce", GFElderTradeTest::Flags)
bool FGFElderQuestAcceptOnceTest::RunTest(const FString&)
{
	FGFQuestLine Line;
	Line.Progress.Step = EGFQuestStep::TalkToElder;
	const FGFQuestApplyResult First = FGFElderTalk::Accept(Line, TEXT("P"), TEXT("Rime"));
	TestTrue(TEXT("First accept joins"), First.Result == EGFQuestResult::Ok && First.bClaimFaction);
	TestTrue(TEXT("Faction is Rime"), Line.Progress.FactionId == FName(TEXT("Rime")));
	TestTrue(TEXT("Rank is Initiate"), Line.Progress.Rank == EGFFactionRank::Initiate);
	TestTrue(TEXT("The hunt starts"), Line.Progress.Step == EGFQuestStep::HuntHide);

	const FGFQuestProgress After = Line.Progress;
	const FGFQuestApplyResult Second = FGFElderTalk::Accept(Line, TEXT("P"), TEXT("Verdant"));
	TestFalse(TEXT("Second accept does not claim"), Second.bClaimFaction);
	TestTrue(TEXT("Second accept changes nothing"), Line.Progress == After);
	TestTrue(TEXT("Still Rime"), Line.Progress.FactionId == FName(TEXT("Rime")));
	TestTrue(TEXT("Still hunting hide"), Line.Progress.Step == EGFQuestStep::HuntHide);

	const FGFQuestApplyResult SameTown = FGFElderTalk::Accept(Line, TEXT("P"), TEXT("Rime"));
	TestTrue(TEXT("Same town again changes nothing"), Line.Progress == After && !SameTown.bClaimFaction);
	return true;
}

#endif
