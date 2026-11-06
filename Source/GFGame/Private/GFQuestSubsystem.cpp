#include "GFQuestSubsystem.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFFashionQuestTravel.h"
#include "GFElderBook.h"
#include "GFLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

const FName UGFQuestSubsystem::ModeName(TEXT("Quest"));

UGFQuestSubsystem* UGFQuestSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGFQuestSubsystem>() : nullptr;
}

void UGFQuestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Catalog = FGFQuestCatalog::Load();
	EventHandle = FGFQuestEvents::OnRaised().AddUObject(this, &UGFQuestSubsystem::OnQuestEvent);
	SideCatalog = FGFSideQuestCatalog::Load();
	SideEventHandle = FGFSideQuestEvents::OnRaised().AddUObject(this, &UGFQuestSubsystem::OnSideQuestEvent);
}

void UGFQuestSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (AGFGameState* GS = World->GetGameState<AGFGameState>())
		{
			if (LeaderHandle.IsValid())
			{
				GS->OnFactionLeaderSet.Remove(LeaderHandle);
			}
			GS->UnregisterModeHandler(this);
		}
	}
	LeaderHandle.Reset();
	if (EventHandle.IsValid())
	{
		FGFQuestEvents::OnRaised().Remove(EventHandle);
		EventHandle.Reset();
	}
	if (SideEventHandle.IsValid())
	{
		FGFSideQuestEvents::OnRaised().Remove(SideEventHandle);
		SideEventHandle.Reset();
	}
	Super::Deinitialize();
}

void UGFQuestSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	EnsureRegistered();
}

void UGFQuestSubsystem::EnsureRegistered()
{
	UWorld* World = GetWorld();
	AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	if (!GS || !GS->HasAuthority())
	{
		return;
	}
	if (GS->FindModeHandler(ModeName) != static_cast<IGFModeHandler*>(this))
	{
		GS->RegisterModeHandler(this);
	}
	if (!LeaderHandle.IsValid())
	{
		LeaderHandle = GS->OnFactionLeaderSet.AddUObject(this, &UGFQuestSubsystem::OnFactionLeaderSet);
	}
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UGFFashionQuestTravel* Travel = GI->GetSubsystem<UGFFashionQuestTravel>())
		{
			Travel->TryBind();
			Travel->RestoreHere(World);
		}
	}
}

FGFModeResult UGFQuestSubsystem::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request)
{
	(void)By;
	(void)Request;
	return FGFModeResult::Fail(TEXT("UnknownVerb"));
}

void UGFQuestSubsystem::OnModePlayerReady(AGFPlayerState* PS)
{
	EnsureRegistered();
	if (!PS || PS->PlayerKey.IsEmpty())
	{
		return;
	}
	if (const FGFQuestProgress* Progress = FindProgress(PS->PlayerKey))
	{
		if (!Progress->FactionId.IsNone() && PS->FactionId != Progress->FactionId)
		{
			if (AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr)
			{
				const FName Code = GS->ServerClaimFaction(PS, Progress->FactionId);
				if (Code != FName(TEXT("Ok")) && Code != FName(TEXT("Same")))
				{
					UE_LOG(LogGF, Log, TEXT("Quest: %s rejoined but %s did not take them (%s)."), *PS->PlayerKey, *Progress->FactionId.ToString(), *Code.ToString());
				}
			}
		}
	}
	RefreshSide(PS->PlayerKey);
	PushPlayer(PS);
	AskHireIfWaiting(PS->PlayerKey);
}

void UGFQuestSubsystem::CaptureModeState(TArray<uint8>& Out) const
{
	FGFQuestSave Save;
	for (const TPair<FString, FGFQuestLine>& Pair : Lines)
	{
		if (Pair.Key.IsEmpty() || Pair.Value.Progress.IsDefault())
		{
			continue;
		}
		FGFQuestRecord Record;
		Record.PlayerKey = Pair.Key;
		Record.Progress = Pair.Value.Progress;
		Save.Records.Add(Record);
	}
	for (const TPair<FString, FGFSideQuestLine>& Pair : SideLines)
	{
		if (Pair.Key.IsEmpty() || Pair.Value.Board.IsDefault())
		{
			continue;
		}
		FGFSideQuestBoardRecord Record;
		Record.PlayerKey = Pair.Key;
		Record.Board = Pair.Value.Board;
		Save.SideBoards.Add(Record);
	}
	if (Save.Records.Num() > 0 || Save.SideBoards.Num() > 0)
	{
		FGFModeBytes::Write(Save, Out);
	}
}

