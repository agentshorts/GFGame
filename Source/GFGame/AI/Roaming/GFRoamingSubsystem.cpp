#include "GFRoamingSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UGFRoamingSubsystem* UGFRoamingSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}
	const UGameInstance* GI = Cast<UGameInstance>(WorldContext);
	if (!GI)
	{
		const UWorld* World = WorldContext->GetWorld();
		GI = World ? World->GetGameInstance() : nullptr;
	}
	return GI ? GI->GetSubsystem<UGFRoamingSubsystem>() : nullptr;
}

TSharedPtr<FGFRevengeGangBoard> UGFRoamingSubsystem::EnsureBoard()
{
	if (!Board)
	{
		Board = MakeShared<FGFRevengeGangBoard>();
	}
	return Board;
}

bool UGFRoamingSubsystem::BeginTravel(FName ToMap, const FString& TravellerKey)
{
	if (!Board || TravellerKey.IsEmpty() || Board->GetTargetKey() != TravellerKey)
	{
		return false;
	}
	const FGFRoamingHandoff Record = Board->ExportHandoff(ToMap);
	if (!Record.bPending)
	{
		return false;
	}
	Pending = Record;
	Board.Reset();
	return true;
}

bool UGFRoamingSubsystem::ConsumeTravel()
{
	if (!Pending.bPending)
	{
		return false;
	}
	TSharedPtr<FGFRevengeGangBoard> Next = MakeShared<FGFRevengeGangBoard>();
	if (!Next->ConsumeHandoff(Pending))
	{
		return false;
	}
	Board = Next;
	return true;
}
