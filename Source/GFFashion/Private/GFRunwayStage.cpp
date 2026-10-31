#include "GFRunwayStage.h"
#include "GFRunwaySite.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

namespace GFRunwayStage
{
	static bool Paid(EGFTxResult R)
	{
		return R == EGFTxResult::Applied || R == EGFTxResult::AlreadyApplied;
	}
}

AGFRunwayStage::AGFRunwayStage()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(10.f);
}

AGFRunwayStage* AGFRunwayStage::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFRunwayStage> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

void AGFRunwayStage::EnsurePlayableSite()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	AGFRunwaySite* Site = AGFRunwaySite::Get(World);
	const bool bNew = Site == nullptr;
	if (bNew)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Face with the stage. A site left on world +X hides the start arch when this actor is turned.
		const FTransform StageXf(GetActorRotation(), GetActorLocation());
		Site = World->SpawnActor<AGFRunwaySite>(AGFRunwaySite::StaticClass(), StageXf, Params);
	}
	if (!Site)
	{
		return;
	}
	if (bNew)
	{
		UE_LOG(LogGF, Log, TEXT("Runway: playable site on the stage (wardrobe, labelled start arch, finish, hub)"));
	}
	Site->SetActorLocationAndRotation(GetActorLocation(), GetActorRotation());
	Site->BuildIfNeeded();
}

void AGFRunwayStage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFRunwayStage, State);
}

void AGFRunwayStage::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		EnsureLinks();
	}
}

void AGFRunwayStage::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority())
	{
		for (const FGFRunwayPendingReward& P : PendingRewards)
		{
			UE_LOG(LogGF, Warning, TEXT("Runway: reward %s for %s was never paid (ledger refused until the stage closed)."), *P.TxId.ToString(), *P.PlayerKey);
		}
		for (const TPair<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<AGFRunwayPlayerLink>>& L : Links)
		{
			if (AGFRunwayPlayerLink* Link = L.Value.Get())
			{
				Link->Destroy();
			}
		}
		Links.Reset();
	}
	Super::EndPlay(Reason);
}

void AGFRunwayStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	LinkTimer -= DeltaSeconds;
	if (LinkTimer <= 0.f)
	{
		LinkTimer = 0.25f;
		EnsureLinks();
	}
	Step(DeltaSeconds);
}

void AGFRunwayStage::EnsureLinks()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	for (auto It = Links.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			if (AGFRunwayPlayerLink* Link = It->Value.Get())
			{
				Link->Destroy();
			}
			It.RemoveCurrent();
		}
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		const TWeakObjectPtr<AGFRunwayPlayerLink>* Existing = Links.Find(PC);
		if (Existing && Existing->IsValid())
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.Owner = PC;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		if (AGFRunwayPlayerLink* Link = World->SpawnActor<AGFRunwayPlayerLink>(AGFRunwayPlayerLink::StaticClass(), FTransform::Identity, Params))
		{
			Links.Add(PC, Link);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Ledger access: the game state's (ServerMint / ServerBurn) or a test override. Never anything else.

bool AGFRunwayStage::LedgerReady() const
{
	if (LedgerOverride && AccountsOverride)
	{
		return true;
	}
	const AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	return GS && GS->HasAuthority();
}

bool AGFRunwayStage::ReadAccount(const FString& Key, FGFInventory& Out) const
{
	if (LedgerOverride && AccountsOverride)
	{
		return AccountsOverride->ReadAccount(Key, Out);
	}
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	return GS && GS->HasAuthority() && GS->Accounts().ReadAccount(Key, Out);
}

bool AGFRunwayStage::WasApplied(const FGuid& TxId) const
{
	if (LedgerOverride && AccountsOverride)
	{
		return LedgerOverride->WasApplied(TxId);
	}
	const AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	return GS && GS->Ledger.WasApplied(TxId);
}

EGFTxResult AGFRunwayStage::Mint(const FGuid& TxId, const FString& To, const TArray<FGFItemStack>& Stacks)
{
	if (LedgerOverride && AccountsOverride)
	{
		return LedgerOverride->Mint(*AccountsOverride, TxId, To, Stacks, TEXT("Fashion.Runway"));
	}
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	return GS && GS->HasAuthority() ? GS->ServerMint(TxId, To, Stacks, TEXT("Fashion.Runway")) : EGFTxResult::UnknownAccount;
}

EGFTxResult AGFRunwayStage::Burn(const FGuid& TxId, const FString& From, const TArray<FGFItemStack>& Stacks)
{
	if (LedgerOverride && AccountsOverride)
	{
		return LedgerOverride->Burn(*AccountsOverride, TxId, From, Stacks, TEXT("Fashion.Dye"));
	}
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	return GS && GS->HasAuthority() ? GS->ServerBurn(TxId, From, Stacks, TEXT("Fashion.Dye")) : EGFTxResult::UnknownAccount;
}

// ------------------------------------------------------------------------------------------------

EGFRunwayRequestResult AGFRunwayStage::StartShow(FName ThemeId)
{
	if (!HasAuthority())
	{
		return EGFRunwayRequestResult::NotAuthority;
	}
	TArray<FString> Keys;
	if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const AGFPlayerState* GFPS = Cast<AGFPlayerState>(PS);
			if (GFPS && GFPS->bCharacterReady && !GFPS->PlayerKey.IsEmpty())
			{
				Keys.AddUnique(GFPS->PlayerKey);
			}
		}
	}
	return StartShowFor(ThemeId, Keys, static_cast<int32>(FPlatformTime::Cycles()));
}

