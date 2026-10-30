#include "GFGiantTypes.h"
#include "GFDataTables.h"
#include "GFLineTokens.h"
#include "GFSafeZone.h"

const TCHAR* FGFGiantRules::TargetToken = TEXT("{Target}");

// ---------------- Save ----------------

int32 FGFGiantSave::Touch(const FString& Key, int64 NowTicks)
{
	int32 i = IndexOf(Key);
	if (i == INDEX_NONE)
	{
		i = PlayerKeys.Add(Key);
		PlayerSeconds.SetNum(PlayerKeys.Num());
		FirstSeenUtcTicks.SetNum(PlayerKeys.Num());
		PlayerSeconds[i] = 0.0;
		FirstSeenUtcTicks[i] = NowTicks;
	}
	PlayerSeconds.SetNum(PlayerKeys.Num());
	FirstSeenUtcTicks.SetNum(PlayerKeys.Num());
	return i;
}

double FGFGiantSave::SecondsOf(const FString& Key) const
{
	const int32 i = IndexOf(Key);
	return PlayerSeconds.IsValidIndex(i) ? PlayerSeconds[i] : 0.0;
}

int64 FGFGiantSave::FirstSeenOf(const FString& Key) const
{
	const int32 i = IndexOf(Key);
	return FirstSeenUtcTicks.IsValidIndex(i) ? FirstSeenUtcTicks[i] : 0;
}

// ---------------- Lines ----------------

FGFGiantLines FGFGiantLines::Load()
{
	return FromTable(FGFDataTables::Get<FGFGiantLineRow>(TEXT("DT_GiantDinoLines")));
}

FGFGiantLines FGFGiantLines::FromTable(const UDataTable* Table)
{
	FGFGiantLines Out;
	if (!Table)
	{
		return Out;
	}
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FGFGiantLineRow* Row = reinterpret_cast<const FGFGiantLineRow*>(Pair.Value);
		if (Row && !Row->Line.IsEmpty())
		{
			Out.ByPhase.FindOrAdd(Row->Phase).Add({ Pair.Key, Row->Line });
		}
	}
	return Out;
}

int32 FGFGiantLines::Total() const
{
	int32 N = 0;
	for (const TPair<EGFGiantLinePhase, TArray<FLine>>& P : ByPhase)
	{
		N += P.Value.Num();
	}
	return N;
}

// ---------------- Rules ----------------

void FGFGiantRules::AccumulatePlay(FGFGiantSave& Save, double Dt, int32 NumPlayers)
{
	if (Dt > 0.0 && NumPlayers > 0)
	{
		Save.PlaySecondsSinceLast += Dt * NumPlayers;
	}
}

void FGFGiantRules::StartClockIfNeeded(FGFGiantSave& Save, int64 NowTicks)
{
	if (Save.LastEventUtcTicks == 0)
	{
		Save.LastEventUtcTicks = NowTicks;
	}
}

bool FGFGiantRules::ShouldTrigger(const FGFGiantTuning& T, const FGFGiantSave& Save, int64 NowTicks, bool bAnyoneOutside)
{
	if (!bAnyoneOutside || Save.LastEventUtcTicks == 0)
	{
		return false;
	}
	const double RealSeconds = double(NowTicks - Save.LastEventUtcTicks) / TicksPerSecond;
	return RealSeconds >= T.MinRealSecondsBetween && Save.PlaySecondsSinceLast >= T.MinPlaySecondsSinceLast;
}

bool FGFGiantRules::MarkEventStarted(FGFGiantSave& Save, int64 NowTicks)
{
	const bool bReturn = Save.EventCount > 0;
	++Save.EventCount;
	Save.LastEventUtcTicks = NowTicks;
	Save.PlaySecondsSinceLast = 0.0;
	return bReturn;
}

FGFSafeRegion FGFGiantRules::DefaultRegion(const FGFGiantTuning& T, const FVector& Center, int32 Members)
{
	FGFSafeRegion R;
	R.Id = TEXT("Default");
	R.Center = Center;
	R.Members = FMath::Max(1, Members);
	R.BaseRadius = T.CircleBaseRadius;
	R.RadiusPerExtraMember = T.CircleRadiusPerExtraPlayer;
	R.MaxRadius = T.CircleMaxRadius;
	return R;
}

bool FGFGiantRules::IsExposed(const IGFSafeZoneQuery& Zone, const TArray<FBox>& /*NoEntry*/, const FGFGiantPlayerView& P)
{
	// A jail box does not hide a player. Jailed players are not protected, so he can target them and run up to the box.
	return !Zone.IsSafeFor(P.Subject(), P.Location);
}

bool FGFGiantRules::CanTouch(const IGFSafeZoneQuery& Zone, const TArray<FBox>& NoEntry, const FGFGiantPlayerView& P)
{
	return IsExposed(Zone, NoEntry, P) && !FGFNoEntry::IsInside(NoEntry, P.Location);
}

