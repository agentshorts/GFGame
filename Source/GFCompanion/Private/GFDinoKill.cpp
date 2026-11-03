#include "GFDinoKill.h"

namespace
{
	FGFDinoKillResult Snapshot(const FGFDinoKillMachine& Machine)
	{
		FGFDinoKillResult Out;
		Out.Life = Machine.Life;
		Out.HitPoints = Machine.HitPoints;
		return Out;
	}
}

FName FGFDinoKill::MaterialItem(FName Species)
{
	if (Species == TEXT("Tusksaw") || Species == TEXT("Hornback") || Species == TEXT("Lanternmaw"))
	{
		return TEXT("Hide");
	}
	if (Species == TEXT("Nipper") || Species == TEXT("Raptor") || Species == TEXT("Scythepaw"))
	{
		return TEXT("Scales");
	}
	return NAME_None;
}

FName FGFDinoKill::SkinItem(FName Species)
{
	if (MaterialItem(Species).IsNone())
	{
		return NAME_None;
	}
	return FName(*FString::Printf(TEXT("Skin_%s"), *Species.ToString()));
}

float FGFDinoKill::SkinFraction(int32 Seed)
{
	FRandomStream Rng(Seed);
	return Rng.GetFraction();
}

TArray<FGFItemStack> FGFDinoKill::Roll(FName Species, int32 Seed)
{
	TArray<FGFItemStack> Out;
	const FName Material = MaterialItem(Species);
	if (Material.IsNone())
	{
		return Out;
	}
	Out.Emplace(Material, MaterialCount);
	if (SkinFraction(Seed) < SkinChance)
	{
		Out.Emplace(SkinItem(Species), 1);
	}
	return Out;
}

FString FGFDinoKill::PickKiller(const TMap<FString, float>& Shares, const FString& KillingBlow)
{
	FString Best;
	float BestAmount = -1.f;
	for (const TPair<FString, float>& Pair : Shares)
	{
		if (Pair.Key.IsEmpty() || Pair.Value <= 0.f)
		{
			continue;
		}
		if (Pair.Value > BestAmount + KINDA_SMALL_NUMBER)
		{
			BestAmount = Pair.Value;
			Best = Pair.Key;
			continue;
		}
		if (!FMath::IsNearlyEqual(Pair.Value, BestAmount))
		{
			continue;
		}
		const bool bBlow = Pair.Key == KillingBlow;
		const bool bBestBlow = Best == KillingBlow;
		if (bBlow && !bBestBlow)
		{
			Best = Pair.Key;
		}
		else if (bBlow == bBestBlow && Pair.Key < Best)
		{
			Best = Pair.Key;
		}
	}
	return Best;
}

EGFDinoLife FGFDinoKill::NextLife(EGFDinoLife Current, float HitPoints, float StateSeconds, bool bCanDie)
{
	if (Current == EGFDinoLife::Dead)
	{
		return EGFDinoLife::Dead;
	}
	if (!bCanDie)
	{
		return Current == EGFDinoLife::Downed ? EGFDinoLife::Downed : EGFDinoLife::Alive;
	}
	if (Current == EGFDinoLife::Alive)
	{
		return HitPoints <= 0.f ? EGFDinoLife::Downed : EGFDinoLife::Alive;
	}
	return StateSeconds >= DownedSeconds ? EGFDinoLife::Dead : EGFDinoLife::Downed;
}

FGFOnDinoKilled& FGFDinoKill::OnDinoKilled()
{
	static FGFOnDinoKilled Delegate;
	return Delegate;
}

void FGFDinoKillMachine::Reset(float MaxHp)
{
	const float Max = MaxHp > 0.f ? MaxHp : FGFDinoKill::DefaultHitPoints;
	Life = EGFDinoLife::Alive;
	HitPoints = Max;
	MaxHitPoints = Max;
	StateSeconds = 0.f;
	bPaid = false;
	Shares.Reset();
	KillingBlow.Reset();
}

FGFDinoKillResult FGFDinoKillMachine::ApplyHit(FName Species, float Damage, const FString& AttackerKey, int32 Seed, bool bTamed, bool bRented)
{
	const bool bCanDie = FGFDinoKill::CanDie(bTamed, bRented);
	if (!bCanDie || Life != EGFDinoLife::Alive || Damage <= 0.f)
	{
		return Snapshot(*this);
	}

	const bool bWasUp = HitPoints > 0.f;
	const float Removed = FMath::Min(HitPoints, Damage);
	HitPoints = FMath::Max(0.f, HitPoints - Damage);
	if (!AttackerKey.IsEmpty())
	{
		Shares.FindOrAdd(AttackerKey) += Damage;
	}
	if (bWasUp && HitPoints <= 0.f)
	{
		KillingBlow = AttackerKey;
	}

	const EGFDinoLife Before = Life;
	Life = FGFDinoKill::NextLife(Life, HitPoints, StateSeconds, true);
	FGFDinoKillResult Out = Snapshot(*this);
	Out.Applied = Removed;
	if (Before == EGFDinoLife::Alive && Life == EGFDinoLife::Downed)
	{
		StateSeconds = 0.f;
		if (!bPaid)
		{
			bPaid = true;
			Out.bKilled = true;
			Out.KillerKey = FGFDinoKill::PickKiller(Shares, KillingBlow);
			Out.Stacks = FGFDinoKill::Roll(Species, Seed);
		}
	}
	return Out;
}

FGFDinoKillResult FGFDinoKillMachine::Advance(float DeltaSeconds)
{
	if (Life != EGFDinoLife::Downed)
	{
		return Snapshot(*this);
	}
	StateSeconds += FMath::Max(0.f, DeltaSeconds);
	const EGFDinoLife Before = Life;
	Life = FGFDinoKill::NextLife(Life, HitPoints, StateSeconds, true);
	FGFDinoKillResult Out = Snapshot(*this);
	Out.bNowDead = Before == EGFDinoLife::Downed && Life == EGFDinoLife::Dead;
	return Out;
}
