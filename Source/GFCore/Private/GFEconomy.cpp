#include "GFEconomy.h"
#include "Engine/DataTable.h"

// ---------------- FGFEconomy ----------------

int32 FGFEconomy::DefaultValueForTier(int32 Tier, EGFExclusive Exclusive)
{
	if (Exclusive == EGFExclusive::HallowNight)
	{
		return 25;
	}
	if (Exclusive == EGFExclusive::Lab)
	{
		return Tier >= 4 ? 20 : 10;
	}
	switch (Tier)
	{
	case 1: return 1;
	case 2: return 3;
	case 3: return 8;
	case 4: return 20;
	default: return 0;
	}
}

const FGFItemRow* FGFEconomy::FindItem(const UDataTable* Items, FName ItemId)
{
	return Items ? Items->FindRow<FGFItemRow>(ItemId, TEXT("GFEconomy"), /*bWarnIfRowMissing*/ false) : nullptr;
}

int32 FGFEconomy::UnitValue(const UDataTable* Items, FName ItemId)
{
	const FGFItemRow* Row = FindItem(Items, ItemId);
	return Row ? Row->Value : 0;
}

int64 FGFEconomy::StacksValue(const UDataTable* Items, const TArray<FGFItemStack>& Stacks)
{
	int64 Total = 0;
	for (const FGFItemStack& S : Stacks)
	{
		Total += int64(UnitValue(Items, S.ItemId)) * FMath::Max(0, S.Count);
	}
	return Total;
}

int64 FGFEconomy::InventoryValue(const UDataTable* Items, const FGFInventory& Inventory)
{
	return StacksValue(Items, Inventory.Items);
}

bool FGFEconomy::IsWagerable(const UDataTable* Items, FName ItemId)
{
	const FGFItemRow* Row = FindItem(Items, ItemId);
	return Row && Row->bWagerable;
}

bool FGFEconomy::IsRentable(const UDataTable* Items, FName ItemId)
{
	const FGFItemRow* Row = FindItem(Items, ItemId);
	return Row && Row->bRentable;
}

int32 FGFEconomy::ValueBand(int64 Value)
{
	if (Value <= 0) return 0;
	if (Value <= 15) return 1;
	if (Value <= 60) return 2;
	if (Value <= 150) return 3;
	return 4;
}

bool FGFEconomy::NormalizeStacks(const TArray<FGFItemStack>& In, TArray<FGFItemStack>& Out)
{
	Out.Reset();
	for (const FGFItemStack& S : In)
	{
		if (S.ItemId.IsNone() || S.Count <= 0)
		{
			Out.Reset();
			return false;
		}
		if (FGFItemStack* Existing = Out.FindByPredicate([&](const FGFItemStack& E) { return E.ItemId == S.ItemId; }))
		{
			Existing->Count += S.Count;
		}
		else
		{
			Out.Add(S);
		}
	}
	return Out.Num() > 0;
}

TArray<FString> FGFEconomy::ValidateItemTable(const UDataTable* Items)
{
	TArray<FString> Problems;
	if (!Items)
	{
		Problems.Add(TEXT("no item table"));
		return Problems;
	}
	Items->ForeachRow<FGFItemRow>(TEXT("GFEconomy"), [&](const FName& Id, const FGFItemRow& Row)
	{
		const bool bTradeable = Row.Category == EGFItemCategory::Material || Row.Category == EGFItemCategory::Treat
			|| Row.Category == EGFItemCategory::Seed || Row.Category == EGFItemCategory::Block;
		if (Row.Tier < 0 || Row.Tier > 4)
		{
			Problems.Add(FString::Printf(TEXT("%s: tier %d out of range"), *Id.ToString(), Row.Tier));
		}
		if (bTradeable && Row.Tier == 0)
		{
			Problems.Add(FString::Printf(TEXT("%s: tradeable item without a tier"), *Id.ToString()));
		}
		if (Row.Tier > 0 && Row.Value != DefaultValueForTier(Row.Tier, Row.Exclusive))
		{
			Problems.Add(FString::Printf(TEXT("%s: value %d, tier %d expects %d"), *Id.ToString(), Row.Value, Row.Tier, DefaultValueForTier(Row.Tier, Row.Exclusive)));
		}
		if (Row.Tier > 0 && Row.MaterialCategory == EGFMaterialCategory::None)
		{
			Problems.Add(FString::Printf(TEXT("%s: no material category"), *Id.ToString()));
		}
		if (!bTradeable && (Row.bWagerable || Row.bRentable))
		{
			Problems.Add(FString::Printf(TEXT("%s: furniture/outfits can't be wagered or rented"), *Id.ToString()));
		}
		if (Row.Exclusive != EGFExclusive::None && Row.bRentable)
		{
			Problems.Add(FString::Printf(TEXT("%s: exclusives aren't rent"), *Id.ToString()));
		}
	});
	return Problems;
}