EGFRunwayRequestResult AGFRunwayStage::StartShowFor(FName ThemeId, const TArray<FString>& PlayerKeys, int32 Seed)
{
	if (!HasAuthority())
	{
		return EGFRunwayRequestResult::NotAuthority;
	}
	if (Show.GetPhase() != EGFRunwayPhase::Idle && Show.GetPhase() != EGFRunwayPhase::Results)
	{
		return EGFRunwayRequestResult::WrongPhase;
	}
	if (!FGFFashionCatalog::FindTheme(ThemeId))
	{
		return EGFRunwayRequestResult::UnknownTheme;
	}
	if (PlayerKeys.Num() == 0)
	{
		return EGFRunwayRequestResult::NoPlayers;
	}
	Show.DressingSeconds = DressingSeconds;
	Show.WalkSeconds = WalkSeconds;
	Show.JudgingSeconds = JudgingSeconds;
	if (!Show.Start(FGuid::NewGuid(), ThemeId, Seed, PlayerKeys))
	{
		return EGFRunwayRequestResult::WrongPhase;
	}
	for (const FString& Key : PlayerKeys)
	{
		if (const TArray<FGFOutfit>* Past = Looks.Find(Key))
		{
			Show.SetHistory(Key, *Past);
		}
	}
	bPaid = false;
	SyncState();
	return EGFRunwayRequestResult::Ok;
}

