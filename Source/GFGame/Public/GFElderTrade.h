#pragma once

// Elder shop rows and one idempotent trade. Prices are 5/4 to buy and 4/5 to sell, rounded half up.
// The ledger moves the items. A short elder is stocked with Mint into the elder account, and that is the only create.
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFEconomy.h"
#include "GFElderTrade.generated.h"

UENUM()
enum class EGFElderTradeKind : uint8
{
	Buy,
	Sell
};

/** DT_ElderTrades.csv. Row name is the trade id. Give is what the player hands over. Get is what the player receives. */
USTRUCT()
struct GFGAME_API FGFElderTradeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "GF|Elder")
	FName TownId;

	UPROPERTY(EditAnywhere, Category = "GF|Elder")
	EGFElderTradeKind Kind = EGFElderTradeKind::Buy;

	UPROPERTY(EditAnywhere, Category = "GF|Elder")
	TArray<FGFItemStack> Give;

	UPROPERTY(EditAnywhere, Category = "GF|Elder")
	TArray<FGFItemStack> Get;
};

struct GFGAME_API FGFElderTradeListing
{
	FName RowId;
	FName TownId;
	EGFElderTradeKind Kind = EGFElderTradeKind::Buy;
	TArray<FGFItemStack> Give;
	TArray<FGFItemStack> Get;
};

struct GFGAME_API FGFElderTradeCatalog
{
	TArray<FGFElderTradeListing> Rows;

	static const FGFElderTradeCatalog& Builtin();
	static FGFElderTradeCatalog Load();
	static FGFElderTradeCatalog FromTable(const UDataTable* Table);

	const FGFElderTradeListing* Find(FName RowId) const;
	TArray<const FGFElderTradeListing*> ForTown(FName TownId) const;
};

struct GFGAME_API FGFElderTrade
{
	/** 5/4. Elder sells to the player. */
	static constexpr double BuyMultiplier = 1.25;
	/** 4/5. Elder buys from the player. */
	static constexpr double SellMultiplier = 0.8;

	/** Round half up of GoodsValue * Numerator / Denominator. */
	static int32 RoundRatio(int64 GoodsValue, int32 Numerator, int32 Denominator);
	/** Payment value for a pile of goods. Buy uses 5/4, sell uses 4/5. */
	static int32 Quote(EGFElderTradeKind Kind, int64 GoodsValue);

	static FGuid MakeTxId(const FString& PlayerKey, const FString& ElderKey, FName RowId, int32 RequestId);
	static FGuid ChildId(const FGuid& TxId, int32 Salt);

	/**
	 * All or nothing. TxId is the give transfer. A resend of the same id does not pay twice.
	 * Not enough items changes nothing and does not consume the id.
	 * OutWhy is set when the result is neither Applied nor AlreadyApplied.
	 */
	static EGFTxResult Apply(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FGuid& TxId, const FString& PlayerKey, const FString& ElderKey, const TArray<FGFItemStack>& Give, const TArray<FGFItemStack>& Get, FString& OutWhy);

	static FString ShortageText(const FGFInventory& Have, const TArray<FGFItemStack>& Need);
};