// ---------------- Accounts ----------------

bool FGFMapLedgerAccounts::ReadAccount(const FString& Account, FGFInventory& Out) const
{
	if (const FGFInventory* Found = Accounts.Find(Account))
	{
		Out = *Found;
		return true;
	}
	return false;
}

void FGFMapLedgerAccounts::WriteAccount(const FString& Account, const FGFInventory& In)
{
	Accounts.FindOrAdd(Account) = In;
}

TArray<FGFItemStack> FGFEscrow::AllStacks() const
{
	TArray<FGFItemStack> All;
	for (const FGFEscrowDeposit& D : Deposits)
	{
		All.Append(D.Stacks);
	}
	TArray<FGFItemStack> Merged;
	FGFEconomy::NormalizeStacks(All, Merged);
	return Merged;
}

// ---------------- Ledger ----------------

static void AddStacks(FGFInventory& Inv, const TArray<FGFItemStack>& Stacks)
{
	for (const FGFItemStack& S : Stacks)
	{
		Inv.Add(S.ItemId, S.Count);
	}
}

EGFTxResult FGFTransferLedger::Transfer(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FString& From, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	if (WasApplied(TxId))
	{
		return EGFTxResult::AlreadyApplied;
	}
	TArray<FGFItemStack> Norm;
	if (!TxId.IsValid() || !FGFEconomy::NormalizeStacks(Stacks, Norm))
	{
		return EGFTxResult::InvalidStacks;
	}
	if (From == To)
	{
		return EGFTxResult::SameAccount;
	}
	FGFInventory FromInv, ToInv;
	if (!Accounts.ReadAccount(From, FromInv) || !Accounts.ReadAccount(To, ToInv))
	{
		return EGFTxResult::UnknownAccount;
	}
	if (!FromInv.RemoveAll(Norm))
	{
		return EGFTxResult::InsufficientItems;
	}
	AddStacks(ToInv, Norm);
	Accounts.WriteAccount(From, FromInv);
	Accounts.WriteAccount(To, ToInv);
	MarkApplied(TxId);
	Log(TxId, EGFTxKind::Transfer, From, To, Norm, Reason);
	return EGFTxResult::Applied;
}

EGFTxResult FGFTransferLedger::Mint(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	if (WasApplied(TxId))
	{
		return EGFTxResult::AlreadyApplied;
	}
	TArray<FGFItemStack> Norm;
	if (!TxId.IsValid() || !FGFEconomy::NormalizeStacks(Stacks, Norm))
	{
		return EGFTxResult::InvalidStacks;
	}
	FGFInventory ToInv;
	if (!Accounts.ReadAccount(To, ToInv))
	{
		return EGFTxResult::UnknownAccount;
	}
	AddStacks(ToInv, Norm);
	Accounts.WriteAccount(To, ToInv);
	for (const FGFItemStack& S : Norm)
	{
		Minted.FindOrAdd(S.ItemId) += S.Count;
	}
	MarkApplied(TxId);
	Log(TxId, EGFTxKind::Mint, FString(), To, Norm, Reason);
	return EGFTxResult::Applied;
}

EGFTxResult FGFTransferLedger::Burn(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FString& From, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	if (WasApplied(TxId))
	{
		return EGFTxResult::AlreadyApplied;
	}
	TArray<FGFItemStack> Norm;
	if (!TxId.IsValid() || !FGFEconomy::NormalizeStacks(Stacks, Norm))
	{
		return EGFTxResult::InvalidStacks;
	}
	FGFInventory FromInv;
	if (!Accounts.ReadAccount(From, FromInv))
	{
		return EGFTxResult::UnknownAccount;
	}
	if (!FromInv.RemoveAll(Norm))
	{
		return EGFTxResult::InsufficientItems;
	}
	Accounts.WriteAccount(From, FromInv);
	for (const FGFItemStack& S : Norm)
	{
		Burned.FindOrAdd(S.ItemId) += S.Count;
	}
	MarkApplied(TxId);
	Log(TxId, EGFTxKind::Burn, From, FString(), Norm, Reason);
	return EGFTxResult::Applied;
}