FVector FGFGiantRules::NoEntryApproach(const FGFGiantTuning& T, const TArray<FBox>& NoEntry, const FVector& From, const FVector& Target)
{
	const FBox* Box = nullptr;
	for (const FBox& B : NoEntry)
	{
		if (B.IsValid && B.IsInsideOrOn(Target))
		{
			Box = &B;
			break;
		}
	}
	if (!Box)
	{
		return Target;
	}
	const float R = FMath::Max(0.f, T.GiantRadius);
	const float MinX = Box->Min.X - R;
	const float MaxX = Box->Max.X + R;
	const float MinY = Box->Min.Y - R;
	const float MaxY = Box->Max.Y + R;
	// Closest point on the expanded rectangle. From outside, that point is on the edge he can actually stand on.
	return FVector(FMath::Clamp(From.X, MinX, MaxX), FMath::Clamp(From.Y, MinY, MaxY), From.Z);
}

bool FGFGiantRules::AnyoneOutside(const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players, const TArray<FBox>& NoEntry)
{
	return Players.ContainsByPredicate([&](const FGFGiantPlayerView& P) { return IsExposed(Zone, NoEntry, P); });
}

FVector FGFGiantRules::ClampOutside(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& P)
{
	return FGFSafeZone::KeepOut(Zone, P, T.GiantRadius);
}

namespace
{
	/** Newest exposed player, matching PickTarget. With bHandoffRank, a hold beats a revoke and a newer revoke wins. */
	bool PrefersPlayer(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FGFGiantPlayerView& Challenger, const FGFGiantPlayerView& Best, bool bHandoffRank)
	{
		if (bHandoffRank)
		{
			const int32 RankC = Challenger.bHeld ? 0 : 1;
			const int32 RankB = Best.bHeld ? 0 : 1;
			if (RankC != RankB)
			{
				return RankC < RankB;
			}
			if (RankC == 1 && FMath::Abs(Challenger.DragAgeSeconds - Best.DragAgeSeconds) > 0.25f)
			{
				return Challenger.DragAgeSeconds < Best.DragAgeSeconds;
			}
		}
		if (FMath::Abs(Challenger.PlaySeconds - Best.PlaySeconds) > T.NewestTieSeconds)
		{
			return Challenger.PlaySeconds < Best.PlaySeconds;
		}
		if (Challenger.FirstSeenTicks != Best.FirstSeenTicks)
		{
			return Challenger.FirstSeenTicks > Best.FirstSeenTicks;
		}
		return FGFSafeZone::DistanceOutside(Zone, Challenger.Location) > FGFSafeZone::DistanceOutside(Zone, Best.Location);
	}

	int32 BestPlayer(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players, const TArray<int32>& Indices, bool bHandoffRank)
	{
		int32 Best = INDEX_NONE;
		for (int32 i : Indices)
		{
			if (Best == INDEX_NONE || PrefersPlayer(T, Zone, Players[i], Players[Best], bHandoffRank))
			{
				Best = i;
			}
		}
		return Best;
	}
}

int32 FGFGiantRules::PickTarget(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players, const TArray<FBox>& NoEntry)
{
	TArray<int32> Exposed;
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		if (IsExposed(Zone, NoEntry, Players[i]))
		{
			Exposed.Add(i);
		}
	}
	return BestPlayer(T, Zone, Players, Exposed, false);
}

bool FGFGiantRules::IsHandoffCandidate(const FGFGiantTuning& T, const FGFGiantPlayerView& P)
{
	const bool bHeldByOther = P.bHeld && P.HolderKey != P.Key && (!P.HolderKey.IsEmpty() || !P.HolderName.IsEmpty());
	if (bHeldByOther)
	{
		return true;
	}
	const bool bNamed = !P.DraggerKey.IsEmpty() || !P.Dragger.IsEmpty();
	const bool bSelf = !P.DraggerKey.IsEmpty() && P.DraggerKey == P.Key;
	return bNamed && !bSelf && P.DragAgeSeconds >= 0.f && P.DragAgeSeconds <= T.HandoffWindowSeconds;
}

FString FGFGiantRules::SetupName(const FGFGiantPlayerView& P)
{
	return !P.Dragger.IsEmpty() ? P.Dragger : P.HolderName;
}

