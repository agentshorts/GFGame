#include "GFVillainRules.h"
#include "GFEconomy.h"
#include "Engine/DataTable.h"
#include "Math/RandomStream.h"

float FGFVillainRules::MoveSpeedMultiplier()
{
	return 1.f;
}

bool FGFVillainRules::PersistsOnCharacter()
{
	return false;
}

bool FGFVillainRules::GrantsBuildRights()
{
	return false;
}

bool FGFVillainRules::ChangesGiantTargeting()
{
	return false;
}

bool FGFVillainRules::CountsAsDinoKill()
{
	return false;
}

int32 FGFVillainRules::RepDelta()
{
	return 0;
}

const TCHAR* FGFVillainRules::RetireLine()
{
	return TEXT("has retired from villainy, for now");
}

TArray<FString> FGFVillainRules::Titles()
{
	return {
		TEXT("the Unpleasant"),
		TEXT("the Snack Thief"),
		TEXT("Menace of the Flats"),
		TEXT("Destroyer of Picnics"),
		TEXT("the Rude"),
		TEXT("Badlands Gremlin")
	};
}

FString FGFVillainRules::TitleAt(int32 Index)
{
	const TArray<FString> List = Titles();
	return List.IsValidIndex(Index) ? List[Index] : FString();
}

FString FGFVillainRules::DinobookLine(int32 WinStreak)
{
	if (WinStreak >= WantedStreak)
	{
		return TEXT("Wanted: a villain has won three fights in a row.");
	}
	return TEXT("A villain was seen in the badlands again. Typical.");
}

FName FGFVillainRules::RequestDuel(bool& bStarted)
{
	bStarted = false;
	return TEXT("Parked");
}

bool FGFVillainRules::PlaceAllows(EGFVillainPlace Place)
{
	return Place == EGFVillainPlace::Wild || Place == EGFVillainPlace::Hazard || Place == EGFVillainPlace::Jail;
}

FGFVillainSide FGFVillainRules::DinoSide(const FGFVillainSide& Owner, const FString& DinoKey, EGFVillainPlace Place, bool bRental)
{
	FGFVillainSide Side;
	Side.Key = DinoKey;
	Side.bDino = true;
	Side.bRental = bRental;
	Side.OwnerKey = Owner.Key;
	Side.Place = Place;
	Side.bVillain = Owner.bVillain;
	Side.bJailed = Place == EGFVillainPlace::Jail;
	Side.WarmupUntil = Owner.WarmupUntil;
	Side.ImmuneUntil = Owner.ImmuneUntil;
	Side.Health = Owner.Health;
	Side.Location = Owner.Location;
	return Side;
}

namespace
{
	FGFVillainHit Hit(FName Code, bool bBonk, float Damage, bool bLock)
	{
		FGFVillainHit Out;
		Out.Code = Code;
		Out.bBonk = bBonk;
		Out.Damage = Damage;
		Out.bDamage = Damage > 0.f;
		Out.bRestartLock = bLock && Out.bDamage;
		return Out;
	}

	int32 RollTitle(FRandomStream& Rng, int32 Avoid)
	{
		const int32 Count = FGFVillainRules::Titles().Num();
		if (Count <= 0)
		{
			return INDEX_NONE;
		}
		if (Count == 1)
		{
			return 0;
		}
		int32 Index = Rng.RandRange(0, Count - 1);
		if (Index == Avoid)
		{
			Index = (Index + 1) % Count;
		}
		return Index;
	}
}

FName FGFVillainRules::TryArm(FGFVillainStatus& Status, double Now, bool bJailed, FRandomStream& Rng)
{
	if (bJailed)
	{
		return TEXT("Jailed");
	}
	if (Status.bOn)
	{
		return TEXT("Already");
	}
	Status.bOn = true;
	Status.WarmupUntil = Now + WarmupSeconds;
	Status.DisarmUntil = -1.0;
	if (Status.Title == INDEX_NONE)
	{
		Status.Title = RollTitle(Rng, INDEX_NONE);
	}
	if (Status.Health <= 0.f)
	{
		Status.Health = StartHealth;
	}
	return TEXT("Ok");
}

FName FGFVillainRules::TryDisarm(FGFVillainStatus& Status, double Now)
{
	if (!Status.bOn)
	{
		return TEXT("NotVillain");
	}
	if (Status.DisarmUntil >= 0.0)
	{
		return TEXT("Disarming");
	}
	if (Now < Status.LockUntil)
	{
		return TEXT("Locked");
	}
	Status.DisarmUntil = Now + DisarmSeconds;
	return TEXT("Ok");
}

