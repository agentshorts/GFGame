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

FGuid UGFCompanionRoster::AddCompanion(FName Species, const FString& Name)
{
	return FGFCompanions::AddTamed(Companions, Species, Name);
}
