#include "GFShardTravelSubsystem.h"

#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

FString UGFShardTravelSubsystem::KeyFor(const AController* Controller)
{
	const APlayerState* PS = Controller ? Controller->PlayerState : nullptr;
	if (!PS)
	{
		return FString();
	}
	if (PS->GetUniqueId().IsValid())
	{
		return PS->GetUniqueId()->ToString();
	}
	return FString::Printf(TEXT("%s#%d"), *PS->GetPlayerName(), PS->GetPlayerId());
}

void UGFShardTravelSubsystem::SetPending(FName StoneId)
{
	PendingStoneId = StoneId;
	ArrivedKeys.Reset();
}

void UGFShardTravelSubsystem::ClearPending()
{
	PendingStoneId = NAME_None;
	ArrivedKeys.Reset();
}

bool UGFShardTravelSubsystem::Consume(const FString& PlayerKey, FName& OutStoneId)
{
	if (PendingStoneId.IsNone() || PlayerKey.IsEmpty() || ArrivedKeys.Contains(PlayerKey))
	{
		return false;
	}
	ArrivedKeys.Add(PlayerKey);
	OutStoneId = PendingStoneId;
	return true;
}

bool UGFShardTravelSubsystem::ConsumedStone(const FString& PlayerKey, FName& OutStoneId) const
{
	if (PendingStoneId.IsNone() || PlayerKey.IsEmpty() || !ArrivedKeys.Contains(PlayerKey))
	{
		return false;
	}
	OutStoneId = PendingStoneId;
	return true;
}
