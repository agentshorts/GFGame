#include "GFFashionGateSubsystem.h"

#include "Engine/World.h"

void UGFFashionGateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BeforeEnterHandle = OnBeforeEnter.AddUObject(this, &UGFFashionGateSubsystem::HandleBeforeEnter);
}

void UGFFashionGateSubsystem::Deinitialize()
{
	OnBeforeEnter.Remove(BeforeEnterHandle);
	BeforeEnterHandle.Reset();
	Super::Deinitialize();
}

void UGFFashionGateSubsystem::HandleBeforeEnter(FName GateId)
{
	if (GateId == GetGateId())
	{
		NotifyMapTravel(GetWorld());
	}
}

void UGFFashionGateSubsystem::NotifyMapTravel(UWorld* Leaving)
{
	OnMapTravel.Broadcast(Leaving);
}

void UGFFashionGateSubsystem::StashQuestCarry(TArray<uint8> QuestBytes, TArray<FGFFactionLeader> Leaders, TMap<FString, FName> Sessions)
{
	Carry.Stash(MoveTemp(QuestBytes), MoveTemp(Leaders), MoveTemp(Sessions));
}

bool UGFFashionGateSubsystem::TakeQuestCarry(TArray<uint8>& QuestBytes, TArray<FGFFactionLeader>& Leaders, TMap<FString, FName>& Sessions)
{
	return Carry.Take(QuestBytes, Leaders, Sessions);
}
