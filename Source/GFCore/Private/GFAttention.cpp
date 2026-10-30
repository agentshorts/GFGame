#include "GFAttention.h"

namespace
{
	struct FWorldHolds
	{
		TWeakObjectPtr<UWorld> World;
		TArray<FGFAttentionHold> Holds;
	};

	TArray<FWorldHolds>& HoldStore()
	{
		static TArray<FWorldHolds> Store;
		return Store;
	}
}

void FGFAttention::SetHolds(UWorld* World, TArray<FGFAttentionHold> Holds)
{
	if (!World)
	{
		return;
	}
	TArray<FWorldHolds>& Store = HoldStore();
	Store.RemoveAll([](const FWorldHolds& E) { return !E.World.IsValid(); });
	FWorldHolds* Found = Store.FindByPredicate([World](const FWorldHolds& E) { return E.World.Get() == World; });
	if (!Found)
	{
		Found = &Store.AddDefaulted_GetRef();
		Found->World = World;
	}
	Found->Holds = MoveTemp(Holds);
}

void FGFAttention::ClearWorld(UWorld* World)
{
	HoldStore().RemoveAll([World](const FWorldHolds& E) { return !E.World.IsValid() || E.World.Get() == World; });
}

bool FGFAttention::FindHold(const UWorld* World, const FString& TargetKey, FGFAttentionHold& Out)
{
	for (const FWorldHolds& E : HoldStore())
	{
		if (E.World.Get() != World)
		{
			continue;
		}
		for (const FGFAttentionHold& H : E.Holds)
		{
			if (H.TargetKey == TargetKey)
			{
				Out = H;
				return true;
			}
		}
	}
	return false;
}

FGFOnGiantTaunt& FGFAttention::OnTaunt()
{
	static FGFOnGiantTaunt Delegate;
	return Delegate;
}

void FGFAttention::RaiseTaunt(UWorld* World, const FGFGiantTaunt& Taunt)
{
	if (World)
	{
		OnTaunt().Broadcast(World, Taunt);
	}
}
