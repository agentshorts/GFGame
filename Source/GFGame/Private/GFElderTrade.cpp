#include "GFElderTrade.h"
#include "GFDataTables.h"
#include "Misc/Crc.h"

int32 FGFElderTrade::RoundRatio(int64 GoodsValue, int32 Numerator, int32 Denominator)
{
	if (GoodsValue <= 0 || Numerator <= 0 || Denominator <= 0)
	{
		return 0;
	}
	const int64 Scaled = GoodsValue * int64(Numerator);
	const int64 Den = Denominator;
	const int64 Rounded = (Scaled * 2 + Den) / (Den * 2);
	return Rounded > int64(MAX_int32) ? MAX_int32 : int32(Rounded);
}

int32 FGFElderTrade::Quote(EGFElderTradeKind Kind, int64 GoodsValue)
{
	if (Kind == EGFElderTradeKind::Sell)
	{
		return RoundRatio(GoodsValue, 4, 5);
	}
	return RoundRatio(GoodsValue, 5, 4);
}

FGuid FGFElderTrade::MakeTxId(const FString& PlayerKey, const FString& ElderKey, FName RowId, int32 RequestId)
{
	const FString Seed = FString::Printf(TEXT("%s|%s|%s|%d"), *PlayerKey, *ElderKey, *RowId.ToString(), RequestId);
	const uint32 A = FCrc::StrCrc32(*Seed);
	const uint32 B = FCrc::StrCrc32(*Seed, 0x811C9DC5u);
	const uint32 C = FCrc::StrCrc32(*Seed, 0x01000193u);
	const uint32 D = FCrc::StrCrc32(*Seed, 0x9E3779B9u) | 1u;
	return FGuid(A, B, C, D);
}

FGuid FGFElderTrade::ChildId(const FGuid& TxId, int32 Salt)
{
	const uint32 Mix = uint32(Salt) * 0x9E3779B9u;
	FGuid Out(TxId.A ^ Mix, TxId.B, TxId.C, (TxId.D ^ (uint32(Salt) * 0x85EBCA6Bu)) | 1u);
	if (!Out.IsValid() || Out == TxId)
	{
		Out.D ^= 0xA5A5A5A5u;
	}
	return Out;
}

FString FGFElderTrade::ShortageText(const FGFInventory& Have, const TArray<FGFItemStack>& Need)
{
	TArray<FString> Parts;
	for (const FGFItemStack& Stack : Need)
	{
		const int32 Got = Have.GetCount(Stack.ItemId);
		if (Got < Stack.Count)
		{
			Parts.Add(FString::Printf(TEXT("%s x%d"), *Stack.ItemId.ToString(), Stack.Count - Got));
		}
	}
	if (Parts.Num() == 0)
	{
		return TEXT("Not enough items.");
	}
	return FString::Printf(TEXT("Not enough %s."), *FString::Join(Parts, TEXT(", ")));
}

