// Material economy (PLAN.md section 2): value tiers and the transfer ledger's no-duplication guarantees.
#include "Misc/AutomationTest.h"
#include "GFEconomy.h"
#include "GFDataTables.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFEconTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFEconTest
{
	static const FName Items[] = { TEXT("Wood"), TEXT("Stone"), TEXT("Amber"), TEXT("Fossil") };

	static int64 Sum(const FGFMapLedgerAccounts& A, FName Item)
	{
		int64 T = 0;
		for (const TPair<FString, FGFInventory>& P : A.Accounts)
		{
			T += P.Value.GetCount(Item);
		}
		return T;
	}

	static uint32 StateHash(const FGFMapLedgerAccounts& A, const FGFTransferLedger& L)
	{
		uint32 H = 0;
		TArray<FString> Keys;
		A.Accounts.GetKeys(Keys);
		Keys.Sort();
		for (const FString& K : Keys)
		{
			H = HashCombine(H, HashCombine(GetTypeHash(K), A.Accounts[K].GetContentHash()));
		}
		for (const FGFEscrow& E : L.Escrows)
		{
			H = HashCombine(H, GetTypeHash(E.bOpen));
			for (const FGFItemStack& S : E.AllStacks())
			{
				H = HashCombine(H, HashCombine(GetTypeHash(S.ItemId), GetTypeHash(S.Count)));
			}
		}
		return H;
	}

	static TArray<FGFItemStack> RandomStacks(FRandomStream& Rng)
	{
		TArray<FGFItemStack> S;
		const int32 N = Rng.RandRange(1, 3);
		for (int32 i = 0; i < N; ++i)
		{
			S.Emplace(Items[Rng.RandRange(0, 3)], Rng.RandRange(1, 12)); // duplicates on purpose
		}
		return S;
	}
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEconomyTiersTest, "GFGame.Economy.Tiers", GFEconTestFlags)
bool FGFEconomyTiersTest::RunTest(const FString&)
{
	const UDataTable* Table = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (!TestNotNull(TEXT("DT_Items loads"), Table))
	{
		return false;
	}
	for (const FString& P : FGFEconomy::ValidateItemTable(Table))
	{
		AddError(P);
	}
	struct FCase { const TCHAR* Id; int32 Tier; int32 Value; };
	for (const FCase& C : { FCase{ TEXT("Wood"), 1, 1 }, FCase{ TEXT("Scales"), 2, 3 }, FCase{ TEXT("Amber"), 3, 8 }, FCase{ TEXT("Fossil"), 4, 20 },
		FCase{ TEXT("Moonstone"), 4, 25 }, FCase{ TEXT("PolymerGel"), 3, 10 }, FCase{ TEXT("AnomalyShard"), 4, 20 } })
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Table, C.Id);
		if (TestNotNull(FString::Printf(TEXT("%s exists"), C.Id), Row))
		{
			TestEqual(FString::Printf(TEXT("%s tier"), C.Id), Row->Tier, C.Tier);
			TestEqual(FString::Printf(TEXT("%s value"), C.Id), Row->Value, C.Value);
		}
	}
	TestTrue(TEXT("Moonstone is HallowNight-exclusive"), FGFEconomy::FindItem(Table, TEXT("Moonstone"))->Exclusive == EGFExclusive::HallowNight);
	TestFalse(TEXT("Exclusives aren't rent"), FGFEconomy::IsRentable(Table, TEXT("Moonstone")));
	TestTrue(TEXT("Wood is rentable"), FGFEconomy::IsRentable(Table, TEXT("Wood")));
	TestFalse(TEXT("Furniture can't be wagered"), FGFEconomy::IsWagerable(Table, TEXT("Chair")));
	TestFalse(TEXT("Unknown items can't be wagered"), FGFEconomy::IsWagerable(Table, TEXT("NoSuchThing")));
	TestEqual(TEXT("Stack value"), FGFEconomy::StacksValue(Table, { FGFItemStack(TEXT("Wood"), 10), FGFItemStack(TEXT("Amber"), 2), FGFItemStack(TEXT("Moonstone"), 1) }), int64(10 + 16 + 25));
	TestEqual(TEXT("Band none"), FGFEconomy::ValueBand(0), 0);
	TestEqual(TEXT("Band small"), FGFEconomy::ValueBand(15), 1);
	TestEqual(TEXT("Band medium"), FGFEconomy::ValueBand(16), 2);
	TestEqual(TEXT("Band big"), FGFEconomy::ValueBand(150), 3);
	TestEqual(TEXT("Band huge"), FGFEconomy::ValueBand(151), 4);
	// No currency anywhere (PLAN.md section 2): no item may be called a coin, gem or money.
	Table->ForeachRow<FGFItemRow>(TEXT("Test"), [this](const FName& Id, const FGFItemRow&)
	{
		const FString S = Id.ToString();
		TestFalse(FString::Printf(TEXT("%s is not a currency"), *S), S.Contains(TEXT("Coin")) || S.Contains(TEXT("Gem")) || S.Contains(TEXT("Money")));
	});
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEconomyLedgerTest, "GFGame.Economy.Ledger", GFEconTestFlags)
bool FGFEconomyLedgerTest::RunTest(const FString&)
{
	using namespace GFEconTest;
	FRandomStream Rng(1234);
	FGFMapLedgerAccounts A;
	FGFTransferLedger L;
	const TArray<FString> Keys = { TEXT("Owner"), TEXT("Friend1"), TEXT("Friend2"), TEXT("Friend3") };
	TMap<FName, int64> Start;
	for (const FString& K : Keys)
	{
		FGFInventory& Inv = A.Accounts.Add(K);
		for (FName It : Items)
		{
			const int32 N = Rng.RandRange(0, 60);
			Inv.Add(It, N);
			Start.FindOrAdd(It) += N;
		}
	}

	struct FOp { FGuid TxId; TFunction<EGFTxResult(const FGuid&)> Run; EGFTxResult First = EGFTxResult::InvalidStacks; };
	TArray<FOp> History;
	TArray<FGuid> EscrowIds;
	int32 Applied = 0, Replays = 0, Refused = 0;
	bool bOk = true;

	for (int32 Step = 0; Step < 1000 && bOk; ++Step)
	{
		const int32 Roll = Rng.RandRange(0, 99);
		if (Roll < 15 && History.Num() > 0)
		{
			// Replay an earlier operation with its original TxId.
			const FOp& Old = History[Rng.RandRange(0, History.Num() - 1)];
			const uint32 Before = StateHash(A, L);
			const EGFTxResult R = Old.Run(Old.TxId);
			++Replays;
			if (Old.First == EGFTxResult::Applied)
			{
				bOk &= TestTrue(FString::Printf(TEXT("Step %d: replay of an applied tx is AlreadyApplied"), Step), R == EGFTxResult::AlreadyApplied);
				bOk &= TestEqual(FString::Printf(TEXT("Step %d: replay changes nothing"), Step), StateHash(A, L), Before);
			}
		}
		else
		{
			FOp Op;
			Op.TxId = FGuid::NewGuid();
			const FString From = Keys[Rng.RandRange(0, 3)];
			const FString To = Rng.RandRange(0, 19) == 0 ? From : Keys[Rng.RandRange(0, 3)]; // sometimes the same account
			TArray<FGFItemStack> Stacks = RandomStacks(Rng);
			if (Rng.RandRange(0, 29) == 0)
			{
				Stacks.Emplace(Items[0], Rng.RandRange(-3, 0)); // invalid count
			}
			if (Roll < 60)
			{
				const FString Target = Rng.RandRange(0, 39) == 0 ? FString(TEXT("Stranger")) : To;
				Op.Run = [&A, &L, From, Target, Stacks](const FGuid& Id) { return L.Transfer(A, Id, From, Target, Stacks, TEXT("Gift")); };
			}
			else if (Roll < 72)
			{
				if (EscrowIds.Num() == 0 || Rng.RandRange(0, 3) == 0)
				{
					EscrowIds.Add(FGuid::NewGuid());
					L.OpenEscrow(EscrowIds.Last(), TEXT("Wager"));
				}
				const FGuid E = EscrowIds[Rng.RandRange(0, EscrowIds.Num() - 1)];
				Op.Run = [&A, &L, From, Stacks, E](const FGuid& Id) { return L.Deposit(A, Id, E, From, Stacks, TEXT("Stake")); };
			}
			else if (Roll < 80 && EscrowIds.Num() > 0)
			{
				const FGuid E = EscrowIds[Rng.RandRange(0, EscrowIds.Num() - 1)];
				if (Rng.RandBool())
				{
					Op.Run = [&A, &L, To, E](const FGuid& Id) { return L.Release(A, Id, E, To, TEXT("Win")); };
				}
				else
				{
					Op.Run = [&A, &L, E](const FGuid& Id) { return L.Refund(A, Id, E, TEXT("Draw")); };
				}
			}
			else if (Roll < 90)
			{
				Op.Run = [&A, &L, To, Stacks](const FGuid& Id) { return L.Mint(A, Id, To, Stacks, TEXT("Reward")); };
			}
			else
			{
				Op.Run = [&A, &L, From, Stacks](const FGuid& Id) { return L.Burn(A, Id, From, Stacks, TEXT("Craft")); };
			}
			const uint32 Before = StateHash(A, L);
			Op.First = Op.Run(Op.TxId);
			if (Op.First == EGFTxResult::Applied)
			{
				++Applied;
			}
			else
			{
				++Refused;
				// Refusals are all-or-nothing (opening a new escrow above is the only allowed side effect, and it's empty).
				bOk &= TestEqual(FString::Printf(TEXT("Step %d: refused op (%s) changes nothing"), Step, *UEnum::GetValueAsString(Op.First)), StateHash(A, L), Before);
			}
			History.Add(MoveTemp(Op));
		}
		// Conservation after every step.
		for (FName It : Items)
		{
			const int64 Have = Sum(A, It) + L.HeldInEscrow(It);
			const int64 Want = Start[It] + L.GetMinted(It) - L.GetBurned(It);
			if (Have != Want)
			{
				AddError(FString::Printf(TEXT("Step %d: %s not conserved: have %lld, want %lld"), Step, *It.ToString(), Have, Want));
				bOk = false;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("Ledger: %d applied, %d refused, %d replays"), Applied, Refused, Replays));
	TestTrue(TEXT("Plenty of transfers actually applied"), Applied > 400);
	TestTrue(TEXT("Some ops were refused (insufficient items, invalid, unknown)"), Refused > 20);

	// Refund every open wager (host restart): stakes go home and nothing is left held.
	L.RefundOpen(A, TEXT("Wager"));
	for (FName It : Items)
	{
		TestEqual(FString::Printf(TEXT("%s: nothing left in escrow"), *It.ToString()), L.HeldInEscrow(It), int64(0));
		TestEqual(FString::Printf(TEXT("%s conserved after refunds"), *It.ToString()), Sum(A, It), Start[It] + L.GetMinted(It) - L.GetBurned(It));
	}

	// The ledger survives a save round-trip and still refuses old TxIds.
	FGFTransferLedger Copy;
	Copy.AppliedOrder = L.AppliedOrder;
	Copy.Escrows = L.Escrows;
	Copy.RebuildIndex();
	TestTrue(TEXT("Restored ledger remembers applied TxIds"), Copy.WasApplied(L.AppliedOrder.Last()));
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEconomyEscrowTest, "GFGame.Economy.Escrow", GFEconTestFlags)
bool FGFEconomyEscrowTest::RunTest(const FString&)
{
	auto Fresh = [](FGFMapLedgerAccounts& A)
	{
		A.Accounts.Reset();
		A.Accounts.Add(TEXT("Owner")).Add(TEXT("Amber"), 10);
		A.Accounts.Add(TEXT("Friend")).Add(TEXT("Wood"), 30);
	};
	const FGuid Wager = FGuid::NewGuid();
	auto Stake = [&](FGFMapLedgerAccounts& A, FGFTransferLedger& L)
	{
		L.OpenEscrow(Wager, TEXT("Wager"));
		TestTrue(TEXT("Owner's stake"), L.Deposit(A, FGuid::NewGuid(), Wager, TEXT("Owner"), { FGFItemStack(TEXT("Amber"), 3) }) == EGFTxResult::Applied);
		TestTrue(TEXT("Friend stake"), L.Deposit(A, FGuid::NewGuid(), Wager, TEXT("Friend"), { FGFItemStack(TEXT("Wood"), 24) }) == EGFTxResult::Applied);
		TestTrue(TEXT("Can't stake more than you have"), L.Deposit(A, FGuid::NewGuid(), Wager, TEXT("Friend"), { FGFItemStack(TEXT("Wood"), 7) }) == EGFTxResult::InsufficientItems);
		TestEqual(TEXT("Stakes held"), L.HeldInEscrow(TEXT("Wood")), int64(24));
	};
	{
		FGFMapLedgerAccounts A; Fresh(A); FGFTransferLedger L; Stake(A, L);
		const FGuid Tx = FGuid::NewGuid();
		TestTrue(TEXT("Win pays out"), L.Release(A, Tx, Wager, TEXT("Owner")) == EGFTxResult::Applied);
		TestTrue(TEXT("Paying twice is a no-op"), L.Release(A, Tx, Wager, TEXT("Owner")) == EGFTxResult::AlreadyApplied);
		TestTrue(TEXT("Closed escrow can't pay again"), L.Release(A, FGuid::NewGuid(), Wager, TEXT("Friend")) == EGFTxResult::EscrowClosed);
		TestTrue(TEXT("Closed escrow can't be refunded"), L.Refund(A, FGuid::NewGuid(), Wager) == EGFTxResult::EscrowClosed);
		TestEqual(TEXT("Winner keeps their amber"), A.Accounts[TEXT("Owner")].GetCount(TEXT("Amber")), 10);
		TestEqual(TEXT("Winner gets the wood"), A.Accounts[TEXT("Owner")].GetCount(TEXT("Wood")), 24);
		TestEqual(TEXT("Loser keeps the rest"), A.Accounts[TEXT("Friend")].GetCount(TEXT("Wood")), 6);
	}
	{
		FGFMapLedgerAccounts A; Fresh(A); FGFTransferLedger L; Stake(A, L);
		TestTrue(TEXT("Draw refunds"), L.Refund(A, FGuid::NewGuid(), Wager) == EGFTxResult::Applied);
		TestEqual(TEXT("Draw: their amber back"), A.Accounts[TEXT("Owner")].GetCount(TEXT("Amber")), 10);
		TestEqual(TEXT("Draw: his wood back"), A.Accounts[TEXT("Friend")].GetCount(TEXT("Wood")), 30);
	}
	{
		// Disconnect mid-wager: the host's world reloads; RefundOpen returns both stakes. Mail is left alone.
		FGFMapLedgerAccounts A; Fresh(A); FGFTransferLedger L; Stake(A, L);
		const FGuid Letter = FGuid::NewGuid();
		L.OpenEscrow(Letter, TEXT("Mail"));
		TestTrue(TEXT("Letter filled"), L.Deposit(A, FGuid::NewGuid(), Letter, TEXT("Friend"), { FGFItemStack(TEXT("Wood"), 2) }) == EGFTxResult::Applied);
		TestEqual(TEXT("One wager refunded"), L.RefundOpen(A, TEXT("Wager")), 1);
		TestEqual(TEXT("Owner's amber back"), A.Accounts[TEXT("Owner")].GetCount(TEXT("Amber")), 10);
		TestEqual(TEXT("His wood back, minus the letter"), A.Accounts[TEXT("Friend")].GetCount(TEXT("Wood")), 28);
		TestTrue(TEXT("Letter still open"), L.FindEscrow(Letter)->bOpen);
		TestTrue(TEXT("Claiming the letter"), L.Release(A, FGuid::NewGuid(), Letter, TEXT("Owner")) == EGFTxResult::Applied);
		TestEqual(TEXT("Claimed"), A.Accounts[TEXT("Owner")].GetCount(TEXT("Wood")), 2);
		L.PruneClosedEscrows();
		TestEqual(TEXT("Closed escrows pruned"), L.Escrows.Num(), 0);
	}
	{
		// A refund needs every depositor reachable, else nothing moves.
		FGFMapLedgerAccounts A; Fresh(A); FGFTransferLedger L; Stake(A, L);
		A.Accounts.Remove(TEXT("Friend"));
		TestTrue(TEXT("Refund with a missing account refuses"), L.Refund(A, FGuid::NewGuid(), Wager) == EGFTxResult::UnknownAccount);
		TestEqual(TEXT("Owner's stake still held"), L.HeldInEscrow(TEXT("Amber")), int64(3));
	}
	return true;
}

#endif
