#include "GFGameState.h"
#include "GFRecipeTypes.h"
#include "GFDataTables.h"
#include "GFPlayerState.h"
#include "GFVoxelWorld.h"
#include "GFInventoryComponent.h"
#include "GFDataTables.h"
#include "GFLog.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AGFGameState::AGFGameState()
{
	HostWorldId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

void AGFGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFGameState, HostWorldId);
	DOREPLIFETIME(AGFGameState, bFreeBuild);
	DOREPLIFETIME(AGFGameState, Land);
	DOREPLIFETIME(AGFGameState, FactionLeaders);
	DOREPLIFETIME(AGFGameState, ChatLog);
}

void AGFGameState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		EnsureLand();
		GetWorldTimerManager().SetTimer(RentTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			ServerUpdateRent();
			ServerCheckPendingPackUps();
		}), RentCheckSeconds, true);
		GetWorldTimerManager().SetTimer(BackupTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			ServerBackupPlots();
		}), BlueprintBackupSeconds, true);
	}
}

AGFPlayerState* AGFGameState::FindPlayerByKey(const FString& PlayerKey) const
{
	for (APlayerState* PS : PlayerArray)
	{
		AGFPlayerState* GPS = Cast<AGFPlayerState>(PS);
		if (GPS && GPS->PlayerKey == PlayerKey)
		{
			return GPS;
		}
	}
	return nullptr;
}

AGFVoxelWorld* AGFGameState::GetVoxelWorld() const
{
	if (!CachedVoxel.IsValid() && GetWorld())
	{
		for (TActorIterator<AGFVoxelWorld> It(GetWorld()); It; ++It)
		{
			CachedVoxel = *It;
			break;
		}
	}
	return CachedVoxel.Get();
}

TArray<FGFItemStack> AGFGameState::StarterKit()
{
	TArray<FGFItemStack> Kit;
	for (const TCHAR* Id : { TEXT("Dirt"), TEXT("WoodPlank"), TEXT("Stone") })
	{
		FGFItemStack& S = Kit.AddDefaulted_GetRef();
		S.ItemId = Id;
		S.Count = 64;
	}
	return Kit;
}

// ---------------- Economy ----------------

namespace
{
	class FGFServerAccounts : public IGFLedgerAccounts
	{
	public:
		explicit FGFServerAccounts(AGFGameState* InGS) : GS(InGS) {}

		virtual bool ReadAccount(const FString& Account, FGFInventory& Out) const override
		{
			if (Account.IsEmpty() || !GS.IsValid())
			{
				return false;
			}
			const AGFPlayerState* PS = GS->FindPlayerByKey(Account);
			if (PS && !PS->bLoggedOut)
			{
				if (!PS->bCharacterReady || !PS->Inventory)
				{
					return false; // the upload would overwrite anything we moved now
				}
				Out = PS->Inventory->GetInventory();
				return true;
			}
			const FGFGuestRecord* Rec = GS->GuestRecords.Find(Account);
			if (Rec && Rec->bHasCharacter)
			{
				Out = Rec->LastCharacter.Inventory;
				return true;
			}
			return false;
		}

		virtual void WriteAccount(const FString& Account, const FGFInventory& In) override
		{
			if (!GS.IsValid())
			{
				return;
			}
			AGFPlayerState* PS = GS->FindPlayerByKey(Account);
			if (PS && !PS->bLoggedOut)
			{
				PS->Inventory->SetInventory(In); // schedules the write-back to the owner
				return;
			}
			if (FGFGuestRecord* Rec = GS->GuestRecords.Find(Account))
			{
				Rec->LastCharacter.Inventory = In;
				++Rec->LastCharacter.Revision; // the host's copy must beat the guest's on rejoin
			}
		}

	private:
		TWeakObjectPtr<AGFGameState> GS;
	};
}

UDataTable* AGFGameState::GetItemTable() const
{
	return FGFDataTables::Get(ItemTable, TEXT("DT_Items"), FGFItemRow::StaticStruct());
}

IGFLedgerAccounts& AGFGameState::Accounts()
{
	if (!AccountsImpl)
	{
		AccountsImpl = MakeUnique<FGFServerAccounts>(this);
	}
	return *AccountsImpl;
}

static EGFTxResult LogTx(const TCHAR* What, EGFTxResult R)
{
	if (R != EGFTxResult::Applied && R != EGFTxResult::AlreadyApplied)
	{
		UE_LOG(LogGF, Verbose, TEXT("Ledger: %s refused (%s)"), What, *UEnum::GetValueAsString(R));
	}
	return R;
}

EGFTxResult AGFGameState::ServerTransfer(const FGuid& TxId, const FString& From, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	return HasAuthority() ? LogTx(TEXT("transfer"), Ledger.Transfer(Accounts(), TxId, From, To, Stacks, Reason)) : EGFTxResult::UnknownAccount;
}

EGFTxResult AGFGameState::ServerMint(const FGuid& TxId, const FString& To, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	return HasAuthority() ? LogTx(TEXT("mint"), Ledger.Mint(Accounts(), TxId, To, Stacks, Reason)) : EGFTxResult::UnknownAccount;
}

EGFTxResult AGFGameState::ServerBurn(const FGuid& TxId, const FString& From, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	return HasAuthority() ? LogTx(TEXT("burn"), Ledger.Burn(Accounts(), TxId, From, Stacks, Reason)) : EGFTxResult::UnknownAccount;
}

EGFTxResult AGFGameState::ServerDeposit(const FGuid& TxId, const FGuid& EscrowId, FName Kind, const FString& From, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	if (!HasAuthority())
	{
		return EGFTxResult::UnknownAccount;
	}
	Ledger.OpenEscrow(EscrowId, Kind);
	return LogTx(TEXT("deposit"), Ledger.Deposit(Accounts(), TxId, EscrowId, From, Stacks, Reason));
}

EGFTxResult AGFGameState::ServerRelease(const FGuid& TxId, const FGuid& EscrowId, const FString& To, FName Reason)
{
	return HasAuthority() ? LogTx(TEXT("release"), Ledger.Release(Accounts(), TxId, EscrowId, To, Reason)) : EGFTxResult::UnknownAccount;
}