EGFTxResult FGFElderTrade::Apply(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FGuid& TxId, const FString& PlayerKey, const FString& ElderKey, const TArray<FGFItemStack>& Give, const TArray<FGFItemStack>& Get, FString& OutWhy)
{
	OutWhy.Reset();
	if (!TxId.IsValid() || PlayerKey.IsEmpty() || ElderKey.IsEmpty())
	{
		OutWhy = TEXT("That trade is not valid.");
		return EGFTxResult::InvalidStacks;
	}
	if (PlayerKey == ElderKey)
	{
		OutWhy = TEXT("That trade is not valid.");
		return EGFTxResult::SameAccount;
	}
	TArray<FGFItemStack> GiveNorm;
	TArray<FGFItemStack> GetNorm;
	if (!FGFEconomy::NormalizeStacks(Give, GiveNorm) || !FGFEconomy::NormalizeStacks(Get, GetNorm))
	{
		OutWhy = TEXT("That trade is not valid.");
		return EGFTxResult::InvalidStacks;
	}

	const FGuid MintId = ChildId(TxId, 1);
	const FGuid GetId = ChildId(TxId, 2);
	const bool bGiveDone = Ledger.WasApplied(TxId);
	const bool bGetDone = Ledger.WasApplied(GetId);
	if (bGiveDone && bGetDone)
	{
		return EGFTxResult::AlreadyApplied;
	}

	FGFInventory PlayerInv;
	if (!Accounts.ReadAccount(PlayerKey, PlayerInv))
	{
		OutWhy = TEXT("The ledger does not know this player.");
		return EGFTxResult::UnknownAccount;
	}
	FGFInventory ElderInv;
	if (!Accounts.ReadAccount(ElderKey, ElderInv))
	{
		Accounts.WriteAccount(ElderKey, FGFInventory());
		if (!Accounts.ReadAccount(ElderKey, ElderInv))
		{
			OutWhy = TEXT("The elder account is closed.");
			return EGFTxResult::UnknownAccount;
		}
	}
	if (!bGiveDone && !PlayerInv.HasAll(GiveNorm))
	{
		OutWhy = ShortageText(PlayerInv, GiveNorm);
		return EGFTxResult::InsufficientItems;
	}

	bool bChanged = false;
	if (!bGiveDone)
	{
		TArray<FGFItemStack> Short;
		for (const FGFItemStack& Stack : GetNorm)
		{
			const int32 Have = ElderInv.GetCount(Stack.ItemId);
			if (Have < Stack.Count)
			{
				Short.Add(FGFItemStack(Stack.ItemId, Stack.Count - Have));
			}
		}
		if (Short.Num() > 0 && !Ledger.WasApplied(MintId))
		{
			const EGFTxResult Minted = Ledger.Mint(Accounts, MintId, ElderKey, Short, TEXT("ElderTrade"));
			if (Minted != EGFTxResult::Applied && Minted != EGFTxResult::AlreadyApplied)
			{
				OutWhy = TEXT("The elder could not stock that.");
				return Minted;
			}
			bChanged = bChanged || Minted == EGFTxResult::Applied;
		}
		const EGFTxResult Gave = Ledger.Transfer(Accounts, TxId, PlayerKey, ElderKey, GiveNorm, TEXT("ElderTrade"));
		if (Gave != EGFTxResult::Applied && Gave != EGFTxResult::AlreadyApplied)
		{
			OutWhy = Gave == EGFTxResult::InsufficientItems ? ShortageText(PlayerInv, GiveNorm) : FString(TEXT("The trade did not go through."));
			return Gave;
		}
		bChanged = bChanged || Gave == EGFTxResult::Applied;
	}
	if (!Ledger.WasApplied(GetId))
	{
		const EGFTxResult Got = Ledger.Transfer(Accounts, GetId, ElderKey, PlayerKey, GetNorm, TEXT("ElderTrade"));
		if (Got != EGFTxResult::Applied && Got != EGFTxResult::AlreadyApplied)
		{
			OutWhy = TEXT("The elder could not hand that over.");
			return Got;
		}
		bChanged = bChanged || Got == EGFTxResult::Applied;
	}
	return bChanged ? EGFTxResult::Applied : EGFTxResult::AlreadyApplied;
}

namespace
{
	void AddListing(FGFElderTradeCatalog& Catalog, const TCHAR* RowId, const TCHAR* TownId, EGFElderTradeKind Kind, const FGFItemStack& Give, const FGFItemStack& Get)
	{
		FGFElderTradeListing Row;
		Row.RowId = RowId;
		Row.TownId = TownId;
		Row.Kind = Kind;
		Row.Give = { Give };
		Row.Get = { Get };
		Catalog.Rows.Add(Row);
	}
}

