#include "GFGiantTypes.h"
#include "GFDataTables.h"
#include "GFLineTokens.h"

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

bool FGFGiantRules::AnyoneOutside(const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players)
{
	return Players.ContainsByPredicate([&](const FGFGiantPlayerView& P) { return !Zone.IsInsideSafe(P.Location); });
}

FVector FGFGiantRules::ClampOutside(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& P)
{
	return FGFSafeZone::KeepOut(Zone, P, T.GiantRadius);
}

int32 FGFGiantRules::PickTarget(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players)
{
	int32 Best = INDEX_NONE;
	float BestOut = 0.f;
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		const FGFGiantPlayerView& P = Players[i];
		if (Zone.IsInsideSafe(P.Location))
		{
			continue;
		}
		const float POut = FGFSafeZone::DistanceOutside(Zone, P.Location);
		if (Best == INDEX_NONE)
		{
			Best = i;
			BestOut = POut;
			continue;
		}
		const FGFGiantPlayerView& B = Players[Best];
		bool bBetter;
		if (FMath::Abs(P.PlaySeconds - B.PlaySeconds) > T.NewestTieSeconds)
		{
			bBetter = P.PlaySeconds < B.PlaySeconds;
		}
		else if (P.FirstSeenTicks != B.FirstSeenTicks)
		{
			bBetter = P.FirstSeenTicks > B.FirstSeenTicks;
		}
		else
		{
			bBetter = POut > BestOut;
		}
		if (bBetter)
		{
			Best = i;
			BestOut = POut;
		}
	}
	return Best;
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

FVector FGFGiantRules::RespawnPoint(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& From)
{
	FVector EdgeP, Outward;
	if (!Zone.ClosestEdge(From, EdgeP, Outward))
	{
		return From;
	}
	FVector Out = EdgeP - Outward * T.RespawnInset;
	Out.Z = From.Z;
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

void FGFGiantSim::Say(EGFGiantLinePhase Phase, const FString& TargetName, const FString& AimedAt, FGFGiantStepOut& Out, const FString& DraggerName)
{
	FName Id;
	FString Text;
	if (!FGFGiantRules::PickLine(T, Lines, Phase, RecentLines, Rng, Id, Text))
	{
		return;
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

	const int32 Ti = FGFGiantRules::PickTarget(T, *Zone, Players);
	const FVector From = Ti != INDEX_NONE ? Players[Ti].Location : (Players.Num() > 0 ? Players[0].Location : FVector::ZeroVector);
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
	Location = FGFGiantRules::ClampOutside(T, *Zone, From + Away * T.SpawnDistance);
	for (const FGFGiantPlayerView& P : Players)
	{
		LastPositions.Add(P.Key, P.Location);
	}
	const FGFGiantPlayerView* Target = Ti != INDEX_NONE ? &Players[Ti] : nullptr;
	Say(bReturnVisit ? EGFGiantLinePhase::Return : EGFGiantLinePhase::Spotted, Target ? Target->Name : FString(), FString(), Out, Target ? Target->Dragger : FString());
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
	return Eat(Key, Players, Out);
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
		Location += (Away.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Away) * T.CircleWalkSpeed * 2.f * Dt;
		Location = FGFGiantRules::ClampOutside(T, Safe, Location);
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

	const int32 Ti = FGFGiantRules::PickTarget(T, Safe, Players);
	ChatIn -= Dt;
	if (Ti == INDEX_NONE)
	{
		// Everyone is home: grumble along the edge.
		if (State == EGFGiantState::Chase)
		{
			Say(EGFGiantLinePhase::Herded, FString(), FString(), Out);
			State = EGFGiantState::Circling;
			ChatIn = Rng.FRandRange(T.CirclingChatMinSeconds, T.CirclingChatMaxSeconds);
			TargetKey.Reset();
			IdleSeconds = 0.f;
			bIdleWarned = false;
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
			FVector Goal = EdgeP + Outward * T.GiantRadius + Tangent * (T.EdgePaceDistance * FMath::Sin(Elapsed * 0.6f));
			Goal = FGFGiantRules::ClampOutside(T, Safe, Goal);
			Location = FGFSafeZone::StepOutside(Safe, Location, Goal, T.CircleWalkSpeed * Dt, T.GiantRadius);
		}
		else
		{
			Location = FGFGiantRules::ClampOutside(T, Safe, Location);
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

	// Herd: get behind the target (away from safety) and push it home. Slides along borders, never into safety.
	const float Speed = FGFGiantRules::ChaseSpeed(T, Target.RunSpeed, TimeOnTarget);
	LastSpeed = Speed;
	const FVector Goal = FGFGiantRules::HerdGoal(T, Safe, Target.Location);
	Location = FGFSafeZone::StepOutside(Safe, Location, Goal, Speed * Dt, T.GiantRadius);

	// Shoves (the only contact): anyone outside safety and close enough is launched toward the nearest safety.
	bool bShoved = false;
	for (const FGFGiantPlayerView& P : Players)
	{
		if (Safe.IsInsideSafe(P.Location) || FVector::Dist2D(P.Location, Location) > T.ShoveRange)
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

	// Standing still outside, near it: a warning taunt, then a snack.
	const float* TargetSpeed = Speeds.Find(Target.Key);
	const bool bIdle = T.bEatIdleTarget && !Target.bBusy && TargetSpeed && *TargetSpeed < T.IdleSpeedThreshold
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