EGFTxResult AGFGameState::ServerRefund(const FGuid& TxId, const FGuid& EscrowId, FName Reason)
{
	return HasAuthority() ? LogTx(TEXT("refund"), Ledger.Refund(Accounts(), TxId, EscrowId, Reason)) : EGFTxResult::UnknownAccount;
}

int64 AGFGameState::CountEverywhere(FName ItemId) const
{
	int64 Total = Ledger.HeldInEscrow(ItemId);
	TSet<FString> Online;
	for (APlayerState* PS : PlayerArray)
	{
		const AGFPlayerState* GPS = Cast<AGFPlayerState>(PS);
		if (GPS && !GPS->bLoggedOut)
		{
			Online.Add(GPS->PlayerKey);
			Total += GPS->Inventory ? GPS->Inventory->GetCount(ItemId) : 0;
		}
	}
	for (const TPair<FString, FGFGuestRecord>& P : GuestRecords)
	{
		if (!Online.Contains(P.Key) && P.Value.bHasCharacter)
		{
			Total += P.Value.LastCharacter.Inventory.GetCount(ItemId);
		}
	}
	return Total;
}

// ---------------- Land: plots, rent, mail, blueprints ----------------

int64 AGFGameState::LandToday() const
{
	return FGFLandRules::DayIndex(FDateTime::Now()) + DebugDayOffset;
}

void AGFGameState::EnsureLand()
{
	if (!HasAuthority() || Land.Plots.Num() > 0)
	{
		return;
	}
	const AGFVoxelWorld* Voxel = GetVoxelWorld();
	const FString Landlord = Land.LandlordKey;
	const TArray<FString> Builders = Land.Builders;
	Land = FGFLandRules::MakeDefaultLayout(Voxel ? Voxel->PlotSizeInBlocks : FIntVector(64, 64, 32));
	Land.LandlordKey = Landlord;
	Land.Builders = Builders;
	MarkLandChanged();
}

void AGFGameState::MarkLandChanged()
{
	for (FGFLandPlot& P : Land.Plots)
	{
		FGFLandRules::RefreshStatus(P);
	}
	++Land.Revision;
	ForceNetUpdate();
	OnLandChanged.Broadcast();
}

void AGFGameState::OnRep_Land()
{
	OnLandChanged.Broadcast();
}

void AGFGameState::ServerSetLandlord(const FString& Key)
{
	if (!HasAuthority() || Key.IsEmpty())
	{
		return;
	}
	EnsureLand();
	const FString Old = Land.LandlordKey;
	if (Old == Key)
	{
		return;
	}
	Land.LandlordKey = Key;
	if (!Old.IsEmpty())
	{
		for (FGFMailLetter& L : Mail)
		{
			if (L.ToKey == Old && L.Kind == EGFLetterKind::Rent)
			{
				L.ToKey = Key; // rent letters always belong to whoever hosts the world
			}
		}
	}
	MarkLandChanged();
	ServerRefreshMailbox(Key);
}

// --- Factions ---

int32 AGFGameState::FactionCount(FName FactionId) const
{
	int32 N = 0;
	for (const APlayerState* P : PlayerArray)
	{
		const AGFPlayerState* PS = Cast<AGFPlayerState>(P);
		N += PS && !PS->bLoggedOut && !FactionId.IsNone() && PS->FactionId == FactionId ? 1 : 0;
	}
	return N;
}

FString AGFGameState::FactionLeaderOf(FName FactionId) const
{
	const FGFFactionLeader* L = FactionLeaders.FindByPredicate([&](const FGFFactionLeader& F) { return F.FactionId == FactionId; });
	return L && !FactionId.IsNone() ? L->LeaderKey : FString();
}

bool AGFGameState::IsFactionLeader(const AGFPlayerState* PS) const
{
	return PS && !PS->FactionId.IsNone() && !PS->PlayerKey.IsEmpty() && FactionLeaderOf(PS->FactionId) == PS->PlayerKey;
}

FName AGFGameState::ServerClaimFaction(AGFPlayerState* PS, FName FactionId)
{
	if (!HasAuthority() || !PS)
	{
		return TEXT("NotAuthority");
	}
	if (PS->FactionId == FactionId)
	{
		return TEXT("Same");
	}
	if (!FactionId.IsNone() && MaxPerFaction > 0 && FactionCount(FactionId) >= MaxPerFaction)
	{
		return TEXT("Full");
	}
	PS->FactionId = FactionId;
	PS->ForceNetUpdate();
	if (!PS->PlayerKey.IsEmpty())
	{
		SessionFactions.Add(PS->PlayerKey, FactionId);
	}
	// Leadership stays with the first claimer for the whole session (even if they switch away or log out).
	if (!FactionId.IsNone() && FactionLeaderOf(FactionId).IsEmpty())
	{
		FGFFactionLeader L;
		L.FactionId = FactionId;
		L.LeaderKey = PS->PlayerKey;
		FactionLeaders.Add(L);
	}
	ForceNetUpdate();
	OnFactionChanged.Broadcast(PS, FactionId);
	return TEXT("Ok");
}

// --- Chat ---

FName AGFGameState::ServerHandleChat(AGFPlayerState* By, const FString& Text)
{
	if (!HasAuthority() || !By)
	{
		return TEXT("NotAuthority");
	}
	const FString Clean = Text.TrimStartAndEnd().Left(MaxChatChars);
	if (Clean.IsEmpty())
	{
		return TEXT("Empty");
	}
	for (const TPair<FName, TWeakObjectPtr<UObject>>& Pair : ModeHandlers)
	{
		if (const IGFModeHandler* H = Cast<IGFModeHandler>(Pair.Value.Get()))
		{
			const FName Why = H->ChatBlockReason(By);
			if (!Why.IsNone())
			{
				return Why;
			}
		}
	}
	FGFChatMessage M;
	M.Seq = NextChatSeq++;
	M.SenderKey = By->PlayerKey;
	M.SenderName = By->GetPlayerName();
	M.Text = Clean;
	M.Channel = TEXT("All");
	ChatLog.Add(M);
	if (ChatLog.Num() > MaxChat)
	{
		ChatLog.RemoveAt(0, ChatLog.Num() - MaxChat);
	}
	ForceNetUpdate();
	return TEXT("Ok");
}