EGFRunwayRequestResult AGFRunwayStage::HandleSetOutfit(const FString& PlayerKey, const FGFOutfit& Outfit)
{
	if (!HasAuthority())
	{
		return EGFRunwayRequestResult::NotAuthority;
	}
	if (Show.GetPhase() != EGFRunwayPhase::Dressing)
	{
		return EGFRunwayRequestResult::WrongPhase;
	}
	if (PlayerKey.IsEmpty() || !Show.FindContestant(PlayerKey))
	{
		return EGFRunwayRequestResult::NotInShow;
	}
	FString Why;
	if (!Outfit.Validate(&Why))
	{
		UE_LOG(LogGF, Log, TEXT("Runway: refused outfit from %s: %s"), *PlayerKey, *Why);
		return EGFRunwayRequestResult::InvalidOutfit;
	}

	// Dyes: each dyed slot is charged once per show and colour (TxId show + player + slot + dye + colour), so a resend
	// is free but re-dyeing a slot a new colour charges again. Check the whole bill first so a player who can't
	// afford it all pays nothing and keeps the old outfit.
	struct FCharge
	{
		EGFOutfitSlot Slot;
		FGuid TxId;
		TArray<FGFItemStack> Cost;
	};
	TArray<FCharge> Charges;
	FGFInventory Bill;
	for (int32 i = 0; i < static_cast<int32>(EGFOutfitSlot::Count); ++i)
	{
		const EGFOutfitSlot Slot = static_cast<EGFOutfitSlot>(i);
		TArray<FGFItemStack> Cost = Outfit.DyeCostForSlot(Slot);
		if (Cost.Num() == 0)
		{
			continue;
		}
		const FGuid TxId = Show.DyeTxId(PlayerKey, Slot, Outfit.Slots[i].DyeColor);
		if (WasApplied(TxId))
		{
			continue; // already paid for this colour on this slot in this show
		}
		for (const FGFItemStack& S : Cost)
		{
			Bill.Add(S.ItemId, S.Count);
		}
		Charges.Add({ Slot, TxId, MoveTemp(Cost) });
	}
	FGFOutfit Final = Outfit;
	if (Charges.Num() > 0)
	{
		FGFInventory Account;
		if (!LedgerReady() || !ReadAccount(PlayerKey, Account))
		{
			return EGFRunwayRequestResult::NoAccount;
		}
		for (const FGFItemStack& S : Bill.Items)
		{
			if (!Account.Has(S.ItemId, S.Count))
			{
				return EGFRunwayRequestResult::NotEnoughDye;
			}
		}
		for (const FCharge& C : Charges)
		{
			const EGFTxResult R = Burn(C.TxId, PlayerKey, C.Cost);
			if (!GFRunwayStage::Paid(R))
			{
				// Shouldn't happen after the check above; never show a dye that wasn't paid for.
				UE_LOG(LogGF, Warning, TEXT("Runway: dye burn for %s slot %d failed (%d); dye removed."), *PlayerKey, static_cast<int32>(C.Slot), static_cast<int32>(R));
				Final.ClearDye(C.Slot);
			}
		}
	}
	if (!Show.SetOutfit(PlayerKey, Final))
	{
		return EGFRunwayRequestResult::InvalidOutfit;
	}
	SyncState();
	return EGFRunwayRequestResult::Ok;
}

EGFRunwayRequestResult AGFRunwayStage::HandleSetReady(const FString& PlayerKey, bool bReady)
{
	if (!HasAuthority())
	{
		return EGFRunwayRequestResult::NotAuthority;
	}
	if (Show.GetPhase() != EGFRunwayPhase::Dressing)
	{
		return EGFRunwayRequestResult::WrongPhase;
	}
	if (PlayerKey.IsEmpty() || !Show.SetReady(PlayerKey, bReady))
	{
		return EGFRunwayRequestResult::NotInShow;
	}
	SyncState();
	return EGFRunwayRequestResult::Ok;
}

void AGFRunwayStage::Step(float DeltaSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	Show.Advance(DeltaSeconds);
	if (Show.GetPhase() == EGFRunwayPhase::Results && !bPaid)
	{
		PayOut();
	}
	RetryTimer -= DeltaSeconds;
	if (PendingRewards.Num() > 0 && RetryTimer <= 0.f)
	{
		RetryTimer = 1.f;
		RetryPendingRewards();
	}
	SyncState();
}

void AGFRunwayStage::PayOut()
{
	bPaid = true;
	for (const FGFRunwayContestant& C : Show.GetContestants())
	{
		Looks.FindOrAdd(C.PlayerKey).Add(C.Outfit);
		if (C.Rewards.Num() == 0)
		{
			continue;
		}
		FGFRunwayPendingReward P;
		P.TxId = Show.RewardTxId(C.PlayerKey);
		P.PlayerKey = C.PlayerKey;
		P.Stacks = C.Rewards;
		const EGFTxResult R = Mint(P.TxId, P.PlayerKey, P.Stacks);
		if (R == EGFTxResult::InvalidStacks)
		{
			UE_LOG(LogGF, Error, TEXT("Runway: reward for %s has invalid stacks; dropped."), *C.PlayerKey);
		}
		else if (!GFRunwayStage::Paid(R))
		{
			PendingRewards.Add(MoveTemp(P)); // same TxId on every retry
		}
	}
}

int32 AGFRunwayStage::RetryPendingRewards()
{
	if (!HasAuthority())
	{
		return 0;
	}
	int32 Paid = 0;
	for (int32 i = PendingRewards.Num() - 1; i >= 0; --i)
	{
		const FGFRunwayPendingReward& P = PendingRewards[i];
		const EGFTxResult R = Mint(P.TxId, P.PlayerKey, P.Stacks);
		if (R == EGFTxResult::Applied)
		{
			++Paid;
		}
		if (GFRunwayStage::Paid(R) || R == EGFTxResult::InvalidStacks)
		{
			PendingRewards.RemoveAt(i);
		}
	}
	return Paid;
}

