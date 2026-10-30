#pragma once

// Material economy (PLAN.md section 2): value tiers and an idempotent, conserving transfer ledger.
// Pure C++ (no world), so every rule can be unit-tested; the server wraps it in AGFGameState.
#include "CoreMinimal.h"
#include "GFItemTypes.h"
#include "GFEconomy.generated.h"

class UDataTable;

/** Value tiers and item lookups. Values come from DT_Items; these are the defaults the table must follow. */
struct GFCORE_API FGFEconomy
{
	/** Tier value from the plan: 1 / 3 / 8 / 20; HallowNight exclusives 25; Lab exclusives 10 (tier 3) or 20 (tier 4). */
	static int32 DefaultValueForTier(int32 Tier, EGFExclusive Exclusive = EGFExclusive::None);
	static const FGFItemRow* FindItem(const UDataTable* Items, FName ItemId);
	static int32 UnitValue(const UDataTable* Items, FName ItemId);
	static int64 StacksValue(const UDataTable* Items, const TArray<FGFItemStack>& Stacks);
	static int64 InventoryValue(const UDataTable* Items, const FGFInventory& Inventory);
	static bool IsWagerable(const UDataTable* Items, FName ItemId);
	static bool IsRentable(const UDataTable* Items, FName ItemId);
	/** Wager value bands (PLAN.md M6): 0 none, 1 Small 1-15, 2 Medium 16-60, 3 Big 61-150, 4 Huge 151+. */
	static int32 ValueBand(int64 Value);
	/** Merges duplicate ids. False if any stack has no id or a count <= 0, or the list is empty. */
	static bool NormalizeStacks(const TArray<FGFItemStack>& In, TArray<FGFItemStack>& Out);
	/** Checks every row against the tier table. Returns the problems (empty = fine). */
	static TArray<FString> ValidateItemTable(const UDataTable* Items);
};

UENUM(BlueprintType)
enum class EGFTxResult : uint8
{
	Applied,
	/** This TxId was applied before: nothing changed (safe replay). */
	AlreadyApplied,
	InsufficientItems,
	InvalidStacks,
	UnknownAccount,
	SameAccount,
	UnknownEscrow,
	EscrowClosed
};

UENUM(BlueprintType)
enum class EGFTxKind : uint8
{
	Transfer,
	Mint,
	Burn,
	EscrowDeposit,
	EscrowRelease,
	EscrowRefund
};

/** Who put what into an escrow. */
USTRUCT()
struct GFCORE_API FGFEscrowDeposit
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FString Account;
	UPROPERTY(SaveGame) TArray<FGFItemStack> Stacks;
};

/** Items held by the server between two steps: a wager's stakes, or a letter's contents until it is claimed. */
USTRUCT()
struct GFCORE_API FGFEscrow
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FGuid Id;
	/** "Wager", "Mail", ... Wagers are refunded when a world loads; mail waits to be claimed. */
	UPROPERTY(SaveGame) FName Kind;
	UPROPERTY(SaveGame) bool bOpen = true;
	UPROPERTY(SaveGame) TArray<FGFEscrowDeposit> Deposits;

	TArray<FGFItemStack> AllStacks() const;
};

USTRUCT()
struct GFCORE_API FGFLedgerEntry
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FGuid TxId;
	UPROPERTY(SaveGame) EGFTxKind Kind = EGFTxKind::Transfer;
	UPROPERTY(SaveGame) FString From;
	UPROPERTY(SaveGame) FString To;
	UPROPERTY(SaveGame) TArray<FGFItemStack> Stacks;
	UPROPERTY(SaveGame) FName Reason;
	UPROPERTY(SaveGame) FDateTime TimeUtc;
};

/** Where account inventories live (player states, offline guest records, or a plain map in tests). */
class GFCORE_API IGFLedgerAccounts
{
public:
	virtual ~IGFLedgerAccounts() = default;
	/** False if the account doesn't exist or can't be used right now. */
	virtual bool ReadAccount(const FString& Account, FGFInventory& Out) const = 0;
	virtual void WriteAccount(const FString& Account, const FGFInventory& In) = 0;
};

/** Test / tool implementation. */
class GFCORE_API FGFMapLedgerAccounts : public IGFLedgerAccounts
{
public:
	TMap<FString, FGFInventory> Accounts;
	virtual bool ReadAccount(const FString& Account, FGFInventory& Out) const override;
	virtual void WriteAccount(const FString& Account, const FGFInventory& In) override;
};

/**
 * Every material movement between accounts goes through here. Rules:
 * - All or nothing: a transfer either moves every stack or changes nothing.
 * - Idempotent: each operation carries a TxId; replaying one (a resent RPC, a retried letter claim) is a no-op.
 * - Conserving: items are only created by Mint (rewards, harvests) and destroyed by Burn (crafting, sinks);
 *   for every item, sum(accounts) + sum(open escrows) == minted - burned + starting stock.
 */
USTRUCT()
struct GFCORE_API FGFTransferLedger
{
	GENERATED_BODY()

	/** How many applied TxIds are remembered (oldest are forgotten first). */
	static constexpr int32 MaxRememberedTx = 8192;
	static constexpr int32 MaxRecentEntries = 128;

	EGFTxResult Transfer(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FString& From, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason = NAME_None);
	EGFTxResult Mint(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason = NAME_None);
	EGFTxResult Burn(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FString& From, const TArray<FGFItemStack>& Stacks, FName Reason = NAME_None);

	/** Creates the escrow if it doesn't exist (idempotent by id). */
	FGFEscrow& OpenEscrow(const FGuid& EscrowId, FName Kind);
	EGFTxResult Deposit(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FGuid& EscrowId, const FString& From, const TArray<FGFItemStack>& Stacks, FName Reason = NAME_None);
	/** Everything in the escrow goes to To; the escrow closes. */
	EGFTxResult Release(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FGuid& EscrowId, const FString& To, FName Reason = NAME_None);
	/** Every depositor gets their own stake back; the escrow closes. */
	EGFTxResult Refund(IGFLedgerAccounts& Accounts, const FGuid& TxId, const FGuid& EscrowId, FName Reason = NAME_None);
	/** Refunds every open escrow of Kind (e.g. all wagers after a crash). Returns how many were refunded. */
	int32 RefundOpen(IGFLedgerAccounts& Accounts, FName Kind);

	const FGFEscrow* FindEscrow(const FGuid& EscrowId) const;
	/** Count of ItemId held in open escrows. */
	int64 HeldInEscrow(FName ItemId) const;
	int64 GetMinted(FName ItemId) const;
	int64 GetBurned(FName ItemId) const;
	bool WasApplied(const FGuid& TxId) const;
	const TArray<FGFLedgerEntry>& GetRecent() const { return Recent; }
	/** Drops closed escrows (they're only kept for replays). */
	void PruneClosedEscrows();
	/** Call after loading from a save (rebuilds the lookup set). */
	void RebuildIndex();

	UPROPERTY(SaveGame) TArray<FGuid> AppliedOrder;
	UPROPERTY(SaveGame) TArray<FGFEscrow> Escrows;
	UPROPERTY(SaveGame) TArray<FGFLedgerEntry> Recent;
	UPROPERTY(SaveGame) TMap<FName, int64> Minted;
	UPROPERTY(SaveGame) TMap<FName, int64> Burned;

private:
	FGFEscrow* FindEscrowMutable(const FGuid& EscrowId);
	void MarkApplied(const FGuid& TxId);
	void Log(const FGuid& TxId, EGFTxKind Kind, const FString& From, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason);

	TSet<FGuid> Applied;
};
