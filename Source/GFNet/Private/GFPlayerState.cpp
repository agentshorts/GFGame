#include "GFPlayerState.h"
#include "GFGameState.h"
#include "GFLocalProfileSubsystem.h"
#include "GFInventoryComponent.h"
#include "GFVoxelWorld.h"
#include "GFTamingComponent.h"
#include "GameFramework/Character.h"
#include "GFLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AGFPlayerState::AGFPlayerState()
{
	Inventory = CreateDefaultSubobject<UGFInventoryComponent>(TEXT("Inventory"));
	SetNetUpdateFrequency(10.f);
}

void AGFPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFPlayerState, PlayerKey);
	DOREPLIFETIME(AGFPlayerState, bIsHost);
	DOREPLIFETIME(AGFPlayerState, ColorIndex);
	DOREPLIFETIME(AGFPlayerState, FactionId);
	DOREPLIFETIME(AGFPlayerState, HeldItem);
	DOREPLIFETIME(AGFPlayerState, bCharacterReady);
	DOREPLIFETIME_CONDITION(AGFPlayerState, Companions, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AGFPlayerState, Journal, COND_OwnerOnly);
	DOREPLIFETIME(AGFPlayerState, bShareJournal);
	DOREPLIFETIME_CONDITION(AGFPlayerState, Mailbox, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AGFPlayerState, Progress, COND_OwnerOnly);
}

void AGFPlayerState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && Inventory)
	{
		Inventory->OnInventoryChanged.AddDynamic(this, &AGFPlayerState::HandleInventoryChanged);
	}
	UploadLocalCharacterIfNeeded();
}

void AGFPlayerState::OnRep_Owner()
{
	Super::OnRep_Owner();
	UploadLocalCharacterIfNeeded();
}

AGFGameState* AGFPlayerState::GetGFGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
}

AGFVoxelWorld* AGFPlayerState::GetVoxelWorld() const
{
	const AGFGameState* GS = GetGFGameState();
	return GS ? GS->GetVoxelWorld() : nullptr;
}

bool AGFPlayerState::IsLocallyOwned() const
{
	const APlayerController* PC = GetPlayerController();
	return PC && PC->IsLocalController();
}

// ---------------- Character upload / write-back ----------------

void AGFPlayerState::UploadLocalCharacterIfNeeded()
{
	if (bUploadedLocal || !HasActorBegunPlay() || !IsLocallyOwned() || (HasAuthority() && PlayerKey.IsEmpty()))
	{
		return; // on the server, wait for PostLogin to set the key
	}
	UGameInstance* GI = GetGameInstance();
	UGFLocalProfileSubsystem* Profile = GI ? GI->GetSubsystem<UGFLocalProfileSubsystem>() : nullptr;
	if (!Profile)
	{
		return;
	}
	bUploadedLocal = true;
	if (HasAuthority())
	{
		ServerAcceptCharacter(Profile->GetCharacter());
	}
	else
	{
		ServerUploadCharacter(Profile->GetCharacter());
	}
}

void AGFPlayerState::ServerUploadCharacter_Implementation(const FGFCharacterData& Data)
{
	ServerAcceptCharacter(Data);
}

void AGFPlayerState::ServerAcceptCharacter(FGFCharacterData Data)
{
	if (!HasAuthority() || bCharacterReady)
	{
		return; // one upload per session: a second upload can't re-add spent items
	}
	if (AGFGameState* GS = GetGFGameState())
	{
		const FGFGuestRecord* Rec = GS->GuestRecords.Find(PlayerKey);
		if (Rec && Rec->bHasCharacter && Rec->LastCharacter.Revision >= Data.Revision)
		{
			UE_LOG(LogGF, Log, TEXT("Net: %s rejoined; using the host's copy (rev %d >= uploaded %d)"), *PlayerKey, Rec->LastCharacter.Revision, Data.Revision);
			Data = Rec->LastCharacter;
		}
	}
	if (!Data.bInitialized)
	{
		Data.Inventory = FGFInventory();
		for (const FGFItemStack& S : AGFGameState::StarterKit())
		{
			Data.Inventory.Add(S.ItemId, S.Count);
		}
		Data.bInitialized = true;
	}
	CharacterRevision = Data.Revision;
	ApplyCharacterData(Data);
	bCharacterReady = true;
	ForceNetUpdate();
	PushCharacterToOwner();
	if (AGFGameState* GS = GetGFGameState())
	{
		GS->ServerOnPlayerReady(this); // rent owed while away is collected now, mailbox filled
		GS->NotifyModesPlayerReady(this);
	}
}

