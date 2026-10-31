#include "GFVillainSession.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "Math/RandomStream.h"

FGFVillainStatus FGFVillainSession::Read(const AGFPlayerState& PS)
{
	FGFVillainStatus Status;
	Status.Key = PS.PlayerKey;
	Status.bOn = PS.bVillainMode;
	Status.WarmupUntil = PS.VillainWarmupUntil;
	Status.LockUntil = PS.VillainLockUntil;
	Status.DisarmUntil = PS.VillainDisarmUntil;
	Status.ImmuneUntil = PS.VillainImmuneUntil;
	Status.Title = PS.VillainTitle;
	Status.bTitleSpent = PS.bVillainTitleSpent;
	Status.WinStreak = PS.VillainWinStreak;
	Status.Health = PS.VillainHealth;
	for (const FGFVillainRecentHit& Hit : PS.VillainRecentHits)
	{
		Status.RecentHits.Add(TPair<FString, double>(Hit.Key, Hit.AtSeconds));
	}
	return Status;
}

void FGFVillainSession::Write(AGFPlayerState& PS, const FGFVillainStatus& Status)
{
	PS.SetVillainMode(Status.bOn);
	PS.VillainWarmupUntil = static_cast<float>(Status.WarmupUntil);
	PS.VillainLockUntil = static_cast<float>(Status.LockUntil);
	PS.VillainDisarmUntil = static_cast<float>(Status.DisarmUntil);
	PS.VillainImmuneUntil = static_cast<float>(Status.ImmuneUntil);
	PS.VillainTitle = Status.Title;
	PS.bVillainTitleSpent = Status.bTitleSpent;
	PS.VillainWinStreak = Status.WinStreak;
	PS.VillainHealth = Status.Health;
	PS.VillainRecentHits.Reset();
	for (const TPair<FString, double>& Hit : Status.RecentHits)
	{
		FGFVillainRecentHit Row;
		Row.Key = Hit.Key;
		Row.AtSeconds = static_cast<float>(Hit.Value);
		PS.VillainRecentHits.Add(Row);
	}
	PS.ForceNetUpdate();
}

FName FGFVillainSession::Arm(AGFPlayerState& PS, double Now, bool bJailed, FRandomStream& Rng)
{
	FGFVillainStatus Status = Read(PS);
	const FName Code = FGFVillainRules::TryArm(Status, Now, bJailed, Rng);
	if (Code == TEXT("Ok"))
	{
		Write(PS, Status);
	}
	return Code;
}

FName FGFVillainSession::BeginDisarm(AGFPlayerState& PS, double Now)
{
	FGFVillainStatus Status = Read(PS);
	const FName Code = FGFVillainRules::TryDisarm(Status, Now);
	if (Code == TEXT("Ok"))
	{
		Write(PS, Status);
	}
	return Code;
}

bool FGFVillainSession::Tick(AGFPlayerState& PS, double Now)
{
	FGFVillainStatus Status = Read(PS);
	if (!FGFVillainRules::Tick(Status, Now))
	{
		return false;
	}
	Write(PS, Status);
	return true;
}

FName FGFVillainSession::RerollTitle(AGFPlayerState& PS, FRandomStream& Rng)
{
	FGFVillainStatus Status = Read(PS);
	const FName Code = FGFVillainRules::RerollTitle(Status, Rng);
	if (Code == TEXT("Ok"))
	{
		Write(PS, Status);
	}
	return Code;
}

FGFVillainHit FGFVillainSession::Resolve(AGFPlayerState& Attacker, AGFPlayerState& Victim, EGFVillainPlace AttackerPlace, EGFVillainPlace VictimPlace, double Now, float RawDamage, const UDataTable* Items)
{
	FGFVillainStatus AttackerStatus = Read(Attacker);
	FGFVillainStatus VictimStatus = Read(Victim);

	FGFVillainSide AttackerSide;
	AttackerSide.Key = AttackerStatus.Key;
	AttackerSide.bVillain = AttackerStatus.bOn;
	AttackerSide.Place = AttackerPlace;
	AttackerSide.WarmupUntil = AttackerStatus.WarmupUntil;
	AttackerSide.ImmuneUntil = AttackerStatus.ImmuneUntil;
	AttackerSide.Health = AttackerStatus.Health;
	AttackerSide.bJailed = AttackerPlace == EGFVillainPlace::Jail;

	FGFVillainSide VictimSide;
	VictimSide.Key = VictimStatus.Key;
	VictimSide.bVillain = VictimStatus.bOn;
	VictimSide.Place = VictimPlace;
	VictimSide.WarmupUntil = VictimStatus.WarmupUntil;
	VictimSide.ImmuneUntil = VictimStatus.ImmuneUntil;
	VictimSide.Health = VictimStatus.Health;
	VictimSide.bJailed = VictimPlace == EGFVillainPlace::Jail;

	const FGFVillainQuery Query{ Now, RawDamage };
	const FGFVillainHit Landed = FGFVillainRules::CanDamage(AttackerSide, VictimSide, Query);
	if (Landed.bBonk)
	{
		++Victim.VillainBonkSerial;
		Victim.ForceNetUpdate();
	}
	if (!Landed.bDamage)
	{
		return Landed;
	}

	VictimStatus.Health = FMath::Max(0.f, VictimStatus.Health - Landed.Damage);
	FGFVillainRules::NoteHit(AttackerStatus, VictimStatus, Landed, Now);
	if (VictimStatus.Health <= 0.f)
	{
		const FGFInventory Bag = Victim.Inventory ? Victim.Inventory->GetInventory() : FGFInventory();
		const TArray<FGFItemStack> Drop = FGFVillainRules::FaintDrop(Bag, Items);
		if (Victim.Inventory && Drop.Num() > 0)
		{
			Victim.Inventory->RemoveAll(Drop);
		}
		for (const FGFItemStack& Stack : Drop)
		{
			Victim.VillainDropped.Add(Stack);
		}
		FGFVillainRules::NoteFaint(VictimStatus, AttackerStatus, Now);
	}
	Write(Attacker, AttackerStatus);
	Write(Victim, VictimStatus);
	return Landed;
}