bool AGFGameState::IsJoinRefused(const FString& PlayerKey, FString& OutWhy) const
{
	for (const TPair<FName, TWeakObjectPtr<UObject>>& Pair : ModeHandlers)
	{
		if (const IGFModeHandler* H = Cast<IGFModeHandler>(Pair.Value.Get()))
		{
			if (H->RefuseJoin(PlayerKey, OutWhy))
			{
				return true;
			}
		}
	}
	return false;
}

bool AGFGameState::IsKnownPlayer(const FString& Key) const
{
	if (Key.IsEmpty())
	{
		return false;
	}
	const AGFPlayerState* PS = FindPlayerByKey(Key);
	if (PS && !PS->bLoggedOut)
	{
		return true;
	}
	const FGFGuestRecord* Rec = GuestRecords.Find(Key);
	return Rec && Rec->bHasCharacter;
}

FString AGFGameState::DisplayNameOf(const FString& Key) const
{
	if (const AGFPlayerState* PS = FindPlayerByKey(Key))
	{
		return PS->GetPlayerName();
	}
	if (const FGFGuestRecord* Rec = GuestRecords.Find(Key))
	{
		return Rec->DisplayName;
	}
	return Key;
}

void AGFGameState::SetTenant(FGFLandPlot& Plot, const FString& Key)
{
	Plot.TenantKey = Key;
	Plot.TenantName = DisplayNameOf(Key);
	const AGFPlayerState* PS = FindPlayerByKey(Key);
	Plot.TenantColor = PS ? PS->ColorIndex : -1;
	Plot.OwedPeriods = 0;
	Plot.LastAccruedDay = LandToday(); // the first day is on the house
	Plot.bAutoPay = true;
	MarkLandChanged();
}

// --- Mail ---

const FGFMailLetter* AGFGameState::FindLetter(const FGuid& LetterId) const
{
	return Mail.FindByPredicate([&LetterId](const FGFMailLetter& L) { return L.Id == LetterId; });
}

FGuid AGFGameState::ServerSendLetter(EGFLetterKind Kind, const FString& FromKey, const FString& ToKey, const FString& Text, const TArray<FGFItemStack>& Stacks, FName PlotId, EGFLandResult& OutResult)
{
	OutResult = EGFLandResult::NotReady;
	if (!HasAuthority() || ToKey.IsEmpty())
	{
		return FGuid();
	}
	FGFMailLetter L;
	L.Id = FGuid::NewGuid();
	L.Kind = Kind;
	L.FromKey = FromKey;
	L.FromName = DisplayNameOf(FromKey);
	L.ToKey = ToKey;
	L.Text = Text.Left(500);
	L.PlotId = PlotId;
	L.SentUtc = FDateTime::UtcNow();
	if (Stacks.Num() > 0)
	{
		TArray<FGFItemStack> Norm;
		if (!FGFEconomy::NormalizeStacks(Stacks, Norm))
		{
			OutResult = EGFLandResult::InvalidStacks;
			return FGuid();
		}
		static const FName MailKind(TEXT("Mail"));
		const FName Reason = Kind == EGFLetterKind::Rent ? FName(TEXT("Rent")) : FName(TEXT("Gift"));
		const EGFTxResult R = ServerDeposit(FGuid::NewGuid(), L.Id, MailKind, FromKey, Norm, Reason);
		if (R != EGFTxResult::Applied)
		{
			OutResult = R == EGFTxResult::InsufficientItems ? EGFLandResult::CantAfford
				: R == EGFTxResult::UnknownAccount ? EGFLandResult::NotReady
				: EGFLandResult::InvalidStacks;
			// The empty escrow we opened holds nothing; refund closes it so it never lingers.
			ServerRefund(FGuid::NewGuid(), L.Id, TEXT("MailFailed"));
			Ledger.PruneClosedEscrows();
			return FGuid();
		}
		L.Stacks = Norm;
	}
	Mail.Add(L);
	OutResult = EGFLandResult::Ok;
	PruneMail();
	ServerRefreshMailbox(ToKey);
	if (AGFPlayerState* To = FindPlayerByKey(ToKey))
	{
		if (!To->bLoggedOut)
		{
			To->NotifyLand(FString::Printf(TEXT("You've got mail from %s"), *L.FromName));
		}
	}
	return L.Id;
}

EGFLandResult AGFGameState::ServerClaimLetter(const FString& Key, const FGuid& LetterId)
{
	FGFMailLetter* L = Mail.FindByPredicate([&LetterId](const FGFMailLetter& M) { return M.Id == LetterId; });
	if (!L || L->ToKey != Key || Key.IsEmpty())
	{
		return EGFLandResult::NoSuchLetter;
	}
	if (L->bClaimed)
	{
		return EGFLandResult::AlreadyClaimed;
	}
	if (L->HasItems())
	{
		const EGFTxResult R = ServerRelease(FGuid::NewGuid(), L->Id, Key, TEXT("MailClaim"));
		if (R == EGFTxResult::EscrowClosed)
		{
			L->bClaimed = true;
			ServerRefreshMailbox(Key);
			return EGFLandResult::AlreadyClaimed;
		}
		if (R != EGFTxResult::Applied)
		{
			return EGFLandResult::NotReady;
		}
	}
	L->bClaimed = true;
	ServerRefreshMailbox(Key);
	return EGFLandResult::Ok;
}

void AGFGameState::PruneMail()
{
	// Unclaimed letters are never dropped (they hold items). Keep the newest claimed ones per recipient.
	TMap<FString, int32> ClaimedKept;
	for (int32 i = Mail.Num() - 1; i >= 0; --i)
	{
		if (Mail[i].bClaimed)
		{
			int32& N = ClaimedKept.FindOrAdd(Mail[i].ToKey);
			if (++N > FGFLandRules::MaxClaimedLettersKept)
			{
				Mail.RemoveAt(i);
			}
		}
	}
}