void AGFRunwayStage::SyncState()
{
	const EGFRunwayPhase OldPhase = State.Phase;
	const int32 OldSeconds = State.PhaseSecondsLeft;
	State.Phase = Show.GetPhase();
	State.ThemeId = Show.GetThemeId();
	State.ShowId = Show.GetShowId();
	State.PhaseSecondsLeft = FMath::Max(0, FMath::CeilToInt32(Show.GetPhaseTimeLeft()));
	State.WalkerIndex = Show.GetWalkerIndex();
	State.Contestants = Show.GetContestants();
	if (State.Phase != OldPhase)
	{
		OnPhaseChanged.Broadcast(State.Phase);
	}
	if (State.Phase != OldPhase || State.PhaseSecondsLeft != OldSeconds)
	{
		OnStateChanged.Broadcast();
	}
}

void AGFRunwayStage::OnRep_State(const FGFRunwayStageState& Old)
{
	if (State.Phase != Old.Phase)
	{
		OnPhaseChanged.Broadcast(State.Phase);
	}
	OnStateChanged.Broadcast();
}

const FGFRunwayContestant* AGFRunwayStage::FindContestant(const FString& PlayerKey) const
{
	return State.Contestants.FindByPredicate([&PlayerKey](const FGFRunwayContestant& C) { return C.PlayerKey == PlayerKey; });
}

// ------------------------------------------------------------------------------------------------

AGFRunwayPlayerLink::AGFRunwayPlayerLink()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	bAlwaysRelevant = false;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(1.f);
}

AGFRunwayPlayerLink* AGFRunwayPlayerLink::FindLocal(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFRunwayPlayerLink> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const APlayerController* PC = Cast<APlayerController>(It->GetOwner());
		if (IsValid(*It) && PC && PC->IsLocalController())
		{
			return *It;
		}
	}
	return nullptr;
}

FString AGFRunwayPlayerLink::GetPlayerKey() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	return PS ? PS->PlayerKey : FString();
}

bool AGFRunwayPlayerLink::IsHost() const
{
	// The host's own controller is local on the server; a guest's never is. bIsHost covers the same player if the
	// controller isn't local (e.g. a dedicated test setup).
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	return PC && ((HasAuthority() && PC->IsLocalController()) || (PS && PS->bIsHost));
}

AGFRunwayStage* AGFRunwayPlayerLink::GetStage() const
{
	return AGFRunwayStage::Find(GetWorld());
}

void AGFRunwayPlayerLink::Answer(EGFRunwayRequest Request, EGFRunwayRequestResult Result)
{
	ClientAnswer(Request, Result);
}

void AGFRunwayPlayerLink::ServerRequestStart_Implementation(FName ThemeId)
{
	AGFRunwayStage* Stage = GetStage();
	if (!Stage || GetPlayerKey().IsEmpty())
	{
		Answer(EGFRunwayRequest::Start, EGFRunwayRequestResult::NotInShow);
		return;
	}
	if (!IsHost())
	{
		Answer(EGFRunwayRequest::Start, EGFRunwayRequestResult::NotHost);
		return;
	}
	Answer(EGFRunwayRequest::Start, Stage->StartShow(ThemeId));
}

void AGFRunwayPlayerLink::ServerSetOutfit_Implementation(const FGFOutfit& Outfit)
{
	AGFRunwayStage* Stage = GetStage();
	Answer(EGFRunwayRequest::Outfit, Stage ? Stage->HandleSetOutfit(GetPlayerKey(), Outfit) : EGFRunwayRequestResult::NotInShow);
}

void AGFRunwayPlayerLink::ServerSetReady_Implementation(bool bReady)
{
	AGFRunwayStage* Stage = GetStage();
	Answer(EGFRunwayRequest::Ready, Stage ? Stage->HandleSetReady(GetPlayerKey(), bReady) : EGFRunwayRequestResult::NotInShow);
}

void AGFRunwayPlayerLink::ClientAnswer_Implementation(EGFRunwayRequest Request, EGFRunwayRequestResult Result)
{
	FGFRunwayRequestAnswer A;
	A.Request = Request;
	A.Result = Result;
	Answers.Add(A);
	OnAnswer.Broadcast(A);
}