FGFEscrow& FGFTransferLedger::OpenEscrow(const FGuid& EscrowId, FName Kind)
{
	if (FGFEscrow* Found = FindEscrowMutable(EscrowId))
	{
		return *Found;
	}
	FGFEscrow& E = Escrows.AddDefaulted_GetRef();
	E.Id = EscrowId;
	E.Kind = Kind;
	return E;
}

EGFTxResult FGFTransferLedger::Deposit(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FGuid& EscrowId, const FString& From, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	if (WasApplied(TxId))
	{
		return EGFTxResult::AlreadyApplied;
	}
	TArray<FGFItemStack> Norm;
	if (!TxId.IsValid() || !FGFEconomy::NormalizeStacks(Stacks, Norm))
	{
		return EGFTxResult::InvalidStacks;
	}
	FGFEscrow* E = FindEscrowMutable(EscrowId);
	if (!E)
	{
		return EGFTxResult::UnknownEscrow;
	}
	if (!E->bOpen)
	{
		return EGFTxResult::EscrowClosed;
	}
	FGFInventory FromInv;
	if (!Accounts.ReadAccount(From, FromInv))
	{
		return EGFTxResult::UnknownAccount;
	}
	if (!FromInv.RemoveAll(Norm))
	{
		return EGFTxResult::InsufficientItems;
	}
	Accounts.WriteAccount(From, FromInv);
	FGFEscrowDeposit* Dep = E->Deposits.FindByPredicate([&](const FGFEscrowDeposit& D) { return D.Account == From; });
	if (!Dep)
	{
		Dep = &E->Deposits.AddDefaulted_GetRef();
		Dep->Account = From;
	}
	Dep->Stacks.Append(Norm);
	TArray<FGFItemStack> Merged;
	FGFEconomy::NormalizeStacks(Dep->Stacks, Merged);
	Dep->Stacks = MoveTemp(Merged);
	MarkApplied(TxId);
	Log(TxId, EGFTxKind::EscrowDeposit, From, EscrowId.ToString(), Norm, Reason);
	return EGFTxResult::Applied;
}

EGFTxResult FGFTransferLedger::Release(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FGuid& EscrowId, const FString& To, FName Reason)
{
	if (WasApplied(TxId))
	{
		return EGFTxResult::AlreadyApplied;
	}
	FGFEscrow* E = FindEscrowMutable(EscrowId);
	if (!TxId.IsValid())
	{
		return EGFTxResult::InvalidStacks;
	}
	if (!E)
	{
		return EGFTxResult::UnknownEscrow;
	}
	if (!E->bOpen)
	{
		return EGFTxResult::EscrowClosed;
	}
	FGFInventory ToInv;
	if (!Accounts.ReadAccount(To, ToInv))
	{
		return EGFTxResult::UnknownAccount;
	}
	const TArray<FGFItemStack> All = E->AllStacks();
	AddStacks(ToInv, All);
	Accounts.WriteAccount(To, ToInv);
	E->bOpen = false;
	E->Deposits.Reset();
	MarkApplied(TxId);
	Log(TxId, EGFTxKind::EscrowRelease, EscrowId.ToString(), To, All, Reason);
	return EGFTxResult::Applied;
}

EGFTxResult FGFTransferLedger::Refund(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FGuid& EscrowId, FName Reason)
{
	if (WasApplied(TxId))
	{
		return EGFTxResult::AlreadyApplied;
	}
	FGFEscrow* E = FindEscrowMutable(EscrowId);
	if (!TxId.IsValid())
	{
		return EGFTxResult::InvalidStacks;
	}
	if (!E)
	{
		return EGFTxResult::UnknownEscrow;
	}
	if (!E->bOpen)
	{
		return EGFTxResult::EscrowClosed;
	}
	// All or nothing: every depositor must be reachable before anything moves.
	TArray<FGFInventory> Invs;
	Invs.SetNum(E->Deposits.Num());
	for (int32 i = 0; i < E->Deposits.Num(); ++i)
	{
		if (!Accounts.ReadAccount(E->Deposits[i].Account, Invs[i]))
		{
			return EGFTxResult::UnknownAccount;
		}
	}
	for (int32 i = 0; i < E->Deposits.Num(); ++i)
	{
		AddStacks(Invs[i], E->Deposits[i].Stacks);
		Accounts.WriteAccount(E->Deposits[i].Account, Invs[i]);
		Log(TxId, EGFTxKind::EscrowRefund, EscrowId.ToString(), E->Deposits[i].Account, E->Deposits[i].Stacks, Reason);
	}
	E->bOpen = false;
	E->Deposits.Reset();
	MarkApplied(TxId);
	return EGFTxResult::Applied;
}