void AGFGameState::ServerRefreshMailbox(const FString& Key)
{
	AGFPlayerState* PS = FindPlayerByKey(Key);
	if (!HasAuthority() || !PS || PS->bLoggedOut)
	{
		return;
	}
	TArray<FGFMailLetter> Box;
	for (const FGFMailLetter& L : Mail)
	{
		if (L.ToKey == Key && !L.bClaimed)
		{
			Box.Add(L);
		}
	}
	for (int32 i = Mail.Num() - 1; i >= 0; --i)
	{
		if (Mail[i].ToKey == Key && Mail[i].bClaimed)
		{
			Box.Add(Mail[i]);
		}
	}
	PS->Mailbox = MoveTemp(Box);
	PS->ForceNetUpdate();
}

void AGFGameState::ServerRefreshAllMailboxes()
{
	for (APlayerState* P : PlayerArray)
	{
		if (const AGFPlayerState* GPS = Cast<AGFPlayerState>(P))
		{
			ServerRefreshMailbox(GPS->PlayerKey);
		}
	}
}

// --- Rent ---

EGFLandResult AGFGameState::ServerPayRent(FName PlotId, const FString& PayerKey, bool bAuto)
{
	FGFLandPlot* P = Land.Find(PlotId);
	if (!P)
	{
		return EGFLandResult::NoSuchPlot;
	}
	if (P->TenantKey != PayerKey || PayerKey.IsEmpty())
	{
		return EGFLandResult::NotAllowed;
	}
	FGFLandRules::Accrue(*P, LandToday());
	if (P->OwedPeriods <= 0)
	{
		return EGFLandResult::NothingOwed;
	}
	if (!P->ChargesRent())
	{
		P->OwedPeriods = 0;
		MarkLandChanged();
		return EGFLandResult::Ok;
	}
	if (Land.LandlordKey.IsEmpty())
	{
		return EGFLandResult::NotReady;
	}
	FGFInventory Inv;
	if (!Accounts().ReadAccount(PayerKey, Inv))
	{
		return EGFLandResult::NotReady;
	}
	const int32 Periods = FGFLandRules::AffordablePeriods(*P, Inv.GetCount(P->RentItem));
	if (Periods <= 0)
	{
		return EGFLandResult::CantAfford;
	}
	const FGFItemStack Payment(P->RentItem, Periods * P->RentAmount);
	const FString Text = FString::Printf(TEXT("Rent for %s: %d day%s%s"), *PlotId.ToString(), Periods, Periods == 1 ? TEXT("") : TEXT("s"), bAuto ? TEXT(" (auto-pay)") : TEXT(""));
	EGFLandResult LR;
	ServerSendLetter(EGFLetterKind::Rent, PayerKey, Land.LandlordKey, Text, { Payment }, PlotId, LR);
	if (LR != EGFLandResult::Ok)
	{
		return LR;
	}
	P = Land.Find(PlotId);
	P->OwedPeriods -= Periods;
	P->LastPaidUtc = FDateTime::UtcNow();
	P->LastPaidAmount = Payment.Count;
	MarkLandChanged();
	if (AGFPlayerState* Payer = FindPlayerByKey(PayerKey))
	{
		Payer->NotifyLand(FString::Printf(TEXT("Rent paid: %d %s"), Payment.Count, *Payment.ItemId.ToString()));
	}
	return EGFLandResult::Ok;
}

void AGFGameState::ServerUpdateRent()
{
	if (!HasAuthority())
	{
		return;
	}
	EnsureLand();
	const int64 Today = LandToday();
	bool bChanged = false;
	TArray<FName> AutoPay;
	for (FGFLandPlot& P : Land.Plots)
	{
		const int32 OldOwed = P.OwedPeriods;
		const EGFRentStatus OldStatus = P.Status;
		FGFLandRules::Accrue(P, Today);
		bChanged |= OldOwed != P.OwedPeriods || OldStatus != P.Status;
		const AGFPlayerState* Tenant = P.HasTenant() ? FindPlayerByKey(P.TenantKey) : nullptr;
		if (P.OwedPeriods > 0 && P.bAutoPay && Tenant && !Tenant->bLoggedOut && Tenant->bCharacterReady)
		{
			AutoPay.Add(P.PlotId);
		}
	}
	if (bChanged)
	{
		MarkLandChanged();
	}
	for (const FName& Id : AutoPay)
	{
		if (const FGFLandPlot* P = Land.Find(Id))
		{
			ServerPayRent(Id, P->TenantKey, true);
		}
	}
}

void AGFGameState::ServerOnPlayerReady(AGFPlayerState* PS)
{
	if (!HasAuthority() || !PS || PS->PlayerKey.IsEmpty())
	{
		return;
	}
	EnsureLand();
	if (PS->bIsHost)
	{
		ServerSetLandlord(PS->PlayerKey);
	}
	const int64 Today = LandToday();
	TArray<FName> ToPay;
	for (FGFLandPlot& P : Land.Plots)
	{
		if (P.TenantKey == PS->PlayerKey)
		{
			P.TenantName = PS->GetPlayerName();
			P.TenantColor = PS->ColorIndex;
			FGFLandRules::Accrue(P, Today); // everything missed while away, capped at 3 periods
			if (P.bAutoPay && P.OwedPeriods > 0)
			{
				ToPay.Add(P.PlotId);
			}
		}
	}
	MarkLandChanged();
	for (const FName& Id : ToPay)
	{
		ServerPayRent(Id, PS->PlayerKey, true);
	}
	ServerRefreshMailbox(PS->PlayerKey);
}

// --- Blueprints ---

bool AGFGameState::ServerRefreshBlueprintsFor(AGFPlayerState* PS)
{
	const AGFVoxelWorld* Voxel = GetVoxelWorld();
	if (!HasAuthority() || !PS || !Voxel || PS->PlayerKey.IsEmpty())
	{
		return false;
	}
	bool bChanged = false;
	for (const FGFLandPlot& P : Land.Plots)
	{
		if (P.TenantKey == PS->PlayerKey && P.Kind == EGFPlotKind::Rentable)
		{
			const int64 Hash = FGFLandRules::PlotHash(Voxel->GetGrid(), P);
			const FGFPlotBlueprint* Existing = FGFLandRules::FindBlueprint(PS->Blueprints, HostWorldId, P.PlotId);
			if (!Existing || Existing->Hash != Hash)
			{
				bChanged |= FGFLandRules::StoreBlueprint(PS->Blueprints, FGFLandRules::MakeBlueprint(Voxel->GetGrid(), P, HostWorldId, FDateTime::UtcNow()));
			}
		}
	}
	return bChanged;
}