void UGFQuestSubsystem::RestoreModeState(const TArray<uint8>& In)
{
	Lines.Reset();
	SideLines.Reset();
	FGFQuestSave Save;
	if (In.Num() > 0 && FGFModeBytes::Read(Save, In))
	{
		for (const FGFQuestRecord& Record : Save.Records)
		{
			if (Record.PlayerKey.IsEmpty())
			{
				continue;
			}
			FGFQuestLine& Line = Lines.FindOrAdd(Record.PlayerKey);
			Line.Catalog = &Catalog;
			Line.Progress = Record.Progress;
		}
		// Blobs saved before the side board existed have no SideBoards and load with empty boards.
		// Repair before Refresh and before the player-state copy, so a bad save cannot stay stuck.
		for (const FGFSideQuestBoardRecord& Record : Save.SideBoards)
		{
			if (Record.PlayerKey.IsEmpty())
			{
				continue;
			}
			FGFSideQuestLine& Side = SideLineFor(Record.PlayerKey);
			Side.Board = Record.Board;
			Side.Board.RepairActiveLine();
		}
		// Every known player gets Locked / Available re-derived from the restored errand standing.
		for (const TPair<FString, FGFQuestLine>& Pair : Lines)
		{
			RefreshSide(Pair.Key);
		}
		TArray<FString> SideKeys;
		SideLines.GetKeys(SideKeys);
		for (const FString& Key : SideKeys)
		{
			RefreshSide(Key);
		}
	}
	PushAllPlayers();
}

const FGFQuestProgress* UGFQuestSubsystem::FindProgress(const FString& PlayerKey) const
{
	const FGFQuestLine* Line = Lines.Find(PlayerKey);
	return Line ? &Line->Progress : nullptr;
}

FGFQuestLine& UGFQuestSubsystem::LineFor(const FString& PlayerKey)
{
	FGFQuestLine& Line = Lines.FindOrAdd(PlayerKey);
	Line.Catalog = &Catalog;
	return Line;
}

void UGFQuestSubsystem::PushPlayer(AGFPlayerState* PS) const
{
	if (!PS || PS->PlayerKey.IsEmpty())
	{
		return;
	}
	const FGFQuestProgress* Progress = FindProgress(PS->PlayerKey);
	const FGFQuestProgress Defaults;
	const FGFQuestProgress& View = Progress ? *Progress : Defaults;
	PS->SetQuestReplicated(View.Step, View.Count, View.Rank);

	// Side fields mirror the repaired board. Push runs after load repair, so this copy follows that board.
	const FGFSideQuestBoard* Board = FindSideBoard(PS->PlayerKey);
	const FGFSideQuestProgress* Active = Board ? Board->Active() : nullptr;
	if (Active && Active->Status == EGFSideQuestStatus::Active)
	{
		PS->SetSideQuestReplicated(Active->LineId, Active->StepId, Active->Count, Active->Status);
	}
	else
	{
		PS->SetSideQuestReplicated(NAME_None, NAME_None, 0, EGFSideQuestStatus::Locked);
	}
	TArray<FName> Done;
	if (Board)
	{
		for (const FGFSideQuestProgress& Line : Board->Lines)
		{
			if (Line.Status == EGFSideQuestStatus::Done)
			{
				Done.Add(Line.LineId);
			}
		}
	}
	PS->SetSideDoneReplicated(Done);
}

void UGFQuestSubsystem::PushAllPlayers() const
{
	const AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	for (APlayerState* PS : GS->PlayerArray)
	{
		PushPlayer(Cast<AGFPlayerState>(PS));
	}
}