FGFCharacterData AGFPlayerState::BuildCharacterData() const
{
	FGFCharacterData Data;
	Data.PlayerKey = PlayerKey;
	Data.DisplayName = GetPlayerName();
	Data.Revision = CharacterRevision;
	Data.bInitialized = true;
	Data.Inventory = Inventory ? Inventory->GetInventory() : FGFInventory();
	Data.Companions = Companions;
	Data.Journal = Journal;
	Data.Blueprints = Blueprints;
	Data.Progress = Progress;
	return Data;
}

void AGFPlayerState::ApplyCharacterData(const FGFCharacterData& Data)
{
	if (!HasAuthority())
	{
		return;
	}
	Inventory->SetInventory(Data.Inventory);
	Companions = Data.Companions;
	Journal = Data.Journal;
	Blueprints = Data.Blueprints;
	Progress = Data.Progress;
	if (Companions.Num() > 0)
	{
		Progress.bHasTamed = true; // characters from before the flag existed
	}
}

void AGFPlayerState::PushCharacterToOwner()
{
	if (!HasAuthority() || !bCharacterReady)
	{
		return;
	}
	++CharacterRevision;
	const FGFCharacterData Data = BuildCharacterData();
	if (IsLocallyOwned())
	{
		if (UGFLocalProfileSubsystem* Profile = GetGameInstance()->GetSubsystem<UGFLocalProfileSubsystem>())
		{
			Profile->StoreFromServer(Data);
		}
		AckedRevision = CharacterRevision;
		if (AGFGameState* GS = GetGFGameState())
		{
			GS->ServerOnCharacterAcked(this);
		}
	}
	else
	{
		ClientStoreCharacter(Data);
	}
}

void AGFPlayerState::ServerAckCharacter_Implementation(int32 Revision)
{
	if (Revision > AckedRevision && Revision <= CharacterRevision)
	{
		AckedRevision = Revision;
		if (AGFGameState* GS = GetGFGameState())
		{
			GS->ServerOnCharacterAcked(this);
		}
	}
}

int32 AGFPlayerState::RequestLandAction(FGFLandAction Action)
{
	Action.RequestId = NextLandRequestId++;
	if (HasAuthority())
	{
		ServerLandAction_Implementation(Action);
	}
	else
	{
		ServerLandAction(Action);
	}
	return Action.RequestId;
}

void AGFPlayerState::ServerLandAction_Implementation(const FGFLandAction& Action)
{
	AGFGameState* GS = GetGFGameState();
	const EGFLandResult R = GS ? GS->ServerHandleLandAction(this, Action) : EGFLandResult::NotReady;
	if (R != EGFLandResult::Ok && R != EGFLandResult::Pending)
	{
		UE_LOG(LogGF, Verbose, TEXT("Land: %s %s -> %s"), *PlayerKey, *UEnum::GetValueAsString(Action.Op), *UEnum::GetValueAsString(R));
	}
	if (IsLocallyOwned())
	{
		LandResults.Add(Action.RequestId, R);
	}
	else
	{
		ClientLandResult(Action.RequestId, R);
	}
}

int32 AGFPlayerState::RequestMode(FGFModeRequest Request)
{
	Request.RequestId = NextModeRequestId++;
	if (HasAuthority())
	{
		ServerModeRequest_Implementation(Request);
	}
	else
	{
		ServerModeRequest(Request);
	}
	return Request.RequestId;
}

void AGFPlayerState::ServerModeRequest_Implementation(const FGFModeRequest& Request)
{
	AGFGameState* GS = GetGFGameState();
	FGFModeResult R = GS && bCharacterReady ? GS->ServerHandleModeRequest(this, Request) : FGFModeResult::Fail(TEXT("NotReady"));
	R.RequestId = Request.RequestId;
	R.Mode = Request.Mode;
	R.Verb = Request.Verb;
	if (!R.bOk)
	{
		UE_LOG(LogGF, Verbose, TEXT("Mode: %s %s.%s -> %s"), *PlayerKey, *Request.Mode.ToString(), *Request.Verb.ToString(), *R.Code.ToString());
	}
	if (IsLocallyOwned())
	{
		ModeResults.Add(R.RequestId, R);
	}
	else
	{
		ClientModeResult(R);
	}
}

void AGFPlayerState::ClientModeResult_Implementation(const FGFModeResult& Result)
{
	ModeResults.Add(Result.RequestId, Result);
}

void AGFPlayerState::RequestChat(const FString& Text)
{
	if (HasAuthority())
	{
		ServerChat_Implementation(Text);
	}
	else
	{
		ServerChat(Text);
	}
}