void AGFGameState::ServerBackupPlots()
{
	if (!HasAuthority())
	{
		return;
	}
	for (APlayerState* P : PlayerArray)
	{
		AGFPlayerState* GPS = Cast<AGFPlayerState>(P);
		if (GPS && !GPS->bLoggedOut && GPS->bCharacterReady && ServerRefreshBlueprintsFor(GPS))
		{
			GPS->PushCharacterToOwner();
		}
	}
}

bool AGFGameState::HasPendingPackUp(FName PlotId) const
{
	return PendingPackUps.ContainsByPredicate([PlotId](const FGFPendingPackUp& P) { return P.PlotId == PlotId; });
}

bool AGFGameState::HasPendingRestore(FName PlotId) const
{
	return PendingRestores.ContainsByPredicate([PlotId](const FGFPendingRestore& P) { return P.PlotId == PlotId; });
}

EGFLandResult AGFGameState::ServerPackUpPlot(FName PlotId)
{
	const AGFVoxelWorld* Voxel = GetVoxelWorld();
	FGFLandPlot* P = Land.Find(PlotId);
	if (!P)
	{
		return EGFLandResult::NoSuchPlot;
	}
	if (!Voxel || P->Kind != EGFPlotKind::Rentable || !P->HasTenant())
	{
		return EGFLandResult::NotAllowed;
	}
	if (HasPendingPackUp(PlotId))
	{
		return EGFLandResult::Pending;
	}
	const FGFPlotBlueprint BP = FGFLandRules::MakeBlueprint(Voxel->GetGrid(), *P, HostWorldId, FDateTime::UtcNow());
	FGFLandRules::StoreBlueprint(PackedBlueprints, BP); // the host's copy, too
	AGFPlayerState* Tenant = FindPlayerByKey(P->TenantKey);
	if (Tenant && !Tenant->bLoggedOut && Tenant->bCharacterReady)
	{
		// Only clear once the guest has confirmed their save has the blueprint.
		FGFLandRules::StoreBlueprint(Tenant->Blueprints, BP);
		Tenant->PushCharacterToOwner();
		FGFPendingPackUp& Pending = PendingPackUps.AddDefaulted_GetRef();
		Pending.PlotId = PlotId;
		Pending.TenantKey = P->TenantKey;
		Pending.Revision = Tenant->GetCharacterRevision();
		Pending.Hash = BP.Hash;
		Tenant->NotifyLand(FString::Printf(TEXT("Your build on %s is being packed up (saved to your blueprints)"), *PlotId.ToString()));
		ServerCheckPendingPackUps();
		return HasPendingPackUp(PlotId) ? EGFLandResult::Pending : EGFLandResult::Ok;
	}
	FGFGuestRecord* Rec = GuestRecords.Find(P->TenantKey);
	if (Rec && Rec->bHasCharacter)
	{
		FGFLandRules::StoreBlueprint(Rec->LastCharacter.Blueprints, BP);
		++Rec->LastCharacter.Revision; // the host's copy wins on rejoin, so the guest gets the blueprint
	}
	FinishPackUp(PlotId);
	return EGFLandResult::Ok;
}

void AGFGameState::ServerCheckPendingPackUps()
{
	const AGFVoxelWorld* Voxel = GetVoxelWorld();
	if (!HasAuthority() || !Voxel)
	{
		return;
	}
	for (int32 i = PendingPackUps.Num() - 1; i >= 0; --i)
	{
		FGFPendingPackUp& Pending = PendingPackUps[i];
		const FGFLandPlot* P = Land.Find(Pending.PlotId);
		if (!P || P->TenantKey != Pending.TenantKey)
		{
			PendingPackUps.RemoveAt(i);
			continue;
		}
		AGFPlayerState* Tenant = FindPlayerByKey(Pending.TenantKey);
		const bool bOnline = Tenant && !Tenant->bLoggedOut;
		if (FGFLandRules::PlotHash(Voxel->GetGrid(), *P) != Pending.Hash)
		{
			// They kept building: back up again and wait for that.
			const FGFPlotBlueprint BP = FGFLandRules::MakeBlueprint(Voxel->GetGrid(), *P, HostWorldId, FDateTime::UtcNow());
			FGFLandRules::StoreBlueprint(PackedBlueprints, BP);
			Pending.Hash = BP.Hash;
			if (bOnline)
			{
				FGFLandRules::StoreBlueprint(Tenant->Blueprints, BP);
				Tenant->PushCharacterToOwner();
				Pending.Revision = Tenant->GetCharacterRevision();
				continue;
			}
			if (FGFGuestRecord* Rec = GuestRecords.Find(Pending.TenantKey))
			{
				FGFLandRules::StoreBlueprint(Rec->LastCharacter.Blueprints, BP);
				++Rec->LastCharacter.Revision;
			}
		}
		if (bOnline && Tenant->GetAckedRevision() < Pending.Revision)
		{
			continue;
		}
		// Offline: their guest record (written on logout) holds the blueprint and wins on rejoin.
		const FName PlotId = Pending.PlotId;
		PendingPackUps.RemoveAt(i);
		FinishPackUp(PlotId);
	}
}

void AGFGameState::ServerOnCharacterAcked(AGFPlayerState* PS)
{
	if (PendingPackUps.Num() > 0)
	{
		ServerCheckPendingPackUps();
	}
}

void AGFGameState::FinishPackUp(FName PlotId)
{
	FGFLandPlot* P = Land.Find(PlotId);
	AGFVoxelWorld* Voxel = GetVoxelWorld();
	if (!P || !Voxel)
	{
		return;
	}
	const FString OldTenant = P->TenantKey;
	Voxel->ClearRegion(P->Min + FIntVector(0, 0, 1), P->Max()); // keep the ground
	P->TenantKey.Reset();
	P->TenantName.Reset();
	P->TenantColor = -1;
	P->OwedPeriods = 0;
	P->bRentPaused = false;
	P->bAutoPay = true;
	PendingRestores.RemoveAll([PlotId](const FGFPendingRestore& R) { return R.PlotId == PlotId; });
	MarkLandChanged();
	if (AGFPlayerState* T = FindPlayerByKey(OldTenant))
	{
		T->NotifyLand(FString::Printf(TEXT("%s is packed up. Your build is safe in your blueprints."), *PlotId.ToString()));
	}
}