const FGFElderTradeCatalog& FGFElderTradeCatalog::Builtin()
{
	static const FGFElderTradeCatalog Catalog = []()
	{
		FGFElderTradeCatalog Built;
		AddListing(Built, TEXT("Rime_Fiber"), TEXT("Rime"), EGFElderTradeKind::Buy, FGFItemStack(TEXT("Wood"), 5), FGFItemStack(TEXT("Fiber"), 4));
		AddListing(Built, TEXT("Rime_Metal"), TEXT("Rime"), EGFElderTradeKind::Buy, FGFItemStack(TEXT("Wood"), 8), FGFItemStack(TEXT("Metal"), 2));
		AddListing(Built, TEXT("Rime_Hide"), TEXT("Rime"), EGFElderTradeKind::Sell, FGFItemStack(TEXT("Hide"), 2), FGFItemStack(TEXT("Wood"), 5));
		AddListing(Built, TEXT("Verdant_Flowers"), TEXT("Verdant"), EGFElderTradeKind::Buy, FGFItemStack(TEXT("Wood"), 5), FGFItemStack(TEXT("Flowers"), 4));
		AddListing(Built, TEXT("Verdant_Berry"), TEXT("Verdant"), EGFElderTradeKind::Buy, FGFItemStack(TEXT("Wood"), 3), FGFItemStack(TEXT("Berry"), 2));
		AddListing(Built, TEXT("Verdant_Scales"), TEXT("Verdant"), EGFElderTradeKind::Sell, FGFItemStack(TEXT("Scales"), 2), FGFItemStack(TEXT("Wood"), 5));
		AddListing(Built, TEXT("Cinder_Coalnut"), TEXT("Cinderwrights"), EGFElderTradeKind::Buy, FGFItemStack(TEXT("Wood"), 5), FGFItemStack(TEXT("Coalnut"), 4));
		AddListing(Built, TEXT("Cinder_Fabric"), TEXT("Cinderwrights"), EGFElderTradeKind::Buy, FGFItemStack(TEXT("Wood"), 4), FGFItemStack(TEXT("Fabric"), 1));
		AddListing(Built, TEXT("Cinder_Bone"), TEXT("Cinderwrights"), EGFElderTradeKind::Sell, FGFItemStack(TEXT("Bone"), 1), FGFItemStack(TEXT("Wood"), 2));
		return Built;
	}();
	return Catalog;
}

FGFElderTradeCatalog FGFElderTradeCatalog::FromTable(const UDataTable* Table)
{
	FGFElderTradeCatalog Out;
	if (!Table)
	{
		return Builtin();
	}
	Table->ForeachRow<FGFElderTradeRow>(TEXT("GFElderTrade"), [&Out](const FName& RowName, const FGFElderTradeRow& Row)
	{
		if (RowName.IsNone() || Row.TownId.IsNone() || Row.Give.Num() == 0 || Row.Get.Num() == 0)
		{
			return;
		}
		FGFElderTradeListing Listing;
		Listing.RowId = RowName;
		Listing.TownId = Row.TownId;
		Listing.Kind = Row.Kind;
		Listing.Give = Row.Give;
		Listing.Get = Row.Get;
		Out.Rows.Add(Listing);
	});
	return Out.Rows.Num() > 0 ? Out : Builtin();
}

FGFElderTradeCatalog FGFElderTradeCatalog::Load()
{
	return FromTable(FGFDataTables::Get<FGFElderTradeRow>(TEXT("DT_ElderTrades")));
}

const FGFElderTradeListing* FGFElderTradeCatalog::Find(FName RowId) const
{
	for (const FGFElderTradeListing& Row : Rows)
	{
		if (Row.RowId == RowId)
		{
			return &Row;
		}
	}
	return nullptr;
}

TArray<const FGFElderTradeListing*> FGFElderTradeCatalog::ForTown(FName TownId) const
{
	TArray<const FGFElderTradeListing*> Out;
	for (const FGFElderTradeListing& Row : Rows)
	{
		if (Row.TownId == TownId)
		{
			Out.Add(&Row);
		}
	}
	return Out;
}
