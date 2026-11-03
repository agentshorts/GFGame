#include "GFQuestSubsystem.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFLog.h"
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
}

void UGFQuestSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (AGFGameState* GS = World->GetGameState<AGFGameState>())
		{
			GS->UnregisterModeHandler(this);
		}
	}
	if (EventHandle.IsValid())
	{
		FGFQuestEvents::OnRaised().Remove(EventHandle);
		EventHandle.Reset();
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
	if (GS->FindModeHandler(ModeName) == static_cast<IGFModeHandler*>(this))
	{
		return;
	}
	GS->RegisterModeHandler(this);
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
	PushPlayer(PS);
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
	if (Save.Records.Num() > 0)
	{
		FGFModeBytes::Write(Save, Out);
	}
}

void UGFQuestSubsystem::RestoreModeState(const TArray<uint8>& In)
{
	Lines.Reset();
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
		}
		return Result;
	}
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
