#include "GFRunwayShow.h"
#include "Misc/SecureHash.h"
#include "GFDyes.h"

bool FGFRunwayShow::Start(const FGuid& InShowId, FName InThemeId, int32 InSeed, const TArray<FString>& PlayerKeys)
{
	if (Phase != EGFRunwayPhase::Idle && Phase != EGFRunwayPhase::Results)
	{
		return false;
	}
	if (!FGFFashionCatalog::FindTheme(InThemeId) || PlayerKeys.Num() == 0 || !InShowId.IsValid())
	{
		return false;
	}
	Contestants.Reset();
	for (const FString& Key : PlayerKeys)
	{
		if (Key.IsEmpty() || FindContestant(Key))
		{
			continue;
		}
		FGFRunwayContestant& C = Contestants.AddDefaulted_GetRef();
		C.PlayerKey = Key;
	}
	if (Contestants.Num() == 0)
	{
		return false;
	}
	ShowId = InShowId;
	ThemeId = InThemeId;
	Seed = InSeed;
	Walker = 0;
	Phase = EGFRunwayPhase::Dressing;
	PhaseTimeLeft = DressingSeconds;
	return true;
}

void FGFRunwayShow::Reset()
{
	Phase = EGFRunwayPhase::Idle;
	PhaseTimeLeft = 0.f;
	Contestants.Reset();
}

const FGFRunwayContestant* FGFRunwayShow::FindContestant(const FString& PlayerKey) const
{
	return Contestants.FindByPredicate([&PlayerKey](const FGFRunwayContestant& C) { return C.PlayerKey == PlayerKey; });
}

bool FGFRunwayShow::SetOutfit(const FString& PlayerKey, const FGFOutfit& Outfit)
{
	FGFRunwayContestant* C = Contestants.FindByPredicate([&PlayerKey](const FGFRunwayContestant& X) { return X.PlayerKey == PlayerKey; });
	if (!C || Phase != EGFRunwayPhase::Dressing || !Outfit.Validate())
	{
		return false;
	}
	C->Outfit = Outfit;
	return true;
}

bool FGFRunwayShow::SetReady(const FString& PlayerKey, bool bReady)
{
	FGFRunwayContestant* C = Contestants.FindByPredicate([&PlayerKey](const FGFRunwayContestant& X) { return X.PlayerKey == PlayerKey; });
	if (!C || Phase != EGFRunwayPhase::Dressing)
	{
		return false;
	}
	C->bReady = bReady;
	if (!Contestants.ContainsByPredicate([](const FGFRunwayContestant& X) { return !X.bReady; }))
	{
		EnterWalking(); // everyone's ready: no need to wait out the timer
	}
	return true;
}

void FGFRunwayShow::EnterWalking()
{
	Phase = EGFRunwayPhase::Walking;
	Walker = 0;
	PhaseTimeLeft = WalkSeconds;
}

void FGFRunwayShow::Advance(float DeltaSeconds)
{
	// Loop so one big step can cross several phases (tests, hitches).
	float Left = FMath::Max(0.f, DeltaSeconds);
	while (Left > 0.f && Phase != EGFRunwayPhase::Idle && Phase != EGFRunwayPhase::Results)
	{
		const float Step = FMath::Min(Left, PhaseTimeLeft);
		PhaseTimeLeft -= Step;
		Left -= Step;
		if (PhaseTimeLeft > 0.f)
		{
			break;
		}
		switch (Phase)
		{
		case EGFRunwayPhase::Dressing:
			EnterWalking();
			break;
		case EGFRunwayPhase::Walking:
			if (++Walker >= Contestants.Num())
			{
				Phase = EGFRunwayPhase::Judging;
				PhaseTimeLeft = JudgingSeconds;
				JudgeAll();
			}
			else
			{
				PhaseTimeLeft = WalkSeconds;
			}
			break;
		case EGFRunwayPhase::Judging:
			Phase = EGFRunwayPhase::Results;
			PhaseTimeLeft = 0.f;
			break;
		default:
			break;
		}
	}
}

void FGFRunwayShow::JudgeAll()
{
	const FGFFashionTheme* Theme = FGFFashionCatalog::FindTheme(ThemeId);
	check(Theme);
	static const TArray<FGFOutfit> NoHistory;
	for (FGFRunwayContestant& C : Contestants)
	{
		const TArray<FGFOutfit>* Past = History.Find(C.PlayerKey);
		C.Score = FGFRunwayJudge::Judge(C.Outfit, *Theme, Past ? *Past : NoHistory, Seed, C.PlayerKey);
	}
	// Rank: best total first; ties go to the player key so the order never depends on join order.
	TArray<int32> Order;
	for (int32 i = 0; i < Contestants.Num(); ++i)
	{
		Order.Add(i);
	}
	Order.Sort([this](int32 A, int32 B)
	{
		const FGFRunwayContestant& X = Contestants[A];
		const FGFRunwayContestant& Y = Contestants[B];
		return X.Score.Total != Y.Score.Total ? X.Score.Total > Y.Score.Total : X.PlayerKey < Y.PlayerKey;
	});
	for (int32 r = 0; r < Order.Num(); ++r)
	{
		FGFRunwayContestant& C = Contestants[Order[r]];
		C.Rank = r + 1;
		// An empty outfit (nobody dressed) earns nothing.
		const bool bWinner = r == 0 && Contestants.Num() > 1;
		C.Rewards = C.Outfit.IsEmpty() ? TArray<FGFItemStack>() : FGFRunwayJudge::RewardsFor(C.Score.Tier, bWinner, Rewards);
		History.FindOrAdd(C.PlayerKey).Add(C.Outfit);
	}
}

FGuid FGFRunwayShow::MakeTxId(const FGuid& InShowId, const FString& PlayerKey, const FString& What)
{
	const FString Text = FString::Printf(TEXT("GF.Fashion|%s|%s|%s"), *InShowId.ToString(EGuidFormats::Digits), *PlayerKey, *What);
	const FTCHARToUTF8 Utf8(*Text);
	FMD5 Md5;
	Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	uint8 Digest[16];
	Md5.Final(Digest);
	uint32 W[4];
	FMemory::Memcpy(W, Digest, sizeof(W));
	return FGuid(W[0], W[1], W[2], W[3] | 1u); // never the all-zero (invalid) guid
}

FGuid FGFRunwayShow::DyeTxId(const FString& PlayerKey, EGFOutfitSlot Slot, const FLinearColor& Color) const
{
	const FColor C = Color.ToFColor(false);
	return MakeTxId(ShowId, PlayerKey, FString::Printf(TEXT("Dye.%d.%s.%02x%02x%02x"), static_cast<int32>(Slot),
		*FGFDyes::DyeForColor(Color).ToString(), C.R, C.G, C.B));
}

int32 FGFRunwayShow::GrantRewards(IGFLedgerAccounts& Accounts, FGFTransferLedger& Ledger) const
{
	if (Phase != EGFRunwayPhase::Results && Phase != EGFRunwayPhase::Judging)
	{
		return 0;
	}
	int32 Applied = 0;
	for (const FGFRunwayContestant& C : Contestants)
	{
		if (C.Rewards.Num() > 0 && Ledger.Mint(Accounts, RewardTxId(C.PlayerKey), C.PlayerKey, C.Rewards, TEXT("Fashion.Runway")) == EGFTxResult::Applied)
		{
			++Applied;
		}
	}
	return Applied;
}