void UGFQuestSubsystem::OnQuestEvent(UWorld* World, const FGFQuestEvent& Event)
{
	if (!GetWorld() || World != GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	ApplyEvent(Event);
}

FGFQuestApplyResult UGFQuestSubsystem::ApplyEvent(const FGFQuestEvent& Event)
{
	FGFQuestApplyResult Result;
	Result.Reason = FGFQuestCodes::Unknown;
	if (Event.PlayerKey.IsEmpty())
	{
		return Result;
	}
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		return Result;
	}
	EnsureRegistered();
	FGFQuestLine& Line = LineFor(Event.PlayerKey);
	const FGFQuestProgress Before = Line.Progress;
	Result = Line.Apply(Event);

	AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	AGFPlayerState* PS = GS ? GS->FindPlayerByKey(Event.PlayerKey) : nullptr;
	if (Result.Result == EGFQuestResult::Ok && Result.Charge.Num() > 0)
	{
		if (!PS || !PS->Inventory || !PS->Inventory->RemoveAll(Result.Charge))
		{
			Line.Progress = Before;
			Result.Result = EGFQuestResult::Refused;
			Result.Reason = FGFQuestCodes::Short;
			Result.Charge.Reset();
			Result.bClaimFaction = false;
			Result.bBecameLeader = false;
		}
	}
	if (Result.Result == EGFQuestResult::Ok && Result.bClaimFaction)
	{
		if (PS && GS)
		{
			const FName Code = GS->ServerClaimFaction(PS, Line.Progress.FactionId);
			if (Code != FName(TEXT("Ok")) && Code != FName(TEXT("Same")))
			{
				if (PS->Inventory && Result.Charge.Num() > 0)
				{
					PS->Inventory->AddAll(Result.Charge);
				}
				Line.Progress = Before;
				Result.Result = EGFQuestResult::Refused;
				Result.Reason = Code;
				Result.bClaimFaction = false;
				Result.bBecameLeader = false;
				Result.Charge.Reset();
				UE_LOG(LogGF, Log, TEXT("Quest: %s could not join %s (%s)."), *Event.PlayerKey, *Event.FactionId.ToString(), *Code.ToString());
			}
		}
	}
	if (Result.Result != EGFQuestResult::Ok)
	{
		if (Result.Result == EGFQuestResult::Refused)
		{
			UE_LOG(LogGF, Verbose, TEXT("Quest: %s refused (%s)."), *Event.PlayerKey, *Result.Reason.ToString());
			// A refusal can still close a sub-step (a lost show drops its entry). Keep the player in step with it.
			if (PS && !(Line.Progress == Before))
			{
				PushPlayer(PS);
			}
		}
		return Result;
	}
	if (Result.bBecameLeader && GS)
	{
		// Rank on the player is still Champion. PushPlayer below stores Leader.
		// An empty seat already names the first claimer, so this call returns Same and does not broadcast.
		// The show was still won, and the Elder Book still gets the ShowWin post.
		const FName Code = GS->ServerSetFactionLeader(Line.Progress.FactionId, Event.PlayerKey, AGFGameState::LeaderReasonShowWin);
		if (Code == FName(TEXT("Same")))
		{
			OnFactionLeaderSet(Line.Progress.FactionId, Event.PlayerKey, Event.PlayerKey, AGFGameState::LeaderReasonShowWin);
		}
		else if (Code != FName(TEXT("Ok")))
		{
			UE_LOG(LogGF, Log, TEXT("Quest: %s won the show but did not take %s (%s)."), *Event.PlayerKey, *Line.Progress.FactionId.ToString(), *Code.ToString());
		}
	}
	RefreshSide(Event.PlayerKey);
	if (PS)
	{
		PushPlayer(PS);
	}
	if (Result.bBecameLeader)
	{
		OnQuestLeader.Broadcast(Event.PlayerKey, Line.Progress.FactionId);
	}
	return Result;
}

void UGFQuestSubsystem::OnFactionLeaderSet(FName FactionId, const FString& PlayerKey, const FString& PreviousKey, FName Reason)
{
	(void)PreviousKey;
	if (Reason != AGFGameState::LeaderReasonShowWin)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const AGFGameState* GS = World->GetGameState<AGFGameState>();
	const AGFPlayerState* PS = GS ? GS->FindPlayerByKey(PlayerKey) : nullptr;
	const FString Name = PS ? PS->GetPlayerName() : FString();
	const bool bPosted = FGFElderBook::Post(World, FGFElderBook::TownChannel(FactionId), EGFElderKind::ShowWin, Name, PlayerKey, FactionId, World->GetTimeSeconds());
	if (!bPosted)
	{
		UE_LOG(LogGF, Log, TEXT("Quest: show win for %s did not reach the Elder Book."), *PlayerKey);
	}
}

const FGFSideQuestBoard* UGFQuestSubsystem::FindSideBoard(const FString& PlayerKey) const
{
	const FGFSideQuestLine* Line = SideLines.Find(PlayerKey);
	return Line ? &Line->Board : nullptr;
}

FGFSideQuestLine& UGFQuestSubsystem::SideLineFor(const FString& PlayerKey)
{
	FGFSideQuestLine& Line = SideLines.FindOrAdd(PlayerKey);
	Line.Catalog = &SideCatalog;
	const FGFQuestProgress* Errand = FindProgress(PlayerKey);
	Line.FactionId = Errand ? Errand->FactionId : NAME_None;
	Line.Rank = Errand ? Errand->Rank : EGFFactionRank::Outsider;
	return Line;
}

void UGFQuestSubsystem::SetSideCatalogForTests(const FGFSideQuestCatalog& InCatalog)
{
	SideCatalog = InCatalog;
	SideCatalog.Normalize();
	TArray<FString> Keys;
	SideLines.GetKeys(Keys);
	for (const FString& Key : Keys)
	{
		RefreshSide(Key);
	}
	PushAllPlayers();
}