void AGFPlayerState::ServerChat_Implementation(const FString& Text)
{
	AGFGameState* GS = GetGFGameState();
	const FName Code = GS ? GS->ServerHandleChat(this, Text) : FName(TEXT("NotReady"));
	if (IsLocallyOwned())
	{
		ChatResults.Add(Code);
	}
	else
	{
		ClientChatResult(Code);
	}
}

void AGFPlayerState::ClientChatResult_Implementation(FName Code)
{
	ChatResults.Add(Code);
}

void AGFPlayerState::MarkCharacterChanged()
{
	if (HasAuthority())
	{
		ForceNetUpdate();
		ScheduleWriteBack();
	}
}

void AGFPlayerState::ClientLandResult_Implementation(int32 RequestId, EGFLandResult Result)
{
	LandResults.Add(RequestId, Result);
}

void AGFPlayerState::NotifyLand(const FString& Notice)
{
	if (!HasAuthority() || bLoggedOut)
	{
		return;
	}
	if (IsLocallyOwned())
	{
		LandNotices.Add(Notice);
	}
	else
	{
		ClientLandNotice(Notice);
	}
}

void AGFPlayerState::ClientLandNotice_Implementation(const FString& Notice)
{
	LandNotices.Add(Notice);
}

void AGFPlayerState::ClientStoreCharacter_Implementation(const FGFCharacterData& Data)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGFLocalProfileSubsystem* Profile = GI->GetSubsystem<UGFLocalProfileSubsystem>())
		{
			Profile->StoreFromServer(Data);
		}
	}
	ServerAckCharacter(Data.Revision); // pack-ups wait for this before clearing a plot
}

void AGFPlayerState::SnapshotToGuestRecord()
{
	AGFGameState* GS = GetGFGameState();
	if (!HasAuthority() || !GS || PlayerKey.IsEmpty())
	{
		return;
	}
	if (bCharacterReady && !bLoggedOut)
	{
		GS->NotifyModesPlayerLeft(this); // wagers refunded, crews walk home, unbanked run loot handled
	}
	FGFGuestRecord& Rec = GS->GuestRecords.FindOrAdd(PlayerKey);
	Rec.PlayerKey = PlayerKey;
	Rec.DisplayName = GetPlayerName();
	Rec.ColorIndex = ColorIndex;
	Rec.LastSeen = FDateTime::UtcNow();
	if (bCharacterReady)
	{
		GS->ServerRefreshBlueprintsFor(this); // leaving = a fresh backup of their plot
		++CharacterRevision; // newer than anything the client could have been sent
		Rec.bHasCharacter = true;
		Rec.LastCharacter = BuildCharacterData();
	}
	bLoggedOut = true; // from now on the ledger uses the guest record, not this leaving player state
}

FGuid AGFPlayerState::RequestTransfer(const FString& ToKey, const TArray<FGFItemStack>& Stacks)
{
	const FGuid TxId = FGuid::NewGuid();
	SendTransfer(TxId, ToKey, Stacks);
	return TxId;
}

void AGFPlayerState::SendTransfer(const FGuid& TxId, const FString& ToKey, const TArray<FGFItemStack>& Stacks)
{
	if (HasAuthority())
	{
		ServerTransferTo_Implementation(TxId, ToKey, Stacks);
	}
	else
	{
		ServerTransferTo(TxId, ToKey, Stacks);
	}
}

void AGFPlayerState::ServerTransferTo_Implementation(FGuid TxId, const FString& ToKey, const TArray<FGFItemStack>& Stacks)
{
	AGFGameState* GS = GetGFGameState();
	// A player can only ever spend from their own account.
	const EGFTxResult R = GS ? GS->ServerTransfer(TxId, PlayerKey, ToKey, Stacks, TEXT("Gift")) : EGFTxResult::UnknownAccount;
	if (IsLocallyOwned())
	{
		TransferResults.Add(TxId, R);
	}
	else
	{
		ClientTransferResult(TxId, R);
	}
}

void AGFPlayerState::ClientTransferResult_Implementation(FGuid TxId, EGFTxResult Result)
{
	TransferResults.Add(TxId, Result);
}

void AGFPlayerState::HandleInventoryChanged(FName, int32)
{
	ScheduleWriteBack();
}

void AGFPlayerState::ScheduleWriteBack()
{
	if (bWriteBackScheduled || !bCharacterReady || !GetWorld())
	{
		return;
	}
	bWriteBackScheduled = true;
	GetWorldTimerManager().SetTimer(WriteBackTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		bWriteBackScheduled = false;
		PushCharacterToOwner();
	}), 0.5f, false);
}