EGFTxResult FGFTransferLedger::Forfeit(const FGuid& TxId, const FGuid& EscrowId, FName Reason)
{
	if (WasApplied(TxId))
	{
		return EGFTxResult::AlreadyApplied;
	}
	if (!TxId.IsValid())
	{
		return EGFTxResult::InvalidStacks;
	}
	FGFEscrow* E = FindEscrowMutable(EscrowId);
	if (!E)
	{
		return EGFTxResult::UnknownEscrow;
	}
	if (!E->bOpen)
	{
		return EGFTxResult::EscrowClosed;
	}
	const TArray<FGFItemStack> All = E->AllStacks();
	for (const FGFItemStack& S : All)
	{
		Burned.FindOrAdd(S.ItemId) += S.Count;
	}
	E->bOpen = false;
	E->Deposits.Reset();
	MarkApplied(TxId);
	Log(TxId, EGFTxKind::Burn, EscrowId.ToString(), FString(), All, Reason);
	return EGFTxResult::Applied;
}

int32 FGFTransferLedger::RefundOpen(IGFLedgerAccounts& Accounts, FName Kind)
{
	TArray<FGuid> Ids;
	for (const FGFEscrow& E : Escrows)
	{
		if (E.bOpen && E.Kind == Kind)
		{
			Ids.Add(E.Id);
		}
	}
	int32 Done = 0;
	for (const FGuid& Id : Ids)
	{
		Done += Refund(Accounts, FGuid::NewGuid(), Id, TEXT("RefundOpen")) == EGFTxResult::Applied ? 1 : 0;
	}
	return Done;
}

const FGFEscrow* FGFTransferLedger::FindEscrow(const FGuid& EscrowId) const
{
	return Escrows.FindByPredicate([&](const FGFEscrow& E) { return E.Id == EscrowId; });
}

FGFEscrow* FGFTransferLedger::FindEscrowMutable(const FGuid& EscrowId)
{
	return Escrows.FindByPredicate([&](const FGFEscrow& E) { return E.Id == EscrowId; });
}

int64 FGFTransferLedger::HeldInEscrow(FName ItemId) const
{
	int64 Total = 0;
	for (const FGFEscrow& E : Escrows)
	{
		if (!E.bOpen)
		{
			continue;
		}
		for (const FGFEscrowDeposit& D : E.Deposits)
		{
			for (const FGFItemStack& S : D.Stacks)
			{
				Total += S.ItemId == ItemId ? S.Count : 0;
			}
		}
	}
	return Total;
}

int64 FGFTransferLedger::GetMinted(FName ItemId) const
{
	const int64* V = Minted.Find(ItemId);
	return V ? *V : 0;
}

int64 FGFTransferLedger::GetBurned(FName ItemId) const
{
	const int64* V = Burned.Find(ItemId);
	return V ? *V : 0;
}

bool FGFTransferLedger::WasApplied(const FGuid& TxId) const
{
	if (Applied.Num() != AppliedOrder.Num())
	{
		const_cast<FGFTransferLedger*>(this)->RebuildIndex();
	}
	return Applied.Contains(TxId);
}

void FGFTransferLedger::PruneClosedEscrows()
{
	Escrows.RemoveAll([](const FGFEscrow& E) { return !E.bOpen; });
}

void FGFTransferLedger::RebuildIndex()
{
	Applied.Reset();
	Applied.Append(AppliedOrder);
}

void FGFTransferLedger::MarkApplied(const FGuid& TxId)
{
	Applied.Add(TxId);
	AppliedOrder.Add(TxId);
	if (AppliedOrder.Num() > MaxRememberedTx)
	{
		const int32 Drop = AppliedOrder.Num() - MaxRememberedTx;
		for (int32 i = 0; i < Drop; ++i)
		{
			Applied.Remove(AppliedOrder[i]);
		}
		AppliedOrder.RemoveAt(0, Drop);
	}
}

void FGFTransferLedger::Log(const FGuid& TxId, EGFTxKind Kind, const FString& From, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	FGFLedgerEntry& E = Recent.AddDefaulted_GetRef();
	E.TxId = TxId;
	E.Kind = Kind;
	E.From = From;
	E.To = To;
	E.Stacks = Stacks;
	E.Reason = Reason;
	E.TimeUtc = FDateTime::UtcNow();
	if (Recent.Num() > MaxRecentEntries)
	{
		Recent.RemoveAt(0, Recent.Num() - MaxRecentEntries);
	}
}
