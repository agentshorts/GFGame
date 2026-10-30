#include "GFMiniGameGateSubsystem.h"
#include "GFLog.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Misc/PackageName.h"

bool UGFMiniGameGateSubsystem::EnterMiniGame()
{
	if (!IsUnlocked())
	{
		UE_LOG(LogGF, Warning, TEXT("Gate %s is locked"), *GetGateId().ToString());
		return false;
	}
	const FName Map = GetMapName();
	UGameInstance* GI = GetGameInstance();
	if (Map.IsNone() || !GI || !GI->GetWorld() || !FPackageName::DoesPackageExist(Map.ToString()))
	{
		UE_LOG(LogGF, Warning, TEXT("Gate %s has no map yet (stub)"), *GetGateId().ToString());
		return false;
	}
	OnBeforeEnter.Broadcast(GetGateId());
	UGameplayStatics::OpenLevel(GI->GetWorld(), Map);
	return true;
}

void UGFMiniGameGateSubsystem::ReportResult(const FGFMiniGameResult& Result)
{
	// TODO(M2-M5): look up Result.Tier in the mode's rewards DataTable instead of trusting Result.Rewards.
	PendingRewards.Append(Result.Rewards);
	OnMiniGameFinished.Broadcast(GetGateId(), Result);
}

void UGFMiniGameGateSubsystem::ClaimPendingRewards(FGFInventory& Into)
{
	for (const FGFItemStack& S : PendingRewards)
	{
		Into.Add(S.ItemId, S.Count);
	}
	PendingRewards.Reset();
}
