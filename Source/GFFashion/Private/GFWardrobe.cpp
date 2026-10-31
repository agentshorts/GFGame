#include "GFWardrobe.h"

#include "GFActivityWin.h"
#include "Misc/Crc.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "Engine/World.h"

const FName FGFWardrobe::ActivityStarter(TEXT("Starter"));
const FName FGFWardrobe::ActivityRunway(TEXT("Runway"));
const FName FGFWardrobe::ActivityBeatHop(TEXT("BeatHop"));
const FName FGFWardrobe::ActivityMine(TEXT("Mine"));
const FName FGFWardrobe::ActivityCraft(TEXT("Craft"));

namespace
{
	static bool ContextOpen(FName Context)
	{
		return Context.IsNone() || Context == TEXT("None");
	}

	static bool Lists(const FGFFashionPiece& Piece, FName Activity, FName Context)
	{
		if (!Piece.Sources.Contains(Activity))
		{
			return false;
		}
		if (Activity == FGFWardrobe::ActivityBeatHop && !ContextOpen(Piece.SourceContext) && Piece.SourceContext != Context)
		{
			return false;
		}
		return true;
	}

	static EGFTxResult MintPiece(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FString& PlayerKey, FName PieceId, FName Reason)
	{
		return Ledger.Mint(Accounts, FGFWardrobe::TxIdFor(PlayerKey, PieceId, false), PlayerKey, { FGFItemStack(PieceId, 1) }, Reason);
	}

	static void Collect(EGFTxResult Result, FName PieceId, TArray<FName>* OutGranted)
	{
		if (Result == EGFTxResult::Applied && OutGranted)
		{
			OutGranted->Add(PieceId);
		}
	}

	static void ToastGranted(UWorld* World, const FString& PlayerKey, const TArray<FName>& Granted)
	{
		if (!World || Granted.Num() == 0)
		{
			return;
		}
		AGFGameState* GS = World->GetGameState<AGFGameState>();
		AGFPlayerState* PS = GS ? GS->FindPlayerByKey(PlayerKey) : nullptr;
		if (!PS)
		{
			return;
		}
		for (const FName Id : Granted)
		{
			PS->NotifyLand(FGFWardrobe::ToastFor(Id));
		}
	}

	static bool LiveServer(UWorld* World, const FString& PlayerKey, AGFGameState*& OutGS)
	{
		OutGS = nullptr;
		if (!World || PlayerKey.IsEmpty() || World->GetNetMode() == NM_Client)
		{
			return false;
		}
		OutGS = World->GetGameState<AGFGameState>();
		return OutGS && OutGS->HasAuthority();
	}
}

FGuid FGFWardrobe::TxIdFor(const FString& PlayerKey, FName PieceId, bool bCraftBurn)
{
	const FString Text = PlayerKey + TEXT("|") + PieceId.ToString() + (bCraftBurn ? TEXT("|burn") : TEXT("|mint"));
	const uint32 A = FCrc::StrCrc32(*Text);
	const uint32 B = FCrc::StrCrc32(*Text, 0x811C9DC5u);
	const uint32 C = bCraftBurn ? 0x43524654u : 0x57415244u;
	const uint32 D = bCraftBurn ? 0x4255524Eu : 0x524F4245u;
	return FGuid(A, B, C, D);
}

bool FGFWardrobe::Owns(const FGFInventory& Inventory, FName PieceId)
{
	return Inventory.GetCount(PieceId) > 0;
}

void FGFWardrobe::GrantStarters(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FString& PlayerKey, TArray<FName>* OutGranted)
{
	if (PlayerKey.IsEmpty())
	{
		return;
	}
	for (const FGFFashionPiece& Piece : FGFFashionCatalog::Pieces())
	{
		if (!Piece.bStarter)
		{
			continue;
		}
		Collect(MintPiece(Ledger, Accounts, PlayerKey, Piece.Id, TEXT("Fashion.Starter")), Piece.Id, OutGranted);
	}
}

void FGFWardrobe::GrantForActivity(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FString& PlayerKey, FName Activity, FName Context, TArray<FName>* OutGranted)
{
	if (PlayerKey.IsEmpty() || Activity.IsNone())
	{
		return;
	}
	for (const FGFFashionPiece& Piece : FGFFashionCatalog::Pieces())
	{
		if (!Lists(Piece, Activity, Context))
		{
			continue;
		}
		const FName Reason = Activity == ActivityRunway ? TEXT("Fashion.Runway")
			: Activity == ActivityBeatHop ? TEXT("Fashion.BeatHop")
			: Activity == ActivityMine ? TEXT("Fashion.Mine")
			: TEXT("Fashion.Activity");
		Collect(MintPiece(Ledger, Accounts, PlayerKey, Piece.Id, Reason), Piece.Id, OutGranted);
	}
}