EGFLandResult AGFGameState::ApplyBlueprint(const FGFLandPlot& Plot, const FGFPlotBlueprint& Blueprint)
{
	AGFVoxelWorld* Voxel = GetVoxelWorld();
	if (!Voxel || Blueprint.Size != Plot.Size)
	{
		return EGFLandResult::NotAllowed;
	}
	return Voxel->ApplyRegion(Plot.Min, Blueprint.Voxels, Blueprint.Size) ? EGFLandResult::Ok : EGFLandResult::NoBlueprint;
}

// --- Requests ---

EGFLandResult AGFGameState::ServerHandleLandAction(AGFPlayerState* By, const FGFLandAction& A)
{
	if (!HasAuthority() || !By || By->bLoggedOut || !By->bCharacterReady || By->PlayerKey.IsEmpty())
	{
		return EGFLandResult::NotReady;
	}
	EnsureLand();
	const FString Key = By->PlayerKey;
	const bool bHost = By->bIsHost;
	const int64 Today = LandToday();
	const bool bHostOnly = A.Op == EGFLandOp::SetPlotRent || A.Op == EGFLandOp::AssignPlot || A.Op == EGFLandOp::ForgiveRent || A.Op == EGFLandOp::PauseRent
		|| A.Op == EGFLandOp::GrantBuilder || A.Op == EGFLandOp::PackUpPlot || A.Op == EGFLandOp::ApproveRestore;
	if (bHostOnly && !bHost)
	{
		return EGFLandResult::NotAllowed;
	}
	FGFLandPlot* P = A.PlotId.IsNone() ? nullptr : Land.Find(A.PlotId);
	const AGFVoxelWorld* Voxel = GetVoxelWorld();

	switch (A.Op)
	{
	case EGFLandOp::ClaimPlot:
		if (!P) return EGFLandResult::NoSuchPlot;
		if (P->Kind != EGFPlotKind::Rentable || bHost) return EGFLandResult::NotAllowed;
		if (P->HasTenant()) return P->TenantKey == Key ? EGFLandResult::AlreadyHasPlot : EGFLandResult::PlotTaken;
		if (!P->bForRent) return EGFLandResult::NotForRent;
		if (Land.CountPlotsOf(Key) > 0) return EGFLandResult::AlreadyHasPlot; // one per guest; the host can assign a second
		SetTenant(*P, Key);
		return EGFLandResult::Ok;

	case EGFLandOp::PayRent:
		return ServerPayRent(A.PlotId, Key, false);

	case EGFLandOp::SetAutoPay:
	{
		int32 N = 0;
		for (FGFLandPlot& Mine : Land.Plots)
		{
			if (Mine.TenantKey == Key && (A.PlotId.IsNone() || Mine.PlotId == A.PlotId))
			{
				Mine.bAutoPay = A.bFlag;
				++N;
			}
		}
		if (N == 0) return EGFLandResult::NoSuchPlot;
		MarkLandChanged();
		return EGFLandResult::Ok;
	}

	case EGFLandOp::SendGift:
	case EGFLandOp::SendNote:
	{
		if (A.OtherKey == Key) return EGFLandResult::NotAllowed;
		if (!IsKnownPlayer(A.OtherKey)) return EGFLandResult::NoSuchPlayer;
		const bool bGift = A.Op == EGFLandOp::SendGift;
		if (bGift && A.Stacks.Num() == 0) return EGFLandResult::InvalidStacks;
		EGFLandResult R;
		ServerSendLetter(bGift ? EGFLetterKind::Gift : EGFLetterKind::Note, Key, A.OtherKey, A.Text, bGift ? A.Stacks : TArray<FGFItemStack>(), NAME_None, R);
		return R;
	}

	case EGFLandOp::ClaimLetter:
		return ServerClaimLetter(Key, A.LetterId);

	case EGFLandOp::ClaimAllLetters:
	{
		TArray<FGuid> Ids;
		for (const FGFMailLetter& L : Mail)
		{
			if (L.ToKey == Key && !L.bClaimed)
			{
				Ids.Add(L.Id);
			}
		}
		if (Ids.Num() == 0) return EGFLandResult::NoSuchLetter;
		EGFLandResult Worst = EGFLandResult::Ok;
		for (const FGuid& Id : Ids)
		{
			const EGFLandResult R = ServerClaimLetter(Key, Id);
			if (R != EGFLandResult::Ok)
			{
				Worst = R;
			}
		}
		return Worst;
	}

	case EGFLandOp::RestoreBlueprint:
	{
		if (!P) return EGFLandResult::NoSuchPlot;
		if (!Voxel) return EGFLandResult::NotReady;
		if (P->Kind != EGFPlotKind::Rentable || (!bHost && P->TenantKey != Key)) return EGFLandResult::NotAllowed;
		if (HasPendingPackUp(P->PlotId)) return EGFLandResult::NotReady;
		const FGFPlotBlueprint* BP = FGFLandRules::FindBlueprint(By->Blueprints, A.SourceWorldId, A.SourcePlotId);
		if (!BP) return EGFLandResult::NoBlueprint;
		if (BP->Size != P->Size) return EGFLandResult::NotAllowed;
		if (FGFLandRules::IsBuiltOn(Voxel->GetGrid(), *P)) return EGFLandResult::PlotNotEmpty;
		const bool bSameSlot = A.SourceWorldId == HostWorldId && A.SourcePlotId == P->PlotId;
		if (bSameSlot)
		{
			if (bHost)
			{
				return ApplyBlueprint(*P, *BP);
			}
			// Free, but the host says OK first.
			PendingRestores.RemoveAll([&A](const FGFPendingRestore& R) { return R.PlotId == A.PlotId; });
			FGFPendingRestore& Pending = PendingRestores.AddDefaulted_GetRef();
			Pending.PlotId = A.PlotId;
			Pending.TenantKey = Key;
			Pending.SourceWorldId = A.SourceWorldId;
			Pending.SourcePlotId = A.SourcePlotId;
			if (AGFPlayerState* LandlordPS = FindPlayerByKey(Land.LandlordKey))
			{
				LandlordPS->NotifyLand(FString::Printf(TEXT("%s asks to restore their build on %s"), *By->GetPlayerName(), *A.PlotId.ToString()));
			}
			return EGFLandResult::Pending;
		}
		// Somewhere else: pay the materials again.
		const TArray<FGFItemStack> Cost = FGFLandRules::RebuildCost(*BP, [By](FName Block) { return By->ItemForBlock(Block); });
		const FGFPlotBlueprint Copy = *BP;
		const FGuid BurnId = FGuid::NewGuid();
		if (Cost.Num() > 0 && ServerBurn(BurnId, Key, Cost, TEXT("Rebuild")) != EGFTxResult::Applied)
		{
			return EGFLandResult::CantAfford;
		}
		const EGFLandResult R = ApplyBlueprint(*P, Copy);
		if (R != EGFLandResult::Ok && Cost.Num() > 0)
		{
			ServerMint(FGuid::NewGuid(), Key, Cost, TEXT("RebuildRefund"));
		}
		return R;
	}

	case EGFLandOp::SetPlotRent:
	{
		if (!P) return EGFLandResult::NoSuchPlot;
		if (P->Kind != EGFPlotKind::Rentable) return EGFLandResult::NotRentable;
		const EGFLandResult R = FGFLandRules::ValidateRent(GetItemTable(), A.ItemId, A.Amount);
		if (R != EGFLandResult::Ok) return R;
		FGFLandRules::Accrue(*P, Today); // what's owed so far stays at the old terms
		P->RentItem = A.Amount > 0 ? A.ItemId : NAME_None;
		P->RentAmount = FMath::Max(A.Amount, 0);
		P->bForRent = A.bFlag;
		P->LastAccruedDay = Today;
		if (!P->ChargesRent())
		{
			P->OwedPeriods = 0;
		}
		MarkLandChanged();
		return EGFLandResult::Ok;
	}

	case EGFLandOp::AssignPlot:
	{
		if (!P) return EGFLandResult::NoSuchPlot;
		if (P->Kind != EGFPlotKind::Rentable) return EGFLandResult::NotAllowed;
		if (A.OtherKey.IsEmpty())
		{
			// Unassign only an empty plot; a built one goes through Pack up (so nothing is lost).
			if (Voxel && FGFLandRules::IsBuiltOn(Voxel->GetGrid(), *P)) return EGFLandResult::PlotNotEmpty;
			P->TenantKey.Reset();
			P->TenantName.Reset();
			P->TenantColor = -1;
			P->OwedPeriods = 0;
			MarkLandChanged();
			return EGFLandResult::Ok;
		}
		if (A.OtherKey == Key) return EGFLandResult::NotAllowed;
		if (!IsKnownPlayer(A.OtherKey)) return EGFLandResult::NoSuchPlayer;
		if (P->HasTenant()) return P->TenantKey == A.OtherKey ? EGFLandResult::Ok : EGFLandResult::PlotTaken;
		SetTenant(*P, A.OtherKey);
		return EGFLandResult::Ok;
	}

	case EGFLandOp::ForgiveRent:
		if (!P) return EGFLandResult::NoSuchPlot;
		if (P->OwedPeriods <= 0) return EGFLandResult::NothingOwed;
		P->OwedPeriods = 0;
		P->LastAccruedDay = Today;
		MarkLandChanged();
		if (AGFPlayerState* T = FindPlayerByKey(P->TenantKey))
		{
			T->NotifyLand(TEXT("Your rent was forgiven"));
		}
		return EGFLandResult::Ok;

	case EGFLandOp::PauseRent:
		if (!P) return EGFLandResult::NoSuchPlot;
		FGFLandRules::Accrue(*P, Today);
		P->bRentPaused = A.bFlag;
		P->LastAccruedDay = Today;
		MarkLandChanged();
		return EGFLandResult::Ok;

	case EGFLandOp::GrantBuilder:
		if (A.OtherKey.IsEmpty() || A.OtherKey == Key) return EGFLandResult::NotAllowed;
		if (A.bFlag)
		{
			if (!IsKnownPlayer(A.OtherKey)) return EGFLandResult::NoSuchPlayer;
			Land.Builders.AddUnique(A.OtherKey);
		}
		else
		{
			Land.Builders.Remove(A.OtherKey);
		}
		MarkLandChanged();
		return EGFLandResult::Ok;

	case EGFLandOp::PackUpPlot:
		return ServerPackUpPlot(A.PlotId);

	case EGFLandOp::ApproveRestore:
	{
		if (!P) return EGFLandResult::NoSuchPlot;
		const int32 Idx = PendingRestores.IndexOfByPredicate([&A](const FGFPendingRestore& R) { return R.PlotId == A.PlotId; });
		if (Idx == INDEX_NONE) return EGFLandResult::NoBlueprint;
		const FGFPendingRestore Pending = PendingRestores[Idx];
		if (P->TenantKey != Pending.TenantKey)
		{
			PendingRestores.RemoveAt(Idx);
			return EGFLandResult::NotAllowed;
		}
		const FGFPlotBlueprint* BP = nullptr;
		const AGFPlayerState* Tenant = FindPlayerByKey(Pending.TenantKey);
		if (Tenant && !Tenant->bLoggedOut)
		{
			BP = FGFLandRules::FindBlueprint(Tenant->Blueprints, Pending.SourceWorldId, Pending.SourcePlotId);
		}
		else if (const FGFGuestRecord* Rec = GuestRecords.Find(Pending.TenantKey))
		{
			BP = FGFLandRules::FindBlueprint(Rec->LastCharacter.Blueprints, Pending.SourceWorldId, Pending.SourcePlotId);
		}
		if (!BP) return EGFLandResult::NoBlueprint;
		if (Voxel && FGFLandRules::IsBuiltOn(Voxel->GetGrid(), *P)) return EGFLandResult::PlotNotEmpty;
		const FGFPlotBlueprint Copy = *BP;
		PendingRestores.RemoveAt(Idx);
		const EGFLandResult R = ApplyBlueprint(*P, Copy);
		if (R == EGFLandResult::Ok)
		{
			if (AGFPlayerState* T = FindPlayerByKey(Pending.TenantKey))
			{
				T->NotifyLand(FString::Printf(TEXT("Your build on %s is back"), *A.PlotId.ToString()));
			}
		}
		return R;
	}
	}
	return EGFLandResult::NotAllowed;
}