// ---------------- Held item ----------------

void AGFPlayerState::RequestSetHeldItem(FName ItemId)
{
	if (HasAuthority())
	{
		ServerSetHeldItem_Implementation(ItemId);
	}
	else
	{
		ServerSetHeldItem(ItemId);
	}
}

void AGFPlayerState::ServerSetHeldItem_Implementation(FName ItemId)
{
	// Can only hold what you have.
	HeldItem = (ItemId.IsNone() || Inventory->GetCount(ItemId) > 0) ? ItemId : FName();
	ForceNetUpdate();
}

// ---------------- Block edits ----------------

FName AGFPlayerState::ItemForBlock(FName BlockId) const
{
	const AGFVoxelWorld* Voxel = GetVoxelWorld();
	const FGFBlockRow* Row = Voxel ? Voxel->FindBlockRow(BlockId) : nullptr;
	return (Row && !Row->DropItem.IsNone()) ? Row->DropItem : BlockId;
}

int32 AGFPlayerState::RequestEdit(FIntVector Cell, FName BlockId)
{
	const int32 EditId = NextEditId++;
	if (HasAuthority())
	{
		ServerHandleEdit(Cell, BlockId);
		return EditId;
	}
	if (AGFVoxelWorld* Voxel = GetVoxelWorld())
	{
		Voxel->PredictEdit(EditId, Cell, BlockId);
	}
	ServerRequestEdit(Cell, BlockId, EditId);
	return EditId;
}

void AGFPlayerState::ServerRequestEdit_Implementation(FIntVector Cell, FName BlockId, int32 EditId)
{
	int32 Revision = 0;
	const bool bOk = ServerHandleEdit(Cell, BlockId, nullptr, &Revision);
	ClientEditResult(EditId, bOk, Revision);
}

void AGFPlayerState::ClientEditResult_Implementation(int32 EditId, bool bAccepted, int32 ChunkRevision)
{
	if (AGFVoxelWorld* Voxel = GetVoxelWorld())
	{
		Voxel->ResolvePrediction(EditId, bAccepted, ChunkRevision);
	}
}

bool AGFPlayerState::ValidateEdit(const FIntVector& Cell, FName BlockId, FString& OutReason) const
{
	const AGFGameState* GS = GetGFGameState();
	const AGFVoxelWorld* Voxel = GetVoxelWorld();
	if (!GS || !Voxel)
	{
		OutReason = TEXT("no world");
		return false;
	}
	if (!bCharacterReady)
	{
		OutReason = TEXT("character not loaded");
		return false;
	}
	if (!Voxel->GetGrid().IsInBounds(Cell))
	{
		OutReason = TEXT("out of bounds");
		return false;
	}
	if (GS->MaxEditReach > 0.f)
	{
		const APawn* Pawn = GetPawn();
		if (!Pawn || FVector::Dist(Pawn->GetActorLocation(), Voxel->CellToWorld(Cell)) > GS->MaxEditReach)
		{
			OutReason = TEXT("out of reach");
			return false;
		}
	}
	if (GS->bEnforcePlotPermissions && !FGFLandRules::CanEdit(GS->Land, PlayerKey, bIsHost, Cell, !BlockId.IsNone(), OutReason))
	{
		return false;
	}
	if (!BlockId.IsNone() && !GS->bFreeBuild && Inventory->GetCount(ItemForBlock(BlockId)) < 1)
	{
		OutReason = TEXT("no block item");
		return false;
	}
	return true;
}

bool AGFPlayerState::ServerHandleEdit(const FIntVector& Cell, FName BlockId, FString* OutReason, int32* OutChunkRevision)
{
	FString Reason;
	AGFVoxelWorld* Voxel = GetVoxelWorld();
	FName Previous;
	bool bOk = HasAuthority() && ValidateEdit(Cell, BlockId, Reason) && Voxel->ServerApplyEdit(Cell, BlockId, &Previous, &Reason);
	if (bOk)
	{
		++EditsAccepted;
		if (!GetGFGameState()->bFreeBuild)
		{
			if (BlockId.IsNone())
			{
				Inventory->AddItem(ItemForBlock(Previous), 1);
			}
			else
			{
				Inventory->RemoveItem(ItemForBlock(BlockId), 1);
			}
		}
	}
	else
	{
		++EditsRejected;
	}
	if (OutReason)
	{
		*OutReason = Reason;
	}
	if (OutChunkRevision && Voxel)
	{
		*OutChunkRevision = Voxel->GetChunkRevision(FGFVoxelGrid::CellToChunk(Cell));
	}
	return bOk;
}

// ---------------- Taming ----------------

