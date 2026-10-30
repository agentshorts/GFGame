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
const FName FGFSocialCodes::TargetIsGuest(TEXT("TargetIsGuest"));
const FName FGFSocialCodes::NotMember(TEXT("NotMember"));
const FName FGFSocialCodes::VisitorNoRights(TEXT("VisitorNoRights"));
const FName FGFSocialCodes::Unchanged(TEXT("Unchanged"));
const FName FGFSocialCodes::NotOnYourLand(TEXT("NotOnYourLand"));
const FName FGFSocialCodes::TargetProtected(TEXT("TargetProtected"));

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
	if (Target.bProtected)
	{
		return FGFSocialCodes::TargetProtected; // members at home, the host, guests, protected visitors
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
	if (By.bVisitorNoRights)
	{
		return FGFSocialCodes::VisitorNoRights; // on another faction's land without guest status
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

// --- Faction protection ---

const FGFVisitorAccess* FGFFactionAccess::Find(FName Land, const FString& Key) const
{
	return Visitors.FindByPredicate([&](const FGFVisitorAccess& V) { return V.LandFaction == Land && V.Key == Key; });
}

FGFVisitorAccess& FGFFactionAccess::FindOrAdd(FName Land, const FString& Key)
{
	if (FGFVisitorAccess* Found = Visitors.FindByPredicate([&](const FGFVisitorAccess& V) { return V.LandFaction == Land && V.Key == Key; }))
	{
		return *Found;
	}
	FGFVisitorAccess& Added = Visitors.AddDefaulted_GetRef();
	Added.LandFaction = Land;
	Added.Key = Key;
	return Added;
}

bool FGFFactionAccess::IsProtected(FName Land, const FString& Key) const
{
	const FGFVisitorAccess* V = Find(Land, Key);
	return V && (V->bProtected || V->bGuest);
}

bool FGFFactionAccess::IsGuest(FName Land, const FString& Key) const
{
	const FGFVisitorAccess* V = Find(Land, Key);
	return V && V->bGuest;
}

bool FGFFactionAccess::HasToggleGrant(FName Faction, const FString& MemberKey) const
{
	return !Faction.IsNone() && Grants.ContainsByPredicate([&](const FGFToggleGrant& G) { return G.Faction == Faction && G.MemberKey == MemberKey; });
}

bool FGFFactionAccess::SetToggleGrant(FName Faction, const FString& MemberKey, bool bAllowed)
{
	if (bAllowed == HasToggleGrant(Faction, MemberKey))
	{
		return false;
	}
	if (bAllowed)
	{
		FGFToggleGrant G;
		G.Faction = Faction;
		G.MemberKey = MemberKey;
		Grants.Add(G);
	}
	else
	{
		Grants.RemoveAll([&](const FGFToggleGrant& G) { return G.Faction == Faction && G.MemberKey == MemberKey; });
	}
	return true;
}

void FGFFactionAccess::OnFactionChanged(const FString& Key, FName NewFaction)
{
	Grants.RemoveAll([&](const FGFToggleGrant& G) { return G.MemberKey == Key; });
	if (!NewFaction.IsNone())
	{
		Visitors.RemoveAll([&](const FGFVisitorAccess& V) { return V.Key == Key && V.LandFaction == NewFaction; });
	}
}

FName FGFProtectionRules::CanToggle(const FGFSocialPlayer& By, const FGFSocialPlayer& Target, const FGFFactionAccess& Access, bool bProtect)
{
	if (By.Key == Target.Key)
	{
		return FGFSocialCodes::Self;
	}
	if (By.Faction.IsNone())
	{
		return FGFSocialCodes::NoFaction;
	}
	if (By.bJailed)
	{
		return FGFSocialCodes::Jailed;
	}
	if (!By.bLeader && !Access.HasToggleGrant(By.Faction, By.Key))
	{
		return FGFSocialCodes::NotAllowed;
	}
	if (Target.Faction == By.Faction)
	{
		return FGFSocialCodes::SameFaction; // members are always safe on their own land
	}
	if (Target.bHost)
	{
		return FGFSocialCodes::TargetIsHost; // the host is always protected
	}
	if (Access.IsGuest(By.Faction, Target.Key))
	{
		// Both ways: a guest is already protected, and an explicit Protect on top would be undone by Unguest anyway.
		return FGFSocialCodes::TargetIsGuest;
	}
	if (Target.LandOwner != By.Faction)
	{
		return FGFSocialCodes::NotOnYourLand; // toggles only reach players standing on the toggler's faction land
	}
	return FGFSocialCodes::Ok;
}

FName FGFProtectionRules::CanSetGuest(const FGFSocialPlayer& By, const FGFSocialPlayer& Target, const FGFFactionAccess& Access)
{
	if (By.Key == Target.Key)
	{
		return FGFSocialCodes::Self;
	}
	if (By.Faction.IsNone())
	{
		return FGFSocialCodes::NoFaction;
	}
	if (By.bJailed)
	{
		return FGFSocialCodes::Jailed;
	}
	if (!By.bLeader && !Access.HasToggleGrant(By.Faction, By.Key))
	{
		return FGFSocialCodes::NotAllowed; // the leader, or a member the leader granted
	}
	if (Target.Faction == By.Faction)
	{
		return FGFSocialCodes::SameFaction;
	}
	if (Target.bHost)
	{
		return FGFSocialCodes::TargetIsHost;
	}
	if (Target.LandOwner != By.Faction)
	{
		return FGFSocialCodes::NotOnYourLand; // like the toggles: the target must stand on the setter's faction land
	}
	return FGFSocialCodes::Ok;
}

FName FGFProtectionRules::CanGrantToggle(const FGFSocialPlayer& By, const FGFSocialPlayer& Target)
{
	if (By.Key == Target.Key)
	{
		return FGFSocialCodes::Self;
	}
	if (By.Faction.IsNone())
	{
		return FGFSocialCodes::NoFaction;
	}
	if (!By.bLeader)
	{
		return FGFSocialCodes::NotAllowed;
	}
	if (Target.Faction != By.Faction)
	{
		return FGFSocialCodes::NotMember;
	}
	return FGFSocialCodes::Ok;
}

bool FGFProtectionRules::IsProtectedOn(const FString& Key, FName Faction, bool bHost, FName LandOwner, const FGFFactionAccess& Access)
{
	if (LandOwner.IsNone())
	{
		return false;
	}
	if (Faction == LandOwner || bHost)
	{
		return true;
	}
	return !Key.IsEmpty() && Access.IsProtected(LandOwner, Key);
}

bool FGFProtectionRules::HasVisitorRights(const FString& Key, FName Faction, bool bHost, FName LandOwner, const FGFFactionAccess& Access)
{
	if (LandOwner.IsNone() || bHost || Faction == LandOwner)
	{
		return true;
	}
	return !Key.IsEmpty() && Access.IsGuest(LandOwner, Key);
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
