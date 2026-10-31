#include "GFArenaRules.h"

int32 FGFArenaRosterRules::PerPlayerCap(int32 PlayersOnSide)
{
	if (PlayersOnSide <= 0)
	{
		return 0;
	}
	if (PlayersOnSide < 3)
	{
		return SoloPartyCap;
	}
	return FMath::Max(1, SideCap / PlayersOnSide);
}

FName FGFArenaRosterRules::CheckPlacement(int32 PlayersOnSide, int32 AlreadyOnSide, const TArray<FName>& SideSpecies, const TArray<FName>& Adding)
{
	if (Adding.Num() > PerPlayerCap(PlayersOnSide))
	{
		return TEXT("PartySplit");
	}
	if (AlreadyOnSide + Adding.Num() > SideCap)
	{
		return TEXT("TeamFull");
	}
	TMap<FName, int32> Count;
	for (const FName Species : SideSpecies)
	{
		Count.FindOrAdd(Species) += 1;
	}
	for (const FName Species : Adding)
	{
		int32& N = Count.FindOrAdd(Species);
		++N;
		if (N > SpeciesCap)
		{
			return TEXT("SpeciesCap");
		}
	}
	return TEXT("Ok");
}

FName FGFArenaRosterRules::PickSpecies(const TArray<FName>& SpeciesIds, const TMap<FName, int32>& Used, int32 Roll)
{
	if (SpeciesIds.Num() == 0)
	{
		return NAME_None;
	}
	const int32 Start = FMath::Abs(Roll) % SpeciesIds.Num();
	for (int32 Step = 0; Step < SpeciesIds.Num(); ++Step)
	{
		const FName Id = SpeciesIds[(Start + Step) % SpeciesIds.Num()];
		const int32* Have = Used.Find(Id);
		if (!Have || *Have < SpeciesCap)
		{
			return Id;
		}
	}
	return SpeciesIds[Start];
}

EGFArenaStanding FGFArenaRep::Standing(int32 Rep)
{
	if (Rep <= HostileAt)
	{
		return EGFArenaStanding::Hostile;
	}
	if (Rep <= WaryAt)
	{
		return EGFArenaStanding::Wary;
	}
	if (Rep >= FriendlyAt)
	{
		return EGFArenaStanding::Friendly;
	}
	return EGFArenaStanding::Neutral;
}

bool FGFArenaRep::CanRent(int32 Rep)
{
	return Standing(Rep) != EGFArenaStanding::Hostile;
}

float FGFArenaRep::RentalPriceMul(int32 Rep)
{
	return Standing(Rep) == EGFArenaStanding::Wary ? 1.25f : 1.f;
}

bool FGFArenaRep::ArrivesWellFed(int32 Rep)
{
	return Standing(Rep) == EGFArenaStanding::Friendly;
}

bool FGFArenaRep::CanControl(int32 Rep)
{
	const EGFArenaStanding Band = Standing(Rep);
	return Band != EGFArenaStanding::Wary && Band != EGFArenaStanding::Hostile;
}

bool FGFArenaRep::ForcesChampion(int32 Rep)
{
	return Standing(Rep) == EGFArenaStanding::Hostile;
}

int32 FGFArenaPurse::Purse(int32 RewardValue)
{
	return FMath::Max(0, RewardValue) / 20;
}

int32 FGFArenaPurse::ElderCut(int32 WinningsValue)
{
	return FMath::Max(0, WinningsValue) / 10;
}

FString FGFArenaSidePots::PotWinner(EGFArenaOutcome Outcome, const TArray<FGFArenaSideBet>& Bets)
{
	int32 Side = INDEX_NONE;
	if (Outcome == EGFArenaOutcome::Team0Won)
	{
		Side = 0;
	}
	else if (Outcome == EGFArenaOutcome::Team1Won)
	{
		Side = 1;
	}
	else
	{
		return FString();
	}
	const FGFArenaSideBet* Best = nullptr;
	for (const FGFArenaSideBet& Bet : Bets)
	{
		if (Bet.Side != Side || Bet.Value <= 0 || Bet.Key.IsEmpty())
		{
			continue;
		}
		if (!Best || Bet.Value > Best->Value || (Bet.Value == Best->Value && Bet.Key < Best->Key))
		{
			Best = &Bet;
		}
	}
	return Best ? Best->Key : FString();
}

int32 FGFArenaCheese::CountInWindow(const TArray<double>& Times, double Now, double WindowSeconds)
{
	int32 N = 0;
	for (const double T : Times)
	{
		N += (Now >= T && Now - T < WindowSeconds) ? 1 : 0;
	}
	return N;
}

bool FGFArenaCheese::WagersBlocked(const TArray<double>& PairTimes, double Now)
{
	return CountInWindow(PairTimes, Now, HourSeconds) >= PvpWagersPerHour;
}

int32 FGFArenaCheese::RollStars(int32 Stars, int32 RollsThisHour)
{
	return RollsThisHour >= RollsPerHour ? 0 : Stars;
}

FName FGFArenaSnack::Check(bool bPlaced, bool bAlreadyFed, bool bHasItem, bool bFavourite)
{
	if (!bPlaced)
	{
		return TEXT("NotPlaced");
	}
	if (bAlreadyFed)
	{
		return TEXT("AlreadyFed");
	}
	if (!bHasItem)
	{
		return TEXT("NoFood");
	}
	if (!bFavourite)
	{
		return TEXT("Refused");
	}
	return TEXT("Ok");
}

FGFArenaPost FGFArenaPost::Brag(int32 Stars)
{
	FGFArenaPost Post;
	Post.Kind = TEXT("Brag");
	Post.Line = FString::Printf(TEXT("Three stars on the sand (%d)."), Stars);
	return Post;
}

FGFArenaPost FGFArenaPost::RentalQuit(FName Species)
{
	FGFArenaPost Post;
	Post.Kind = TEXT("RentalQuit");
	Post.Line = FString::Printf(TEXT("%s quit the bout and left the sand."), *Species.ToString());
	return Post;
}

FGFArenaPost FGFArenaPost::ElderNice(FName Family)
{
	FGFArenaPost Post;
	Post.Kind = TEXT("ElderNice");
	Post.Line = FString::Printf(TEXT("%s's elder liked that win."), *Family.ToString());
	return Post;
}