FGFGiantPick FGFGiantRules::PickInterest(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players, const TArray<FBox>& NoEntry, const FGFGiantNotice* Notice, const TSet<FString>* Skip)
{
	FGFGiantPick Out;
	TArray<int32> Exposed;
	TArray<int32> Handoffs;
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		if (Skip && Skip->Contains(Players[i].Key) && FGFNoEntry::IsInside(NoEntry, Players[i].Location))
		{
			continue;
		}
		if (!IsExposed(Zone, NoEntry, Players[i]))
		{
			continue;
		}
		Exposed.Add(i);
		if (IsHandoffCandidate(T, Players[i]))
		{
			Handoffs.Add(i);
		}
	}
	if (Handoffs.Num() > 0)
	{
		Out.Reason = EGFGiantInterest::Handoff;
		Out.Index = BestPlayer(T, Zone, Players, Handoffs, true);
		return Out;
	}
	const bool bNotice = Notice && Notice->bActive && Notice->Radius > 0.f;
	if (bNotice)
	{
		TArray<int32> Near;
		for (int32 i : Exposed)
		{
			if (FVector::Dist2D(Players[i].Location, Notice->At) <= Notice->Radius)
			{
				Near.Add(i);
			}
		}
		if (Near.Num() == 0)
		{
			Out.Reason = EGFGiantInterest::Look;
			return Out;
		}
		Out.Reason = EGFGiantInterest::Taunt;
		Out.Index = BestPlayer(T, Zone, Players, Near, false);
		return Out;
	}
	if (Exposed.Num() == 0)
	{
		return Out;
	}
	Out.Reason = EGFGiantInterest::Normal;
	Out.Index = BestPlayer(T, Zone, Players, Exposed, false);
	return Out;
}

FVector FGFGiantRules::HerdGoal(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& Target)
{
	FVector Away = -FGFSafeZone::DirToSafe(Zone, Target);
	if (Away.IsNearlyZero())
	{
		Away = FVector(1.f, 0.f, 0.f);
	}
	return Target + Away * T.HerdOffset;
}

FVector FGFGiantRules::ShoveVelocity(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& P)
{
	FVector In = FGFSafeZone::DirToSafe(Zone, P);
	if (In.IsNearlyZero())
	{
		In = FVector(-1.f, 0.f, 0.f);
	}
	return In * T.ShoveSpeed + FVector(0.f, 0.f, T.ShoveUpSpeed);
}

float FGFGiantRules::ChaseSpeed(const FGFGiantTuning& T, float RunSpeed, float TimeOnTarget)
{
	const float Run = RunSpeed > 0.f ? RunSpeed : T.PlayerRunSpeed;
	const float Alpha = T.SlowdownSeconds > 0.f ? FMath::Clamp(TimeOnTarget / T.SlowdownSeconds, 0.f, 1.f) : 1.f;
	return Run * FMath::Lerp(T.StartSpeedMul, T.EndSpeedMul, Alpha);
}

FString FGFGiantRules::FillTarget(const FString& Line, const FString& Name)
{
	if (!Name.IsEmpty())
	{
		return Line.Replace(TargetToken, *Name, ESearchCase::IgnoreCase);
	}
	// Nobody to name (no target, or a nameless player): drop the token with its lead-in so the line still reads
	// ("Where you going, {Target}? Huh?" -> "Where you going? Huh?", "Hey {Target}!" -> "Hey!").
	FString Out = Line;
	const FString Tok(TargetToken);
	Out.ReplaceInline(*(TEXT(", ") + Tok), TEXT(""), ESearchCase::IgnoreCase);
	Out.ReplaceInline(*(TEXT(" ") + Tok + TEXT(".")), TEXT(""), ESearchCase::IgnoreCase);
	Out.ReplaceInline(*(TEXT(" ") + Tok), TEXT(""), ESearchCase::IgnoreCase);
	Out.ReplaceInline(*Tok, TEXT("you"), ESearchCase::IgnoreCase);
	return Out;
}

FVector FGFGiantRules::RespawnPoint(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& From, const TArray<FBox>& NoEntry)
{
	FVector EdgeP, Outward;
	if (!Zone.ClosestEdge(From, EdgeP, Outward))
	{
		return From;
	}
	FVector Out = EdgeP - Outward * T.RespawnInset;
	Out.Z = From.Z;
	auto InBox = [&NoEntry](const FVector& P)
	{
		return NoEntry.ContainsByPredicate([&P](const FBox& B)
		{
			return B.IsValid && P.X >= B.Min.X && P.X <= B.Max.X && P.Y >= B.Min.Y && P.Y <= B.Max.Y;
		});
	};
	auto Fits = [&](const FVector& P) { return Zone.IsInsideSafe(P) && !Zone.IsHazardAt(P) && !InBox(P); };
	if (Fits(Out))
	{
		return Out;
	}
	const FVector In = -Outward.GetSafeNormal2D();
	const float Step = FMath::Max(T.RespawnInset, 50.f);
	// Further in along the same line...
	for (int32 k = 2; k <= 8; ++k)
	{
		FVector P = EdgeP + In * (Step * k);
		P.Z = From.Z;
		if (Fits(P))
		{
			return P;
		}
	}
	// ...else the nearest fitting spot around the border point (rings, starting from straight in, either side).
	if (!In.IsNearlyZero())
	{
		for (int32 Ring = 1; Ring <= 24; ++Ring)
		{
			const int32 Count = 8 * Ring;
			for (int32 i = 0; i <= Count / 2; ++i)
			{
				for (const float Side : { 1.f, -1.f })
				{
					FVector P = EdgeP + In.RotateAngleAxis(Side * 360.f * i / Count, FVector::UpVector) * (Step * Ring);
					P.Z = From.Z;
					if (Fits(P))
					{
						return P;
					}
				}
			}
		}
	}
	return Out;
}

