#pragma once

// Owns, grants and labels wardrobe pieces. The catalog (DT_FashionPieces) says where each piece comes from.
// Grants go through the transfer ledger, so the same player cannot be paid twice for the same piece.

#include "CoreMinimal.h"
#include "GFEconomy.h"
#include "GFFashionTypes.h"

class UWorld;

struct GFFASHION_API FGFWardrobe
{
	static const FName ActivityStarter;
	static const FName ActivityRunway;
	static const FName ActivityBeatHop;
	static const FName ActivityMine;
	static const FName ActivityCraft;

	/** Stable per player and piece. The burn id is a different guid, so a craft can spend and then mint. */
	static FGuid TxIdFor(const FString& PlayerKey, FName PieceId, bool bCraftBurn);

	static bool Owns(const FGFInventory& Inventory, FName PieceId);

	/** One free piece per slot, once. OutGranted receives ids that were minted on this call. */
	static void GrantStarters(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FString& PlayerKey, TArray<FName>* OutGranted = nullptr);

	/**
	 * Mints every piece whose Sources list Activity.
	 * Beat Hop pieces with a SourceContext only match that course (Fern is course 1). Runway and Mine ignore context.
	 */
	static void GrantForActivity(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FString& PlayerKey, FName Activity, FName Context, TArray<FName>* OutGranted = nullptr);

	/** Spends CraftCost, then mints the piece. A second craft does not spend again. */
	static EGFWardrobeResult TryCraft(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FString& PlayerKey, FName PieceId);

	static FString ToastFor(FName PieceId);
	static FString RackLabel(const FGFFashionPiece& Piece, bool bOwned);
	static FLinearColor RackColor(const FGFFashionPiece& Piece, bool bOwned);

	/** 0 open the rack, 1 equip, 2 pick a theme, 3 walk the start arch, 4 none (the show has started). */
	static int32 HintStep(bool bRackOpened, bool bEquipped, bool bThemeChosen, bool bShowWalking);
	static void HintLines(int32 Step, TArray<FString>& OutLines);

	/** Server. Mints starters or activity pieces and toasts each new id. No game state: nothing happens. */
	static void HandleNewCharacter(UWorld* World, const FString& PlayerKey);
	static void HandleActivity(UWorld* World, const FString& PlayerKey, FName Activity, FName Context);
};