bool FGFVillainRules::Tick(FGFVillainStatus& Status, double Now)
{
	if (!Status.bOn || Status.DisarmUntil < 0.0 || Now < Status.DisarmUntil)
	{
		return false;
	}
	Status.bOn = false;
	Status.DisarmUntil = -1.0;
	Status.WarmupUntil = -1.0;
	Status.LockUntil = -1.0;
	Status.Title = INDEX_NONE;
	Status.bTitleSpent = false;
	return true;
}

FName FGFVillainRules::RerollTitle(FGFVillainStatus& Status, FRandomStream& Rng)
{
	if (!Status.bOn)
	{
		return TEXT("NotVillain");
	}
	if (Status.bTitleSpent)
	{
		return TEXT("Spent");
	}
	Status.Title = RollTitle(Rng, Status.Title);
	Status.bTitleSpent = true;
	return TEXT("Ok");
}

FGFVillainHit FGFVillainRules::CanDamage(const FGFVillainSide& Attacker, const FGFVillainSide& Victim, const FGFVillainQuery& Query)
{
	if (Query.RawDamage <= 0.f)
	{
		return Hit(TEXT("None"), false, 0.f, false);
	}
	if (!Attacker.bDino && !Victim.bDino && Attacker.Key == Victim.Key)
	{
		return Hit(TEXT("Self"), false, 0.f, false);
	}
	if (Attacker.bRental || Victim.bRental)
	{
		return Hit(TEXT("Rental"), false, 0.f, false);
	}
	if (!Attacker.bVillain)
	{
		return Hit(TEXT("NotVillain"), false, 0.f, false);
	}
	// A safe circle, faction land, the hub, or a mini-game map: the shot passes through.
	// Jail is not on that list. A jailed villain can be targeted.
	if (!PlaceAllows(Attacker.Place) || !PlaceAllows(Victim.Place))
	{
		return Hit(TEXT("Zone"), false, 0.f, false);
	}
	if (Query.Now < Attacker.WarmupUntil || Query.Now < Victim.WarmupUntil)
	{
		return Hit(TEXT("Warmup"), false, 0.f, false);
	}
	if (!Victim.bVillain)
	{
		return Hit(TEXT("Bonk"), true, 0.f, false);
	}
	if (Query.Now < Victim.ImmuneUntil)
	{
		return Hit(TEXT("Immune"), false, 0.f, false);
	}
	return Hit(TEXT("Ok"), false, Query.RawDamage, true);
}

void FGFVillainRules::NoteHit(FGFVillainStatus& Attacker, FGFVillainStatus& Victim, const FGFVillainHit& Landed, double Now)
{
	if (!Landed.bRestartLock)
	{
		return;
	}
	const double Until = Now + LockSeconds;
	Attacker.LockUntil = Until;
	Victim.LockUntil = Until;
	// One entry per hitter: a repeat hit refreshes its time. Entries past the window drop.
	const double Cut = Now - RecentHitSeconds;
	const FString& Key = Attacker.Key;
	Victim.RecentHits.RemoveAll([Cut, &Key](const TPair<FString, double>& Hit)
	{
		return Hit.Value < Cut || Hit.Key == Key;
	});
	Victim.RecentHits.Add(TPair<FString, double>(Attacker.Key, Now));
}

TArray<FGFItemStack> FGFVillainRules::FaintDrop(const FGFInventory& Bag, const UDataTable* Items)
{
	TArray<FGFItemStack> Drop;
	int32 Left = FaintDropCap;
	for (const FGFItemStack& Stack : Bag.Items)
	{
		if (Left <= 0)
		{
			break;
		}
		if (Stack.Count <= 0)
		{
			continue;
		}
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, Stack.ItemId);
		if (!Row || Row->bAccountBound || Row->Category != EGFItemCategory::Material)
		{
			continue;
		}
		int32 Count = FMath::FloorToInt(static_cast<float>(Stack.Count) * FaintMaterialFraction);
		Count = FMath::Min(Count, Left);
		if (Count <= 0)
		{
			continue;
		}
		Drop.Add(FGFItemStack(Stack.ItemId, Count));
		Left -= Count;
	}
	return Drop;
}

void FGFVillainRules::NoteFaint(FGFVillainStatus& Victim, FGFVillainStatus& Attacker, double Now)
{
	Victim.Health = StartHealth;
	Victim.ImmuneUntil = Now + ImmuneSeconds;
	Victim.WinStreak = 0;
	Victim.LockUntil = -1.0;
	Attacker.WinStreak += 1;
}