bool FGFGiantRules::PickLine(const FGFGiantTuning& T, const FGFGiantLines& Lines, EGFGiantLinePhase Phase, const TArray<FName>& Recent, FRandomStream& Rng, FName& OutId, FString& OutText)
{
	const TArray<FGFGiantLines::FLine>* Pool = Lines.ByPhase.Find(Phase);
	if (!Pool || Pool->Num() == 0)
	{
		return false;
	}
	const int32 Window = FMath::Max(0, T.NoRepeatCount);
	auto RecentlySaid = [&](FName Id, int32 N)
	{
		for (int32 k = Recent.Num() - 1, Seen = 0; k >= 0 && Seen < N; --k, ++Seen)
		{
			if (Recent[k] == Id)
			{
				return true;
			}
		}
		return false;
	};
	TArray<int32> Candidates;
	for (int32 Shrink = Window; Shrink >= 0 && Candidates.Num() == 0; --Shrink)
	{
		for (int32 i = 0; i < Pool->Num(); ++i)
		{
			if (!RecentlySaid((*Pool)[i].Id, Shrink))
			{
				Candidates.Add(i);
			}
		}
	}
	const FGFGiantLines::FLine& L = (*Pool)[Candidates[Rng.RandRange(0, Candidates.Num() - 1)]];
	OutId = L.Id;
	OutText = L.Text;
	return true;
}

// ---------------- Sim ----------------

const FGFGiantPlayerView* FGFGiantSim::Find(const TArray<FGFGiantPlayerView>& Players, const FString& Key) const
{
	return Players.FindByPredicate([&](const FGFGiantPlayerView& P) { return P.Key == Key; });
}

bool FGFGiantSim::Say(EGFGiantLinePhase Phase, const FString& TargetName, const FString& AimedAt, FGFGiantStepOut& Out, const FString& DraggerName)
{
	FName Id;
	FString Text;
	if (!FGFGiantRules::PickLine(T, Lines, Phase, RecentLines, Rng, Id, Text))
	{
		return false;
	}
	FGFGiantChatLine C;
	C.Seq = NextSeq++;
	C.LineId = Id;
	C.Phase = Phase;
	C.Text = FGFLineTokens::FillDragger(FGFGiantRules::FillTarget(Text, TargetName), DraggerName);
	C.Speaker = Speaker;
	C.Channel = Channel;
	C.TargetKey = AimedAt;
	C.At = Elapsed;
	Out.Chat.Add(C);
	RecentLines.Add(Id);
	const int32 Keep = FMath::Max(8, T.NoRepeatCount);
	if (RecentLines.Num() > Keep)
	{
		RecentLines.RemoveAt(0, RecentLines.Num() - Keep);
	}
	SinceLastLine = 0.f;
	return true;
}

bool FGFGiantSim::Thank(const FGFGiantPlayerView& Target, FGFGiantStepOut& Out)
{
	if (Thanked.Contains(Target.Key))
	{
		return false;
	}
	if (!Say(EGFGiantLinePhase::Handoff, Target.Name, Target.Key, Out, FGFGiantRules::SetupName(Target)))
	{
		return false;
	}
	Thanked.Add(Target.Key);
	return true;
}

void FGFGiantSim::NoticeTaunt(const FVector& At, float Radius)
{
	if (State != EGFGiantState::Chase && State != EGFGiantState::Circling)
	{
		return;
	}
	bTaunt = true;
	TauntAt = At;
	TauntRadius = Radius > 0.f ? Radius : T.TauntNoticeRadius;
	TauntLeft = T.TauntInterestSeconds;
}

void FGFGiantSim::ChaseChat(const FGFGiantPlayerView* Target, FGFGiantStepOut& Out)
{
	const bool bTarget = Target && Lines.Num(EGFGiantLinePhase::Target) > 0 && Rng.FRand() < T.TargetLineWeight;
	if (bTarget)
	{
		Say(EGFGiantLinePhase::Target, Target->Name, Target->Key, Out, Target->Dragger);
	}
	else
	{
		Say(EGFGiantLinePhase::Chase, Target ? Target->Name : FString(), FString(), Out, Target ? Target->Dragger : FString());
	}
}

