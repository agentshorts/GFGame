#include "GFSocialTypes.h"
#include "GFDataTables.h"
#include "GFAnimMap.h"

const FName FGFSocialCodes::Ok(TEXT("Ok"));
const FName FGFSocialCodes::NotSafe(TEXT("NotSafe"));
const FName FGFSocialCodes::TargetNotSafe(TEXT("TargetNotSafe"));
const FName FGFSocialCodes::SameFaction(TEXT("SameFaction"));
const FName FGFSocialCodes::NoFaction(TEXT("NoFaction"));
const FName FGFSocialCodes::Self(TEXT("Self"));
const FName FGFSocialCodes::Jailed(TEXT("Jailed"));
const FName FGFSocialCodes::TargetJailed(TEXT("TargetJailed"));
const FName FGFSocialCodes::Cooldown(TEXT("Cooldown"));
const FName FGFSocialCodes::OutOfRange(TEXT("OutOfRange"));
const FName FGFSocialCodes::NotAllowed(TEXT("NotAllowed"));
const FName FGFSocialCodes::TargetIsHost(TEXT("TargetIsHost"));
const FName FGFSocialCodes::AlreadyJailed(TEXT("AlreadyJailed"));
const FName FGFSocialCodes::NotJailed(TEXT("NotJailed"));
const FName FGFSocialCodes::NoSuchPlayer(TEXT("NoSuchPlayer"));
const FName FGFSocialCodes::UnknownEmote(TEXT("UnknownEmote"));
const FName FGFSocialCodes::NotBanned(TEXT("NotBanned"));
const FName FGFSocialCodes::Muted(TEXT("Muted"));

FName FGFSocialRules::CheckPower(const FGFSocialPlayer& By, const FGFSocialPlayer& Target, float Distance, bool bOnCooldown, const FGFSocialTuning& T)
{
	if (By.Key == Target.Key)
	{
		return FGFSocialCodes::Self;
	}
	if (By.bJailed)
	{
		return FGFSocialCodes::Jailed;
	}
	if (Target.bJailed)
	{
		return FGFSocialCodes::TargetJailed;
	}
	if (!By.bSafe)
	{
		return FGFSocialCodes::NotSafe;
	}
	if (!Target.bSafe)
	{
		return FGFSocialCodes::TargetNotSafe;
	}
	if (By.Faction.IsNone() || Target.Faction.IsNone())
	{
		return FGFSocialCodes::NoFaction;
	}
	if (By.Faction == Target.Faction)
	{
		return FGFSocialCodes::SameFaction;
	}
	if (Distance > T.PowerRange)
	{
		return FGFSocialCodes::OutOfRange;
	}
	if (bOnCooldown)
	{
		return FGFSocialCodes::Cooldown;
	}
	return FGFSocialCodes::Ok;
}

float FGFSocialRules::ClampScale(float Scale, const FGFSocialTuning& T)
{
	return FMath::Clamp(Scale, T.MinScale, T.MaxScale);
}

float FGFSocialRules::StepScale(float Current, bool bGrow, const FGFSocialTuning& T)
{
	const float Step = FMath::Max(1.01f, T.ScaleStep);
	const float Next = bGrow ? Current * Step : Current / Step;
	// Snap to 1x when a step lands within a hair of it (grow then shrink gets back to exactly normal).
	return FMath::IsNearlyEqual(Next, 1.f, 0.001f) ? 1.f : ClampScale(Next, T);
}

FName FGFSocialRules::JailPower(const FGFSocialPlayer& By, const FGFSocialPlayer& Target)
{
	if (By.Key == Target.Key)
	{
		return FGFSocialCodes::Self;
	}
	if (By.bJailed)
	{
		return FGFSocialCodes::Jailed; // no jail power from inside the cell
	}
	if (By.bHost)
	{
		return FGFSocialCodes::Ok;
	}
	if (Target.bHost)
	{
		return FGFSocialCodes::TargetIsHost;
	}
	if (By.bLeader && !By.Faction.IsNone() && By.Faction == Target.Faction)
	{
		return FGFSocialCodes::Ok;
	}
	return FGFSocialCodes::NotAllowed;
}

FName FGFSocialRules::CanJail(const FGFSocialPlayer& By, const FGFSocialPlayer& Target)
{
	if (By.Key != Target.Key && Target.bJailed)
	{
		return FGFSocialCodes::AlreadyJailed;
	}
	return JailPower(By, Target);
}

FName FGFSocialRules::CanRelease(const FGFSocialPlayer& By, const FGFSocialPlayer& Target)
{
	if (By.Key != Target.Key && !Target.bJailed)
	{
		return FGFSocialCodes::NotJailed;
	}
	return JailPower(By, Target);
}

