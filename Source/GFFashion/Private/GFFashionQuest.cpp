#include "GFFashionQuest.h"

const FName FGFFashionQuest::HideId(TEXT("Hide"));
const FName FGFFashionQuest::ScalesId(TEXT("Scales"));
const FName FGFFashionQuest::AtelierStation(TEXT("Atelier"));

EGFFashionCarryState FGFFashionCarry::Transition(EGFFashionCarryState From, EGFFashionCarryEvent Event)
{
	switch (Event)
	{
	case EGFFashionCarryEvent::Stash:
		return EGFFashionCarryState::Holding;
	case EGFFashionCarryEvent::Take:
		return EGFFashionCarryState::Empty;
	default:
		return From;
	}
}

void FGFFashionCarry::Stash(TArray<uint8> InBytes, TArray<FGFFactionLeader> InLeaders, TMap<FString, FName> InSessions)
{
	State = Transition(State, EGFFashionCarryEvent::Stash);
	QuestBytes = MoveTemp(InBytes);
	Leaders = MoveTemp(InLeaders);
	Sessions = MoveTemp(InSessions);
}

bool FGFFashionCarry::Take(TArray<uint8>& OutBytes, TArray<FGFFactionLeader>& OutLeaders, TMap<FString, FName>& OutSessions)
{
	if (State != EGFFashionCarryState::Holding)
	{
		return false;
	}
	OutBytes = MoveTemp(QuestBytes);
	OutLeaders = MoveTemp(Leaders);
	OutSessions = MoveTemp(Sessions);
	QuestBytes.Reset();
	Leaders.Reset();
	Sessions.Reset();
	State = Transition(State, EGFFashionCarryEvent::Take);
	return true;
}

bool FGFFashionQuest::UsesDinoMaterial(const TArray<FGFItemStack>& Inputs)
{
	for (const FGFItemStack& Stack : Inputs)
	{
		if (Stack.Count > 0 && (Stack.ItemId == HideId || Stack.ItemId == ScalesId))
		{
			return true;
		}
	}
	return false;
}

FName FGFFashionQuest::CraftEventItem(bool bCrafted, FName StationId, const TArray<FGFItemStack>& Inputs, FName OutputItemId)
{
	if (!bCrafted || StationId != AtelierStation || OutputItemId.IsNone() || !UsesDinoMaterial(Inputs))
	{
		return NAME_None;
	}
	return OutputItemId;
}

FName FGFFashionQuest::WornEventItem(bool bWorn, FName PieceId)
{
	return bWorn && !PieceId.IsNone() ? PieceId : NAME_None;
}

int32 FGFFashionQuest::TierValue(EGFRunwayTier Tier)
{
	return static_cast<int32>(Tier);
}

bool FGFFashionQuest::IsShowWin(int32 Place, int32 Tier, int32 ContestantCount)
{
	return ContestantCount >= 1 && Place == FGFQuestShowRule::WinPlace && Tier >= FGFQuestShowRule::WinTier;
}