void FGFGiantSim::Begin(const FGFGiantTuning& InT, const FGFGiantLines& InLines, const IGFSafeZoneQuery& InZone, const TArray<FGFGiantPlayerView>& Players, bool bInReturnVisit, int32 Seed, FGFGiantStepOut& Out)
{
	T = InT;
	Lines = InLines;
	Rng.Initialize(Seed);
	Zone = &InZone;
	bReturnVisit = bInReturnVisit;
	Elapsed = 0.f;
	State = EGFGiantState::Chase;
	EatenKey.Reset();
	RecentLines.Reset();
	ShoveCooldowns.Reset();
	LastPositions.Reset();
	IdleSeconds = 0.f;
	bIdleWarned = false;
	TimeOnTarget = 0.f;
	bWalkingOut = false;
	bTaunt = false;
	TauntLeft = 0.f;
	TauntAt = FVector::ZeroVector;
	TauntRadius = 0.f;
	Thanked.Reset();
	BlockedKey.Reset();
	BlockedSeconds = 0.f;
	Passed.Reset();
	WasExposed.Reset();
	bHerdedSomeone = false;
	for (const FGFGiantPlayerView& P : Players)
	{
		if (FGFGiantRules::IsExposed(*Zone, NoEntry, P))
		{
			WasExposed.Add(P.Key);
		}
	}
	bHasWalkOutGoal = false;
	bHasCircleGoal = false;

	const FGFGiantPick Pick = FGFGiantRules::PickInterest(T, *Zone, Players, NoEntry, nullptr);
	const int32 Ti = Pick.Index;
	// Arrive near the target, else near the first player who is not out of reach (never next to someone in a cell).
	const FGFGiantPlayerView* Anchor = Ti != INDEX_NONE ? &Players[Ti]
		: Players.FindByPredicate([&](const FGFGiantPlayerView& P) { return !FGFNoEntry::IsInside(NoEntry, P.Location); });
	if (!Anchor && Players.Num() > 0)
	{
		Anchor = &Players[0];
	}
	const FVector From = Anchor ? Anchor->Location : FVector::ZeroVector;
	FVector Away = -FGFSafeZone::DirToSafe(*Zone, From);
	if (Away.IsNearlyZero())
	{
		Away = FVector(1.f, 0.f, 0.f);
	}
	if (Ti != INDEX_NONE)
	{
		TargetKey = Players[Ti].Key;
	}
	else
	{
		TargetKey.Reset();
	}
	Location = FGFNoEntry::KeepOut(NoEntry, FGFGiantRules::ClampOutside(T, *Zone, From + Away * T.SpawnDistance), T.GiantRadius, T.BodyHalfHeight);
	for (const FGFGiantPlayerView& P : Players)
	{
		LastPositions.Add(P.Key, P.Location);
	}
	const FGFGiantPlayerView* Target = Ti != INDEX_NONE ? &Players[Ti] : nullptr;
	Say(bReturnVisit ? EGFGiantLinePhase::Return : EGFGiantLinePhase::Spotted, Target ? Target->Name : FString(), FString(), Out, Target ? Target->Dragger : FString());
	if (Target && Pick.Reason == EGFGiantInterest::Handoff)
	{
		Thank(*Target, Out);
	}
	ChatIn = Rng.FRandRange(T.ChaseChatMinSeconds, T.ChaseChatMaxSeconds);
}

void FGFGiantSim::StartLeaving(FGFGiantStepOut& Out)
{
	Say(EGFGiantLinePhase::Leaving, FString(), FString(), Out);
	State = EGFGiantState::Leaving;
	LeaveLeft = T.LeaveWalkSeconds;
	TargetKey.Reset();
	if (LeaveLeft <= 0.f)
	{
		State = EGFGiantState::Gone;
		Out.bEnded = true;
	}
}

bool FGFGiantSim::Eat(const FString& Key, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out)
{
	if (!EatenKey.IsEmpty() || !IsActive() || Key.IsEmpty())
	{
		return false;
	}
	const FGFGiantPlayerView* P = Find(Players, Key);
	EatenKey = Key;
	Say(EGFGiantLinePhase::Snacked, P ? P->Name : Key, Key, Out, P ? P->Dragger : FString());
	State = EGFGiantState::Gone;
	TargetKey.Reset();
	Out.Eaten = Key;
	Out.bEnded = true;
	return true;
}

bool FGFGiantSim::Attacked(const FString& Key, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out)
{
	// It never attacks a protected player (or one out of reach), even one who hits it.
	const FGFGiantPlayerView* P = Find(Players, Key);
	if (!Zone || !P || !FGFGiantRules::CanTouch(*Zone, NoEntry, *P))
	{
		return false;
	}
	return Eat(Key, Players, Out);
}