// ---------------- Mini-game modes ----------------

EGFTxResult AGFGameState::ServerForfeit(const FGuid& TxId, const FGuid& EscrowId, FName Reason)
{
	return HasAuthority() ? LogTx(TEXT("forfeit"), Ledger.Forfeit(TxId, EscrowId, Reason)) : EGFTxResult::UnknownAccount;
}

void AGFGameState::RegisterModeHandler(UObject* Handler)
{
	IGFModeHandler* H = Cast<IGFModeHandler>(Handler);
	if (!HasAuthority() || !H)
	{
		return;
	}
	const FName Mode = H->GetModeName();
	ModeHandlers.Add(Mode, Handler);
	if (const FGFModeBlob* Blob = ModeSaveBlobs.Find(Mode))
	{
		H->RestoreModeState(Blob->Bytes);
	}
}

void AGFGameState::UnregisterModeHandler(UObject* Handler)
{
	IGFModeHandler* H = Cast<IGFModeHandler>(Handler);
	if (!H)
	{
		return;
	}
	const FName Mode = H->GetModeName();
	const TWeakObjectPtr<UObject>* Found = ModeHandlers.Find(Mode);
	if (Found && Found->Get() == Handler)
	{
		FGFModeBlob Blob;
		H->CaptureModeState(Blob.Bytes);
		if (Blob.Bytes.Num() > 0)
		{
			ModeSaveBlobs.Add(Mode, MoveTemp(Blob));
		}
		ModeHandlers.Remove(Mode);
	}
}