void UGFQuestSubsystem::RefreshSide(const FString& PlayerKey)
{
	if (PlayerKey.IsEmpty())
	{
		return;
	}
	SideLineFor(PlayerKey).Refresh();
}

void UGFQuestSubsystem::OnSideQuestEvent(UWorld* World, const FGFSideQuestEvent& Event)
{
	if (!GetWorld() || World != GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	ApplySideEvent(Event);
}

FGFSideQuestApplyResult UGFQuestSubsystem::ApplySideEvent(const FGFSideQuestEvent& Event)
{
	FGFSideQuestApplyResult Result;
	Result.Reason = FGFQuestCodes::Unknown;
	if (Event.PlayerKey.IsEmpty())
	{
		return Result;
	}
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		return Result;
	}
	EnsureRegistered();
	FGFSideQuestLine& Line = SideLineFor(Event.PlayerKey);
	const FGFSideQuestBoard Before = Line.Board;
	Result = Line.Apply(Event);

	AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	AGFPlayerState* PS = GS ? GS->FindPlayerByKey(Event.PlayerKey) : nullptr;
	if (Result.Result == EGFQuestResult::Ok && Result.Charge.Num() > 0)
	{
		if (!PS || !PS->Inventory || !PS->Inventory->RemoveAll(Result.Charge))
		{
			Line.Board = Before;
			Result.Result = EGFQuestResult::Refused;
			Result.Reason = FGFQuestCodes::Short;
			Result.Charge.Reset();
			Result.bAccepted = false;
			Result.bCompleted = false;
		}
	}
	if (Result.Result != EGFQuestResult::Ok)
	{
		if (Result.Result == EGFQuestResult::Refused)
		{
			UE_LOG(LogGF, Verbose, TEXT("SideQuest: %s refused (%s)."), *Event.PlayerKey, *Result.Reason.ToString());
		}
		return Result;
	}
	if (Result.bAccepted)
	{
		PostSide(EGFElderKind::SideAccept, Event.PlayerKey, Result.LineId);
	}
	if (Result.bCompleted)
	{
		PostSide(EGFElderKind::SideComplete, Event.PlayerKey, Result.LineId);
	}
	if (PS)
	{
		PushPlayer(PS);
	}
	// Ask for a hire only on the event that moved onto the hire step, not on every later event while it waits.
	const FGFSideQuestProgress* WasActive = Before.Active();
	const FGFSideQuestProgress* NowActive = Line.Board.Active();
	const bool bMoved = NowActive && (!WasActive || WasActive->LineId != NowActive->LineId || WasActive->StepId != NowActive->StepId);
	if (bMoved)
	{
		AskHireIfWaiting(Event.PlayerKey);
	}
	return Result;
}

bool UGFQuestSubsystem::WaitsOnHire(const FString& PlayerKey, FName& OutLineId, FName& OutTownId) const
{
	const FGFSideQuestLine* Line = SideLines.Find(PlayerKey);
	const FGFSideQuestStepRule* Step = Line ? Line->ActiveStep() : nullptr;
	if (!Step || Step->AdvanceKind != EGFSideQuestEventKind::HireSpawned)
	{
		return false;
	}
	const FGFSideQuestLineRule* Rule = SideCatalog.FindLine(Line->Board.ActiveLineId);
	if (!Rule)
	{
		return false;
	}
	OutLineId = Rule->LineId;
	OutTownId = Rule->TownId;
	return true;
}

void UGFQuestSubsystem::AskHireIfWaiting(const FString& PlayerKey)
{
	FName LineId;
	FName TownId;
	if (!PlayerKey.IsEmpty() && WaitsOnHire(PlayerKey, LineId, TownId))
	{
		OnSideHireAsked.Broadcast(PlayerKey, LineId, TownId);
	}
}

void UGFQuestSubsystem::PostSide(EGFElderKind Kind, const FString& PlayerKey, FName LineId) const
{
	UWorld* World = GetWorld();
	const FGFSideQuestLineRule* Rule = SideCatalog.FindLine(LineId);
	if (!World || !Rule)
	{
		return;
	}
	const AGFGameState* GS = World->GetGameState<AGFGameState>();
	const AGFPlayerState* PS = GS ? GS->FindPlayerByKey(PlayerKey) : nullptr;
	const FString Name = PS ? PS->GetPlayerName() : FString();
	if (!FGFElderBook::Post(World, FGFElderBook::TownChannel(Rule->TownId), Kind, Name, PlayerKey, Rule->TownId, World->GetTimeSeconds()))
	{
		UE_LOG(LogGF, Verbose, TEXT("SideQuest: %s post for %s did not reach the Elder Book."), *LineId.ToString(), *PlayerKey);
	}
}