void FGFGiantSim::NoteHomecomings(const TArray<FGFGiantPlayerView>& Players)
{
	if (!Zone)
	{
		return;
	}
	TSet<FString> Now;
	for (const FGFGiantPlayerView& P : Players)
	{
		if (FGFGiantRules::IsExposed(*Zone, NoEntry, P))
		{
			Now.Add(P.Key);
		}
		else if (WasExposed.Contains(P.Key) && Zone->IsSafeFor(P.Subject(), P.Location))
		{
			bHerdedSomeone = true;
		}
	}
	WasExposed = MoveTemp(Now);
}

void FGFGiantSim::MoveToward(const FVector& Goal, float StepLen, bool bMayEnterSafe)
{
	const IGFSafeZoneQuery& Safe = *Zone;
	const FVector Start = Location;
	auto Allowed = [&](const FVector& P)
	{
		return (bMayEnterSafe || FGFSafeZone::IsClear(Safe, P, T.GiantRadius))
			&& FGFNoEntry::KeepOut(NoEntry, P, T.GiantRadius, T.BodyHalfHeight).Equals(P, 0.01f);
	};
	const bool bStartValid = Allowed(Start);
	auto Walk = [&](const FVector& To)
	{
		FVector Want = To - Location;
		Want.Z = 0.f;
		const float Len = FMath::Min(Want.Size(), StepLen);
		if (Len > KINDA_SMALL_NUMBER)
		{
			Location += Want.GetSafeNormal() * Len;
		}
	};
	if (bMayEnterSafe)
	{
		Walk(Goal);
		bWalkingOut = !FGFSafeZone::IsClear(Safe, Location, T.GiantRadius);
		bHasWalkOutGoal = false;
	}
	else if (bWalkingOut && !FGFSafeZone::IsClear(Safe, Location, T.GiantRadius))
	{
		// It chased someone in: walk out the shortest way rather than jump. The way out is found once (KeepOut can
		// be costly in narrow gaps) and kept while it stays clear.
		if (!bHasWalkOutGoal || !FGFSafeZone::IsClear(Safe, WalkOutGoal, T.GiantRadius))
		{
			WalkOutGoal = FGFSafeZone::KeepOut(Safe, Location, T.GiantRadius);
			bHasWalkOutGoal = true;
		}
		Walk(WalkOutGoal);
		bWalkingOut = !FGFSafeZone::IsClear(Safe, Location, T.GiantRadius);
	}
	else
	{
		// Skirting: slides along borders, at most one step; pushed out at once if territory grows under it.
		bWalkingOut = false;
		bHasWalkOutGoal = false;
		Location = FGFSafeZone::StepOutside(Safe, Location, Goal, StepLen, T.GiantRadius);
	}
	Location = FGFNoEntry::KeepOut(NoEntry, Location, T.GiantRadius, T.BodyHalfHeight);
	// Never more than one step from a valid spot (box pushes and slides can add up): the capped point, else stay.
	if (bStartValid && FVector::Dist2D(Location, Start) > StepLen + 1.f)
	{
		FVector Delta = Location - Start;
		Delta.Z = 0.f;
		const FVector Capped = Start + Delta.GetSafeNormal() * StepLen;
		Location = Allowed(Capped) ? Capped : Start;
		bWalkingOut = bMayEnterSafe && !FGFSafeZone::IsClear(Safe, Location, T.GiantRadius);
	}
}

