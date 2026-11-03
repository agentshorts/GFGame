#include "GFElderShop.h"
#include "GFElderTrade.h"
#include "GFGameState.h"
#include "Engine/World.h"

namespace
{
	class FElderAccounts : public IGFLedgerAccounts
	{
	public:
		AGFGameState* GS = nullptr;
		TMap<FString, FGFInventory>* Stock = nullptr;

		virtual bool ReadAccount(const FString& Account, FGFInventory& Out) const override
		{
			if (Stock)
			{
				if (const FGFInventory* Found = Stock->Find(Account))
				{
					Out = *Found;
					return true;
				}
			}
			return GS && GS->Accounts().ReadAccount(Account, Out);
		}

		virtual void WriteAccount(const FString& Account, const FGFInventory& In) override
		{
			if (Stock && Account.StartsWith(TEXT("elder:")))
			{
				Stock->Add(Account, In);
				return;
			}
			if (GS)
			{
				GS->Accounts().WriteAccount(Account, In);
			}
		}
	};
}

UGFElderShopSubsystem* UGFElderShopSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UGFElderShopSubsystem>() : nullptr;
}

EGFTxResult UGFElderShopSubsystem::Trade(const FString& PlayerKey, const FString& ElderKey, FName TownId, FName RowId, int32 RequestId, FString& OutWhy)
{
	OutWhy.Reset();
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		OutWhy = TEXT("The shop is closed.");
		return EGFTxResult::UnknownAccount;
	}
	AGFGameState* GS = World->GetGameState<AGFGameState>();
	if (!GS || !GS->HasAuthority())
	{
		OutWhy = TEXT("The shop is closed.");
		return EGFTxResult::UnknownAccount;
	}
	const FGFElderTradeCatalog Catalog = FGFElderTradeCatalog::Load();
	const FGFElderTradeListing* Row = Catalog.Find(RowId);
	if (!Row || Row->TownId != TownId)
	{
		OutWhy = TEXT("This elder does not offer that.");
		return EGFTxResult::InvalidStacks;
	}
	FElderAccounts Accounts;
	Accounts.GS = GS;
	Accounts.Stock = &Stock;
	const FGuid TxId = FGFElderTrade::MakeTxId(PlayerKey, ElderKey, RowId, RequestId);
	return FGFElderTrade::Apply(GS->Ledger, Accounts, TxId, PlayerKey, ElderKey, Row->Give, Row->Get, OutWhy);
}
