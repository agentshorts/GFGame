#include "GFFactionLead.h"

FGFFactionLead::EResult FGFFactionLead::Decide(bool bHasPlayer, EGFFactionRank Rank, FName PlayerFaction, FName TargetFaction, const FString& PlayerKey, const FString& CurrentLeader)
{
	if (!bHasPlayer || PlayerKey.IsEmpty() || TargetFaction.IsNone())
	{
		return EResult::Missing;
	}
	if (PlayerFaction != TargetFaction)
	{
		return EResult::WrongFaction;
	}
	if (Rank != EGFFactionRank::Champion)
	{
		return EResult::NotChampion;
	}
	if (CurrentLeader == PlayerKey)
	{
		return EResult::Same;
	}
	return EResult::Ok;
}

FName FGFFactionLead::Code(EResult Result)
{
	switch (Result)
	{
	case EResult::Ok: return TEXT("Ok");
	case EResult::Same: return TEXT("Same");
	case EResult::NotChampion: return TEXT("NotChampion");
	case EResult::WrongFaction: return TEXT("WrongFaction");
	default: return TEXT("Missing");
	}
}