void FGFGiantSim::Step(float Dt, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out)
{
	if (!IsActive() || Dt <= 0.f || !Zone)
	{
		return;
	}
	const IGFSafeZoneQuery& Safe = *Zone;
	Elapsed += Dt;
	SinceLastLine += Dt;
	NoteHomecomings(Players);
	{
		TArray<FString> Free;
		for (const FString& Key : Passed)
		{
			const FGFGiantPlayerView* Held = Find(Players, Key);
			if (!Held || !FGFNoEntry::IsInside(NoEntry, Held->Location))
			{
				Free.Add(Key);
			}
		}
		for (const FString& Key : Free)
		{
			Passed.Remove(Key);
		}
	}
	if (bTaunt)
	{
		TauntLeft -= Dt;
		if (TauntLeft <= 0.f)
		{
			bTaunt = false;
		}
	}
	for (TPair<FString, float>& C : ShoveCooldowns)
	{
		C.Value -= Dt;
	}

	// Horizontal speed of everyone this step (from their positions; teleports and launches both count as moving).
	TMap<FString, float> Speeds;
	for (const FGFGiantPlayerView& P : Players)
	{
		const FVector* Last = LastPositions.Find(P.Key);
		Speeds.Add(P.Key, Last ? FVector::Dist2D(*Last, P.Location) / Dt : TNumericLimits<float>::Max());
		LastPositions.Add(P.Key, P.Location);
	}

	if (State == EGFGiantState::Leaving)
	{
		LeaveLeft -= Dt;
		FVector Away = -FGFSafeZone::DirToSafe(Safe, Location);
		const float Len = T.CircleWalkSpeed * 2.f * Dt;
		MoveToward(Location + (Away.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Away) * Len, Len, false);
		if (LeaveLeft <= 0.f)
		{
			State = EGFGiantState::Gone;
			Out.bEnded = true;
		}
		return;
	}
	if (Elapsed >= T.EventSeconds)
	{
		StartLeaving(Out);
		return;
	}

	FGFGiantNotice Notice;
	if (bTaunt)
	{
		Notice.bActive = true;
		Notice.At = TauntAt;
		Notice.Radius = TauntRadius;
	}
	const FGFGiantPick Pick = FGFGiantRules::PickInterest(T, Safe, Players, NoEntry, bTaunt ? &Notice : nullptr, &Passed);
	const int32 Ti = Pick.Index;
	ChatIn -= Dt;
	if (Pick.Reason == EGFGiantInterest::Look)
	{
		// The taunt pulled him in, and nobody exposed is standing there. Go look. Don't call that "herded".
		if (State == EGFGiantState::Circling)
		{
			State = EGFGiantState::Chase;
			ChatIn = FMath::Min(ChatIn, T.ChaseChatMinSeconds);
		}
		if (!TargetKey.IsEmpty())
		{
			TargetKey.Reset();
			TimeOnTarget = 0.f;
			IdleSeconds = 0.f;
			bIdleWarned = false;
		}
		const float Speed = FGFGiantRules::ChaseSpeed(T, T.PlayerRunSpeed, 0.f);
		LastSpeed = Speed;
		MoveToward(TauntAt, Speed * Dt, false);
		return;
	}
	if (Ti == INDEX_NONE)
	{
		// Everyone he can chase is home, or he gave up on a box and nobody else is out.
		if (State == EGFGiantState::Chase)
		{
			if (bHerdedSomeone)
			{
				Say(EGFGiantLinePhase::Herded, FString(), FString(), Out);
				bHerdedSomeone = false;
			}
			State = EGFGiantState::Circling;
			ChatIn = Rng.FRandRange(T.CirclingChatMinSeconds, T.CirclingChatMaxSeconds);
			TargetKey.Reset();
			IdleSeconds = 0.f;
			bIdleWarned = false;
			bHasCircleGoal = false;
		}
		// Walk the border near the closest player (whichever region they are in), pacing back and forth.
		const FGFGiantPlayerView* Near = nullptr;
		for (const FGFGiantPlayerView& P : Players)
		{
			if (!Near || FVector::DistSquared2D(P.Location, Location) < FVector::DistSquared2D(Near->Location, Location))
			{
				Near = &P;
			}
		}
		FVector EdgeP, Outward;
		if (Near && Safe.ClosestEdge(Near->Location, EdgeP, Outward))
		{
			const FVector Tangent(-Outward.Y, Outward.X, 0.f);
			const FVector Wanted = EdgeP + Outward * T.GiantRadius + Tangent * (T.EdgePaceDistance * FMath::Sin(Elapsed * 0.6f));
			// Cheap push each step; where it fails (next to a narrow gap) keep the last clear goal, and run the costly
			// search only when there is none (once on arrival, or after safety grew over it).
			FVector Pushed;
			if (FGFSafeZone::PushOut(Safe, Wanted, T.GiantRadius, Pushed))
			{
				CircleGoal = Pushed;
				bHasCircleGoal = true;
			}
			else if (!bHasCircleGoal || !FGFSafeZone::IsClear(Safe, CircleGoal, T.GiantRadius))
			{
				CircleGoal = FGFGiantRules::ClampOutside(T, Safe, Wanted);
				bHasCircleGoal = true;
			}
			MoveToward(CircleGoal, T.CircleWalkSpeed * Dt, false);
		}
		else
		{
			MoveToward(Location, 0.f, false);
		}
		if (ChatIn <= 0.f)
		{
			Say(EGFGiantLinePhase::Circling, FString(), FString(), Out);
			ChatIn = Rng.FRandRange(T.CirclingChatMinSeconds, T.CirclingChatMaxSeconds);
		}
		return;
	}

	const FGFGiantPlayerView& Target = Players[Ti];
	if (State == EGFGiantState::Circling)
	{
		State = EGFGiantState::Chase;
		ChatIn = FMath::Min(ChatIn, T.ChaseChatMinSeconds);
	}
	if (Target.Key != TargetKey)
	{
		TargetKey = Target.Key;
		TimeOnTarget = 0.f;
		IdleSeconds = 0.f;
		bIdleWarned = false;
	}
	TimeOnTarget += Dt;

	// Herd: get behind the target (away from safety) and push it toward safety. Slides along borders while the target
	// is outside; walks into safe territory only after an exposed target standing there (unprotected on that land).
	// A target inside a no-entry box is met at the edge. He waits, then leaves them for someone else.
	const float Speed = FGFGiantRules::ChaseSpeed(T, Target.RunSpeed, TimeOnTarget);
	LastSpeed = Speed;
	const bool bBoxed = FGFNoEntry::IsInside(NoEntry, Target.Location);
	if (bBoxed)
	{
		if (BlockedKey != Target.Key)
		{
			BlockedKey = Target.Key;
			BlockedSeconds = 0.f;
		}
		const FVector Goal = FGFGiantRules::NoEntryApproach(T, NoEntry, Location, Target.Location);
		MoveToward(Goal, Speed * Dt, Safe.IsInsideSafe(Target.Location));
		if (FVector::Dist2D(Location, Goal) <= 50.f)
		{
			BlockedSeconds += Dt;
			if (BlockedSeconds >= T.JailDwellSeconds)
			{
				Passed.Add(Target.Key);
			}
		}
	}
	else
	{
		if (BlockedKey == Target.Key)
		{
			BlockedKey.Reset();
			BlockedSeconds = 0.f;
		}
		const FVector Goal = FGFGiantRules::HerdGoal(T, Safe, Target.Location);
		MoveToward(Goal, Speed * Dt, Safe.IsInsideSafe(Target.Location));
	}

	if (Pick.Reason == EGFGiantInterest::Handoff && Thank(Target, Out))
	{
		ChatIn = Rng.FRandRange(T.ChaseChatMinSeconds, T.ChaseChatMaxSeconds);
	}

	// Shoves (the only contact): any exposed player close enough is launched toward the nearest safety.
	bool bShoved = false;
	for (const FGFGiantPlayerView& P : Players)
	{
		if (!FGFGiantRules::CanTouch(Safe, NoEntry, P) || FVector::Dist2D(P.Location, Location) > T.ShoveRange)
		{
			continue;
		}
		float& Cd = ShoveCooldowns.FindOrAdd(P.Key);
		if (Cd > 0.f)
		{
			continue;
		}
		Cd = T.ShoveCooldown;
		Out.Shoves.Add({ P.Key, FGFGiantRules::ShoveVelocity(T, Safe, P.Location) });
		bShoved = true;
	}
	if (bShoved && SinceLastLine >= T.CloseCallMinGap)
	{
		Say(EGFGiantLinePhase::CloseCall, Target.Name, Target.Key, Out, Target.Dragger);
		ChatIn = Rng.FRandRange(T.ChaseChatMinSeconds, T.ChaseChatMaxSeconds);
	}

	// The (exposed) target standing still near it: a warning taunt, then a snack.
	const float* TargetSpeed = Speeds.Find(Target.Key);
	const bool bIdle = T.bEatIdleTarget && !Target.bBusy && FGFGiantRules::CanTouch(Safe, NoEntry, Target) && TargetSpeed && *TargetSpeed < T.IdleSpeedThreshold
		&& FVector::Dist2D(Location, Target.Location) <= T.IdleRange;
	if (bIdle)
	{
		IdleSeconds += Dt;
		if (IdleSeconds >= T.IdleEatSeconds)
		{
			Eat(Target.Key, Players, Out);
			return;
		}
		if (!bIdleWarned && IdleSeconds >= T.IdleWarnSeconds)
		{
			bIdleWarned = true;
			Say(EGFGiantLinePhase::Target, Target.Name, Target.Key, Out, Target.Dragger);
			ChatIn = Rng.FRandRange(T.ChaseChatMinSeconds, T.ChaseChatMaxSeconds);
		}
	}
	else
	{
		IdleSeconds = 0.f;
		bIdleWarned = false;
	}

	if (ChatIn <= 0.f)
	{
		ChaseChat(&Target, Out);
		ChatIn = Rng.FRandRange(T.ChaseChatMinSeconds, T.ChaseChatMaxSeconds);
	}
}

// ---------------- Land zone ----------------

bool FGFGiantLandZone::IsSafeFor(const FGFSafeSubject& Who, const FVector& L) const
{
	// Owned land answers through its owner (protection, guests, visitors); unowned land through the giant's own zone.
	if (const UGFSafeZoneSubsystem* Sub = Lands.Get())
	{
		if (!Sub->OwnerAt(L).IsNone())
		{
			return Sub->IsSafeFor(Who, L);
		}
	}
	return Base && Base->HasAnySafe() && Base->IsSafeFor(Who, L);
}

FName FGFGiantLandZone::OwnerAt(const FVector& L) const
{
	if (const UGFSafeZoneSubsystem* Sub = Lands.Get())
	{
		const FName Owner = Sub->OwnerAt(L);
		if (!Owner.IsNone())
		{
			return Owner;
		}
	}
	return Base ? Base->OwnerAt(L) : NAME_None;
}