FString FGFSocialRules::ChooseLeader(const TArray<FGFSocialPlayer>& Online)
{
	const FGFSocialPlayer* BestFree = nullptr;
	const FGFSocialPlayer* BestAny = nullptr;
	auto Before = [](const FGFSocialPlayer& A, const FGFSocialPlayer& B)
	{
		const bool ALate = A.ClaimOrder <= 0;
		const bool BLate = B.ClaimOrder <= 0;
		if (ALate != BLate)
		{
			return BLate; // a recorded claim comes before "no order"
		}
		if (!ALate && A.ClaimOrder != B.ClaimOrder)
		{
			return A.ClaimOrder < B.ClaimOrder;
		}
		return A.Key.Compare(B.Key) < 0;
	};
	for (const FGFSocialPlayer& M : Online)
	{
		if (M.Key.IsEmpty())
		{
			continue;
		}
		if (!BestAny || Before(M, *BestAny))
		{
			BestAny = &M;
		}
		if (!M.bJailed && (!BestFree || Before(M, *BestFree)))
		{
			BestFree = &M;
		}
	}
	return BestFree ? BestFree->Key : (BestAny ? BestAny->Key : FString());
}

FName FGFSocialRules::CanKickOrBan(const FGFSocialPlayer& By, const FGFSocialPlayer& Target)
{
	if (By.Key == Target.Key)
	{
		return FGFSocialCodes::Self;
	}
	if (Target.bHost)
	{
		return FGFSocialCodes::TargetIsHost;
	}
	return By.bHost ? FGFSocialCodes::Ok : FGFSocialCodes::NotAllowed;
}

const FGFModeRulesRow* FGFSocialRules::FindModeRules(FName Mode)
{
	const UDataTable* Table = FGFDataTables::Get<FGFModeRulesRow>(TEXT("DT_ModeRules"));
	return Table && !Mode.IsNone() ? Table->FindRow<FGFModeRulesRow>(Mode, TEXT("ModeRules"), false) : nullptr;
}

bool FGFSocialRules::EmoteAttractsDanger(FName Mode)
{
	const FGFModeRulesRow* Row = FindModeRules(Mode);
	return Row && Row->bEmoteAttractsDanger;
}

bool FGFSocialRules::EmoteMakesNoise(bool bSafe, FName Mode)
{
	return !bSafe && EmoteAttractsDanger(Mode);
}

const FGFEmoteRow* FGFSocialRules::FindEmote(FName EmoteId)
{
	const UDataTable* Table = FGFDataTables::Get<FGFEmoteRow>(TEXT("DT_Emotes"));
	return Table && !EmoteId.IsNone() ? Table->FindRow<FGFEmoteRow>(EmoteId, TEXT("Emote"), false) : nullptr;
}

TArray<FName> FGFSocialRules::EmoteIds()
{
	const UDataTable* Table = FGFDataTables::Get<FGFEmoteRow>(TEXT("DT_Emotes"));
	return Table ? Table->GetRowNames() : TArray<FName>();
}

FName FGFSocialRules::EmoteAnimKey(const FGFEmoteRow& Row)
{
	return !Row.AnimKey.IsNone() && FGFAnimMap::Find(Row.AnimKey) ? Row.AnimKey : NAME_None;
}

bool FGFCooldowns::IsCooling(const FString& Key, FName Action, double Now) const
{
	const double* U = Until.Find(Slot(Key, Action));
	return U && Now < *U;
}

void FGFCooldowns::Start(const FString& Key, FName Action, double Now, double Seconds)
{
	Until.Add(Slot(Key, Action), Now + Seconds);
}

float FGFScaleBook::Get(const FString& Key) const
{
	const FGFScaleEntry* E = Entries.FindByPredicate([&](const FGFScaleEntry& X) { return X.Key == Key; });
	return E ? E->Scale : 1.f;
}

float FGFScaleBook::Step(const FString& Key, bool bGrow, const FGFSocialTuning& T)
{
	const float Next = FGFSocialRules::StepScale(Get(Key), bGrow, T);
	FGFScaleEntry* E = Entries.FindByPredicate([&](const FGFScaleEntry& X) { return X.Key == Key; });
	if (Next == 1.f)
	{
		Reset(Key);
	}
	else if (E)
	{
		E->Scale = Next;
	}
	else
	{
		FGFScaleEntry New;
		New.Key = Key;
		New.Scale = Next;
		Entries.Add(New);
	}
	return Next;
}

void FGFScaleBook::Reset(const FString& Key)
{
	Entries.RemoveAll([&](const FGFScaleEntry& X) { return X.Key == Key; });
}

TArray<FString> FGFScaleBook::RevertUnsafe(TFunctionRef<bool(const FString&)> IsSafe)
{
	TArray<FString> Out;
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		if (!IsSafe(Entries[i].Key))
		{
			Out.Add(Entries[i].Key);
			Entries.RemoveAt(i);
		}
	}
	return Out;
}
