#include "GFCompanionRoster.h"

FGuid FGFCompanions::AddTamed(TArray<FGFCompanionEntry>& Roster, FName Species, const FString& Name)
{
	const bool bRoom = PartySize(Roster) < MaxParty;
	FGFCompanionEntry& E = Roster.AddDefaulted_GetRef();
	E.Id = FGuid::NewGuid();
	E.Species = Species;
	E.Name = Name.IsEmpty() ? Species.ToString() : Name;
	E.bInParty = bRoom;
	return E.Id;
}

int32 FGFCompanions::PartySize(const TArray<FGFCompanionEntry>& Roster)
{
	int32 N = 0;
	for (const FGFCompanionEntry& E : Roster)
	{
		N += E.bInParty ? 1 : 0;
	}
	return N;
}

bool FGFCompanions::AddXP(FGFCompanionEntry& Entry, int32 Amount)
{
	const int32 Before = Entry.Level;
	Entry.XP = FMath::Max(0, Entry.XP + Amount);
	Entry.Level = FMath::Clamp(1 + Entry.XP / XPPerLevel, 1, MaxLevel);
	return Entry.Level > Before;
}

FGFCompanionEntry* FGFCompanions::Find(TArray<FGFCompanionEntry>& Roster, const FGuid& Id)
{
	return Roster.FindByPredicate([&](const FGFCompanionEntry& E) { return E.Id == Id; });
}

const FGFCompanionEntry* FGFCompanions::Find(const TArray<FGFCompanionEntry>& Roster, const FGuid& Id)
{
	return Roster.FindByPredicate([&](const FGFCompanionEntry& E) { return E.Id == Id; });
}

FGuid UGFCompanionRoster::AddCompanion(FName Species, const FString& Name)
{
	return FGFCompanions::AddTamed(Companions, Species, Name);
}
