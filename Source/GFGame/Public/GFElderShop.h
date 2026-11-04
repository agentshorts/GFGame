#pragma once

// Server shop. Stock for each elder account lives here. Player items stay on the game state ledger.
#include "CoreMinimal.h"
#include "GFEconomy.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFElderShop.generated.h"

UCLASS()
class GFGAME_API UGFElderShopSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UGFElderShopSubsystem* Get(const UWorld* World);

	/**
	 * Server. TxId is hash(menu session id, player key, elder key, row, request id).
	 * OutWhy is a toast when nothing was traded. An invalid session id is refused.
	 */
	EGFTxResult Trade(const FGuid& SessionId, const FString& PlayerKey, const FString& ElderKey, FName TownId, FName RowId, int32 RequestId, FString& OutWhy);

private:
	UPROPERTY()
	TMap<FString, FGFInventory> Stock;
};
