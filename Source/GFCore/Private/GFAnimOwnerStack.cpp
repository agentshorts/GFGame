#include "GFAnimOwnerStack.h"

const TCHAR* GFAnimOwnerName(EGFAnimOwner Owner)
{
	switch (Owner)
	{
	case EGFAnimOwner::Locomotion: return TEXT("Locomotion");
	case EGFAnimOwner::BeatHop: return TEXT("BeatHop");
	case EGFAnimOwner::Mining: return TEXT("Mining");
	case EGFAnimOwner::Emote: return TEXT("Emote");
	case EGFAnimOwner::FistScope: return TEXT("FistScope");
	case EGFAnimOwner::Climbing: return TEXT("Climbing");
	case EGFAnimOwner::WallRun: return TEXT("WallRun");
	case EGFAnimOwner::Mantling: return TEXT("Mantling");
	case EGFAnimOwner::FerrySeat: return TEXT("FerrySeat");
	default: return TEXT("Unknown");
	}
}

bool FGFAnimOwnerStack::Claim(EGFAnimOwner Owner, FName Key, bool bLoop, float Seconds, float PlayRate)
{
	if (Owner == EGFAnimOwner::Locomotion || Key.IsNone())
	{
		return false;
	}
	Release(Owner);
	FGFAnimClaim New;
	New.Owner = Owner;
	New.Key = Key;
	New.bLoop = bLoop;
	New.PlayRate = PlayRate;
	New.bTimed = Seconds > 0.f;
	New.Remaining = New.bTimed ? Seconds : 0.f;
	New.Serial = NextSerial++;
	if (NextSerial == 0)
	{
		NextSerial = 1; // 0 means "nothing applied" to UGFSimpleAnimComponent
	}
	int32 At = 0;
	while (At < Claims.Num() && Claims[At].Owner > Owner)
	{
		++At;
	}
	Claims.Insert(New, At);
	return At == 0;
}

bool FGFAnimOwnerStack::Release(EGFAnimOwner Owner)
{
	return Claims.RemoveAll([Owner](const FGFAnimClaim& C) { return C.Owner == Owner; }) > 0;
}

bool FGFAnimOwnerStack::ReleaseKey(EGFAnimOwner Owner, FName Key)
{
	return Claims.RemoveAll([Owner, Key](const FGFAnimClaim& C) { return C.Owner == Owner && C.Key == Key; }) > 0;
}

bool FGFAnimOwnerStack::Renew(EGFAnimOwner Owner, float Seconds)
{
	for (FGFAnimClaim& C : Claims)
	{
		if (C.Owner == Owner)
		{
			if (C.bTimed && Seconds > 0.f)
			{
				C.Remaining = Seconds;
			}
			return true;
		}
	}
	return false;
}

bool FGFAnimOwnerStack::SetPlayRate(EGFAnimOwner Owner, float PlayRate)
{
	for (FGFAnimClaim& C : Claims)
	{
		if (C.Owner == Owner)
		{
			C.PlayRate = PlayRate;
			return true;
		}
	}
	return false;
}

bool FGFAnimOwnerStack::Tick(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f)
	{
		return false;
	}
	for (FGFAnimClaim& C : Claims)
	{
		if (C.bTimed)
		{
			C.Remaining -= DeltaSeconds;
		}
	}
	return Claims.RemoveAll([](const FGFAnimClaim& C) { return C.bTimed && C.Remaining <= 0.f; }) > 0;
}

const FGFAnimClaim* FGFAnimOwnerStack::Top() const
{
	return Claims.Num() > 0 ? &Claims[0] : nullptr;
}

EGFAnimOwner FGFAnimOwnerStack::TopOwner() const
{
	const FGFAnimClaim* T = Top();
	return T ? T->Owner : EGFAnimOwner::Locomotion;
}

const FGFAnimClaim* FGFAnimOwnerStack::Find(EGFAnimOwner Owner) const
{
	return Claims.FindByPredicate([Owner](const FGFAnimClaim& C) { return C.Owner == Owner; });
}