bool AGFPlayerState::IsTamerCrouching() const
{
	const ACharacter* C = Cast<ACharacter>(GetPawn());
	return C && C->bIsCrouched;
}

void AGFPlayerState::JournalChanged()
{
	ForceNetUpdate();
	ScheduleWriteBack();
}

void AGFPlayerState::RequestSetShareJournal(bool bShare)
{
	if (HasAuthority())
	{
		ServerSetShareJournal_Implementation(bShare);
	}
	else
	{
		ServerSetShareJournal(bShare);
	}
}

void AGFPlayerState::ServerSetShareJournal_Implementation(bool bShare)
{
	bShareJournal = bShare;
}

void AGFPlayerState::RequestUseCalmingMist(AActor* Dino)
{
	if (HasAuthority())
	{
		MistResults.Add(ServerHandleMist(Dino));
	}
	else
	{
		ServerUseCalmingMist(Dino);
	}
}

void AGFPlayerState::ServerUseCalmingMist_Implementation(AActor* Dino)
{
	ClientMistResult(ServerHandleMist(Dino));
}

void AGFPlayerState::ClientMistResult_Implementation(bool bOk)
{
	MistResults.Add(bOk);
}

bool AGFPlayerState::ServerHandleMist(AActor* Dino)
{
	UGFTamingComponent* Taming = Dino ? Dino->FindComponentByClass<UGFTamingComponent>() : nullptr;
	AGFGameState* GS = GetGFGameState();
	if (!HasAuthority() || !Taming || !GS || !bCharacterReady || Inventory->GetCount(GFLabItems::CalmingMist) < 1)
	{
		return false;
	}
	if (!Taming->ServerCalm(this))
	{
		return false; // out of reach, tamed or fled: the mist isn't used
	}
	GS->ServerBurn(FGuid::NewGuid(), PlayerKey, { FGFItemStack(GFLabItems::CalmingMist, 1) }, TEXT("CalmingMist"));
	return true;
}

void AGFPlayerState::RequestOfferFood(AActor* Dino)
{
	if (HasAuthority())
	{
		OfferResults.Add(ServerHandleOffer(Dino));
	}
	else
	{
		ServerOfferFood(Dino);
	}
}

void AGFPlayerState::ServerOfferFood_Implementation(AActor* Dino)
{
	ClientOfferResult(ServerHandleOffer(Dino));
}

void AGFPlayerState::ClientOfferResult_Implementation(EGFOfferResult Result)
{
	OfferResults.Add(Result);
}

EGFOfferResult AGFPlayerState::ServerHandleOffer(AActor* Dino)
{
	UGFTamingComponent* Taming = Dino ? Dino->FindComponentByClass<UGFTamingComponent>() : nullptr;
	AGFGameState* GS = GetGFGameState();
	if (!HasAuthority() || !Taming || !GS || !bCharacterReady)
	{
		return EGFOfferResult::NotTameable;
	}
	const FName Food = HeldItem;
	if (Food.IsNone() || Inventory->GetCount(Food) < 1)
	{
		return EGFOfferResult::NotNow; // nothing in hand
	}
	const bool bWasTamed = Taming->GetTameState() == EGFTameState::Tamed;
	const EGFOfferResult Result = Taming->ServerOffer(this, Food);
	if (Result == EGFOfferResult::Accepted || Result == EGFOfferResult::Favourite)
	{
		GS->ServerBurn(FGuid::NewGuid(), PlayerKey, { FGFItemStack(Food, 1) }, TEXT("Feed"));
	}
	if (!bWasTamed && Taming->GetTameState() == EGFTameState::Tamed)
	{
		// The starter owns the tame, even when a helper's offer finished it.
		for (APlayerState* Other : GS->PlayerArray)
		{
			AGFPlayerState* OwnerPS = Cast<AGFPlayerState>(Other);
			if (OwnerPS && OwnerPS->GetTamerKey() == Taming->GetOwnerKey())
			{
				Taming->CompanionId = OwnerPS->ServerAddCompanion(Taming->Species);
				break;
			}
		}
		UE_LOG(LogGF, Log, TEXT("Taming: %s tamed by %s"), *Taming->Species.ToString(), *Taming->GetOwnerKey());
	}
	return Result;
}

FGuid AGFPlayerState::ServerAddCompanion(FName Species)
{
	if (!HasAuthority())
	{
		return FGuid();
	}
	const FGuid Id = FGFCompanions::AddTamed(Companions, Species, FString());
	Progress.bHasTamed = true; // first tame opens the Mine Mouth
	ForceNetUpdate();
	ScheduleWriteBack();
	return Id;
}