IGFModeHandler* AGFGameState::FindModeHandler(FName Mode) const
{
	const TWeakObjectPtr<UObject>* Found = ModeHandlers.Find(Mode);
	return Found && Found->IsValid() ? Cast<IGFModeHandler>(Found->Get()) : nullptr;
}

const FGFRecipeRow* AGFGameState::FindRecipe(FName RecipeId)
{
	const UDataTable* Recipes = FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes"));
	return Recipes && !RecipeId.IsNone() ? Recipes->FindRow<FGFRecipeRow>(RecipeId, TEXT("FindRecipe"), false) : nullptr;
}

FGFModeResult AGFGameState::ServerHandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request)
{
	if (!HasAuthority() || !By)
	{
		return FGFModeResult::Fail(TEXT("NotAuthority"));
	}
	if (Request.Mode == FName(TEXT("Player")))
	{
		if (Request.Verb == FName(TEXT("PinRecipe")))
		{
			const FName Recipe = Request.Name(0);
			if (!Recipe.IsNone() && !FindRecipe(Recipe))
			{
				return FGFModeResult::Fail(TEXT("UnknownRecipe"));
			}
			By->Progress.PinnedRecipe = Recipe;
			By->MarkCharacterChanged();
			return FGFModeResult::Ok();
		}
		if (Request.Verb == FName(TEXT("ClaimFaction")))
		{
			const FName Code = ServerClaimFaction(By, Request.Name(0));
			return Code == FName(TEXT("Ok")) ? FGFModeResult::Ok() : FGFModeResult::Fail(Code);
		}
		if (Request.Verb == FName(TEXT("WagersOff")))
		{
			By->Progress.bWagersOff = Request.Int(0) != 0;
			By->MarkCharacterChanged();
			return FGFModeResult::Ok();
		}
		return FGFModeResult::Fail(TEXT("UnknownVerb"));
	}
	IGFModeHandler* H = FindModeHandler(Request.Mode);
	return H ? H->HandleModeRequest(By, Request) : FGFModeResult::Fail(TEXT("ModeClosed"));
}

void AGFGameState::NotifyModesPlayerReady(AGFPlayerState* PS)
{
	// A rejoin within the session gets its faction back (leadership is by key, so it comes back too).
	if (HasAuthority() && PS && PS->FactionId.IsNone())
	{
		if (const FName* Faction = SessionFactions.Find(PS->PlayerKey); Faction && !Faction->IsNone())
		{
			PS->FactionId = *Faction;
			PS->ForceNetUpdate();
		}
	}
	TArray<TWeakObjectPtr<UObject>> Handlers;
	ModeHandlers.GenerateValueArray(Handlers);
	for (const TWeakObjectPtr<UObject>& W : Handlers)
	{
		if (IGFModeHandler* H = Cast<IGFModeHandler>(W.Get()))
		{
			H->OnModePlayerReady(PS);
		}
	}
}

void AGFGameState::NotifyModesPlayerLeft(AGFPlayerState* PS)
{
	TArray<TWeakObjectPtr<UObject>> Handlers;
	ModeHandlers.GenerateValueArray(Handlers);
	for (const TWeakObjectPtr<UObject>& W : Handlers)
	{
		if (IGFModeHandler* H = Cast<IGFModeHandler>(W.Get()))
		{
			H->OnModePlayerLeft(PS);
		}
	}
}

TMap<FName, FGFModeBlob> AGFGameState::GatherModeStates() const
{
	TMap<FName, FGFModeBlob> Out = ModeSaveBlobs;
	for (const TPair<FName, TWeakObjectPtr<UObject>>& Pair : ModeHandlers)
	{
		if (const IGFModeHandler* H = Cast<IGFModeHandler>(Pair.Value.Get()))
		{
			FGFModeBlob Blob;
			H->CaptureModeState(Blob.Bytes);
			if (Blob.Bytes.Num() > 0)
			{
				Out.Add(Pair.Key, MoveTemp(Blob));
			}
		}
	}
	return Out;
}

void AGFGameState::RestoreModeStates(const TMap<FName, FGFModeBlob>& Blobs)
{
	ModeSaveBlobs = Blobs;
	for (const TPair<FName, TWeakObjectPtr<UObject>>& Pair : ModeHandlers)
	{
		IGFModeHandler* H = Cast<IGFModeHandler>(Pair.Value.Get());
		const FGFModeBlob* Blob = ModeSaveBlobs.Find(Pair.Key);
		if (H && Blob)
		{
			H->RestoreModeState(Blob->Bytes);
		}
	}
}