EGFWardrobeResult FGFWardrobe::TryCraft(FGFTransferLedger& Ledger, IGFLedgerAccounts& Accounts, const FString& PlayerKey, FName PieceId)
{
	const FGFFashionPiece* Piece = FGFFashionCatalog::FindPiece(PieceId);
	if (!Piece)
	{
		return EGFWardrobeResult::UnknownPiece;
	}
	if (!Piece->Sources.Contains(ActivityCraft) || Piece->CraftCost.Num() == 0)
	{
		return EGFWardrobeResult::NotThisSource;
	}
	if (PlayerKey.IsEmpty())
	{
		return EGFWardrobeResult::NoAccount;
	}
	const FGuid MintId = TxIdFor(PlayerKey, PieceId, false);
	if (Ledger.WasApplied(MintId))
	{
		return EGFWardrobeResult::AlreadyOwned;
	}
	FGFInventory Inv;
	if (!Accounts.ReadAccount(PlayerKey, Inv))
	{
		return EGFWardrobeResult::NoAccount;
	}
	if (!Inv.HasAll(Piece->CraftCost))
	{
		return EGFWardrobeResult::MissingMaterials;
	}
	const EGFTxResult Burned = Ledger.Burn(Accounts, TxIdFor(PlayerKey, PieceId, true), PlayerKey, Piece->CraftCost, TEXT("Fashion.Tailor"));
	if (Burned != EGFTxResult::Applied && Burned != EGFTxResult::AlreadyApplied)
	{
		return Burned == EGFTxResult::InsufficientItems ? EGFWardrobeResult::MissingMaterials : EGFWardrobeResult::Refused;
	}
	const EGFTxResult Minted = Ledger.Mint(Accounts, MintId, PlayerKey, { FGFItemStack(PieceId, 1) }, TEXT("Fashion.Tailor"));
	if (Minted == EGFTxResult::Applied)
	{
		return EGFWardrobeResult::Granted;
	}
	if (Minted == EGFTxResult::AlreadyApplied)
	{
		return EGFWardrobeResult::AlreadyOwned;
	}
	return EGFWardrobeResult::Refused;
}

FString FGFWardrobe::ToastFor(FName PieceId)
{
	const FGFFashionPiece* Piece = FGFFashionCatalog::FindPiece(PieceId);
	const FString Name = Piece ? Piece->DisplayName : PieceId.ToString();
	return Name + TEXT(" is waiting on the rack.");
}

FString FGFWardrobe::RackLabel(const FGFFashionPiece& Piece, bool bOwned)
{
	if (bOwned)
	{
		return Piece.DisplayName;
	}
	return Piece.SourceLine.IsEmpty() ? FString(TEXT("Locked")) : Piece.SourceLine;
}

FLinearColor FGFWardrobe::RackColor(const FGFFashionPiece& Piece, bool bOwned)
{
	return bOwned ? Piece.BaseColor : FLinearColor(0.12f, 0.12f, 0.14f);
}

int32 FGFWardrobe::HintStep(bool bRackOpened, bool bEquipped, bool bThemeChosen, bool bShowWalking)
{
	if (bShowWalking)
	{
		return 4;
	}
	if (!bRackOpened && !bEquipped)
	{
		return 0;
	}
	if (!bEquipped)
	{
		return 1;
	}
	if (!bThemeChosen)
	{
		return 2;
	}
	return 3;
}

void FGFWardrobe::HintLines(int32 Step, TArray<FString>& OutLines)
{
	OutLines.Reset();
	const TCHAR* Steps[] = {
		TEXT("Open the wardrobe rack."),
		TEXT("Equip a piece."),
		TEXT("Pick a theme."),
		TEXT("Walk the Start arch."),
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Steps); ++i)
	{
		OutLines.Add(FString::Printf(TEXT("%s%s"), i == Step ? TEXT("> ") : TEXT("  "), Steps[i]));
	}
}

void FGFWardrobe::HandleNewCharacter(UWorld* World, const FString& PlayerKey)
{
	AGFGameState* GS = nullptr;
	if (!LiveServer(World, PlayerKey, GS))
	{
		return;
	}
	TArray<FName> Granted;
	GrantStarters(GS->Ledger, GS->Accounts(), PlayerKey, &Granted);
	ToastGranted(World, PlayerKey, Granted);
}

void FGFWardrobe::HandleActivity(UWorld* World, const FString& PlayerKey, FName Activity, FName Context)
{
	AGFGameState* GS = nullptr;
	if (!LiveServer(World, PlayerKey, GS))
	{
		return;
	}
	TArray<FName> Granted;
	GrantForActivity(GS->Ledger, GS->Accounts(), PlayerKey, Activity, Context, &Granted);
	ToastGranted(World, PlayerKey, Granted);
}
