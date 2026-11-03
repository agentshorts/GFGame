#include "GFFashionQuestTravel.h"

#include "GFFashionGateSubsystem.h"
#include "GFGameState.h"
#include "GFQuestSubsystem.h"
#include "GFLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UGFFashionQuestTravel::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TryBind();
}

void UGFFashionQuestTravel::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGFFashionGateSubsystem* Gate = GI->GetSubsystem<UGFFashionGateSubsystem>())
		{
			Gate->OnMapTravel.Remove(TravelHandle);
		}
	}
	TravelHandle.Reset();
	Super::Deinitialize();
}

void UGFFashionQuestTravel::TryBind()
{
	if (TravelHandle.IsValid())
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UGFFashionGateSubsystem* Gate = GI ? GI->GetSubsystem<UGFFashionGateSubsystem>() : nullptr;
	if (!Gate)
	{
		return;
	}
	TravelHandle = Gate->OnMapTravel.AddUObject(this, &UGFFashionQuestTravel::OnMapTravel);
}

bool UGFFashionQuestTravel::RestoreHere(UWorld* World)
{
	UGameInstance* GI = GetGameInstance();
	return RestoreFrom(GI ? GI->GetSubsystem<UGFFashionGateSubsystem>() : nullptr, World);
}

void UGFFashionQuestTravel::OnMapTravel(UWorld* Leaving)
{
	UGameInstance* GI = GetGameInstance();
	CaptureInto(GI ? GI->GetSubsystem<UGFFashionGateSubsystem>() : nullptr, Leaving);
}

void UGFFashionQuestTravel::CaptureInto(UGFFashionGateSubsystem* Gate, UWorld* World)
{
	if (!Gate || !World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	TArray<uint8> Bytes;
	TArray<FGFFactionLeader> Leaders;
	TMap<FString, FName> Sessions;
	if (UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(World))
	{
		Quest->CaptureModeState(Bytes);
	}
	if (const AGFGameState* GS = World->GetGameState<AGFGameState>())
	{
		Leaders = GS->FactionLeaders;
		Sessions = GS->SessionFactions;
	}
	UE_LOG(LogGF, Log, TEXT("Fashion travel: carrying %d quest bytes and %d leader seats."), Bytes.Num(), Leaders.Num());
	Gate->StashQuestCarry(MoveTemp(Bytes), MoveTemp(Leaders), MoveTemp(Sessions));
}

bool UGFFashionQuestTravel::RestoreFrom(UGFFashionGateSubsystem* Gate, UWorld* World)
{
	if (!Gate || !World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	if (Gate->GetQuestCarryState() != EGFFashionCarryState::Holding)
	{
		return false;
	}
	AGFGameState* GS = World->GetGameState<AGFGameState>();
	UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(World);
	if (!GS || !Quest || !GS->HasAuthority())
	{
		return false;
	}
	TArray<uint8> Bytes;
	TArray<FGFFactionLeader> Leaders;
	TMap<FString, FName> Sessions;
	if (!Gate->TakeQuestCarry(Bytes, Leaders, Sessions))
	{
		return false;
	}
	Quest->RestoreModeState(Bytes);
	GS->ServerRestoreFactionTravel(Leaders, Sessions);
	UE_LOG(LogGF, Log, TEXT("Fashion travel: restored %d leader seats."), GS->FactionLeaders.Num());
	return true;
}
