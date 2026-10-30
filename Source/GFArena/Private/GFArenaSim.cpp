#include "GFArenaTypes.h"
#include "GFDataTables.h"

// ---------------------------------------------------------------- data

const FGFArenaData& FGFArenaData::Get()
{
	static FGFArenaData Data;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		if (const UDataTable* T = FGFDataTables::Get<FGFArenaSpeciesRow>(TEXT("DT_ArenaSpecies")))
		{
			T->ForeachRow<FGFArenaSpeciesRow>(TEXT("Arena"), [&](const FName& Name, const FGFArenaSpeciesRow& Row) { Data.Species.Add(Name, Row); });
		}
		if (const UDataTable* T = FGFDataTables::Get<FGFDinoAbilityRow>(TEXT("DT_DinoAbilities")))
		{
			T->ForeachRow<FGFDinoAbilityRow>(TEXT("Arena"), [&](const FName& Name, const FGFDinoAbilityRow& Row)
			{
				Data.Abilities.FindOrAdd(Row.Species).Add(Row);
				Data.AbilityIds.Add(FName(*FString::Printf(TEXT("%s.%d"), *Row.Species.ToString(), Row.Slot)), Name);
			});
			for (TPair<FName, TArray<FGFDinoAbilityRow>>& Pair : Data.Abilities)
			{
				Pair.Value.Sort([](const FGFDinoAbilityRow& A, const FGFDinoAbilityRow& B) { return A.Slot < B.Slot; });
			}
		}
		if (const UDataTable* T = FGFDataTables::Get<FGFArenaRewardRow>(TEXT("DT_ArenaRewards")))
		{
			T->ForeachRow<FGFArenaRewardRow>(TEXT("Arena"), [&](const FName&, const FGFArenaRewardRow& Row) { Data.Rewards.Add(Row); });
		}
		if (const UDataTable* T = FGFDataTables::Get<FGFWagerBandRow>(TEXT("DT_WagerBands")))
		{
			T->ForeachRow<FGFWagerBandRow>(TEXT("Arena"), [&](const FName&, const FGFWagerBandRow& Row) { Data.Bands.Add(Row); });
		}
	}
	return Data;
}

int32 FGFArenaData::AbilityCountFor(FName InSpecies, int32 Level) const
{
	const TArray<FGFDinoAbilityRow>* Rows = Abilities.Find(InSpecies);
	if (!Rows)
	{
		return 0;
	}
	int32 N = 0;
	for (const FGFDinoAbilityRow& R : *Rows)
	{
		N += Level >= R.UnlockLevel ? 1 : 0;
	}
	return N;
}

const FGFDinoAbilityRow* FGFArenaData::Ability(FName InSpecies, int32 SlotIndex) const
{
	const TArray<FGFDinoAbilityRow>* Rows = Abilities.Find(InSpecies);
	return Rows && Rows->IsValidIndex(SlotIndex) ? &(*Rows)[SlotIndex] : nullptr;
}

const FGFArenaRewardRow* FGFArenaData::RewardFor(int32 Stars) const
{
	for (const FGFArenaRewardRow& R : Rewards)
	{
		if (R.Stars == Stars)
		{
			return &R;
		}
	}
	return nullptr;
}

const FGFWagerBandRow* FGFArenaData::BandRow(int32 Band) const
{
	const int32 Want = Band <= 0 ? 2 : Band;
	for (const FGFWagerBandRow& R : Bands)
	{
		if (R.Band == Want)
		{
			return &R;
		}
	}
	return nullptr;
}

TArray<FName> FGFArenaData::SpeciesIds() const
{
	TArray<FName> Out;
	Species.GetKeys(Out);
	Out.Sort(FNameLexicalLess());
	return Out;
}

// ---------------------------------------------------------------- math

float FGFArenaMath::Damage(float Attack, float Mult, bool bWellFed, bool bTargetShred, bool bTargetGuard, float ComboMult)
{
	float D = Attack * Mult * ComboMult;
	D *= bWellFed ? 1.15f : 1.f;
	D *= bTargetShred ? 1.2f : 1.f;
	D *= bTargetGuard ? 0.7f : 1.f;
	return FMath::Max(0.f, D);
}

int32 FGFArenaUnit::StatusMask() const
{
	int32 Mask = 0;
	for (int32 i = 1; i < Status.Num(); ++i)
	{
		Mask |= Status[i] > 0.f ? (1 << i) : 0;
	}
	return Mask;
}

// ---------------------------------------------------------------- slow time

bool FGFSlowTime::TryActivate(double NowReal)
{
	if (!IsReady(NowReal))
	{
		return false;
	}
	bActive = true;
	EndsAt = NowReal + WindowSeconds;
	return true;
}

bool FGFSlowTime::Update(double NowReal)
{
	return bActive && NowReal >= EndsAt ? End(NowReal) : false;
}

bool FGFSlowTime::End(double NowReal)
{
	if (!bActive)
	{
		return false;
	}
	bActive = false;
	ReadyAt = NowReal + CooldownSeconds; // the cooldown starts when the window ends
	return true;
}

// ---------------------------------------------------------------- sim

void FGFArenaSim::Init(int32 InSeed, EGFArenaAI Team0AI, EGFArenaAI Team1AI)
{
	Units.Reset();
	Time = 0.f;
	Pending = 0.f;
	Seed = InSeed;
	Rng.Initialize(InSeed);
	Outcome = EGFArenaOutcome::None;
	Combos = 0;
	TeamAI[0] = Team0AI;
	TeamAI[1] = Team1AI;
}

FVector2D FGFArenaSim::SpotPosition(int32 Team, int32 Spot)
{
	const int32 Row = FMath::Clamp(Spot, 0, 5) / 3; // 0 front, 1 back
	const int32 Lane = FMath::Clamp(Spot, 0, 5) % 3;
	const float X = Row == 0 ? 400.f : 700.f;
	const float Y = (Lane - 1) * 350.f;
	return FVector2D(Team == 0 ? -X : X, Y);
}

int32 FGFArenaSim::AddUnit(const FGFArenaData& Data, int32 Team, const FGFArenaEntrant& Who)
{
	FGFArenaUnit U;
	U.Id = Units.Num() + 1;
	U.Team = Team;
	U.OwnerKey = Who.OwnerKey;
	U.CompanionId = Who.CompanionId;
	U.Species = Who.Species;
	U.Level = FMath::Clamp(Who.Level, 1, 20);
	U.Spot = FMath::Clamp(Who.Spot, 0, 5);
	U.bWellFed = Who.bWellFed;
	const FGFArenaSpeciesRow* Row = Data.Species.Find(Who.Species);
	const FGFArenaSpeciesRow Base = Row ? *Row : FGFArenaSpeciesRow();
	const float Scale = FGFArenaMath::LevelScale(U.Level) * (TeamAI[Team & 1] == EGFArenaAI::Champion && Team == 1 ? 1.15f : 1.f);
	U.Role = Base.Role;
	U.MaxHP = U.HP = Base.HP * Scale;
	U.Attack = Base.Attack * Scale;
	U.AttackInterval = Base.AttackInterval;
	U.Range = Base.Range;
	U.Speed = Base.Speed;
	U.Pos = SpotPosition(Team, U.Spot);
	U.AbilityCount = Data.AbilityCountFor(U.Species, U.Level);
	U.Cooldowns.Init(0.f, 3);
	for (int32 i = 0; i < 3; ++i)
	{
		const FGFDinoAbilityRow* A = Data.Ability(U.Species, i);
		// Abilities start part-way charged so openings differ a little; well fed = first one ready.
		U.Cooldowns[i] = A ? A->Cooldown * 0.5f : 0.f;
	}
	if (U.bWellFed)
	{
		U.Cooldowns[0] = 0.f;
	}
	U.Status.Init(0.f, static_cast<int32>(EGFArenaStatus::Count));
	U.AttackTimer = Rng.FRandRange(0.f, U.AttackInterval * 0.5f);
	Units.Add(U);
	return U.Id;
}

FGFArenaUnit* FGFArenaSim::Find(int32 Id)
{
	return Units.IsValidIndex(Id - 1) ? &Units[Id - 1] : nullptr;
}

const FGFArenaUnit* FGFArenaSim::Find(int32 Id) const
{
	return Units.IsValidIndex(Id - 1) ? &Units[Id - 1] : nullptr;
}

int32 FGFArenaSim::AliveCount(int32 Team) const
{
	int32 N = 0;
	for (const FGFArenaUnit& U : Units)
	{
		N += U.Team == Team && U.IsAlive() ? 1 : 0;
	}
	return N;
}

int32 FGFArenaSim::StarsFor(int32 Team) const
{
	const bool bWon = (Team == 0 && Outcome == EGFArenaOutcome::Team0Won) || (Team == 1 && Outcome == EGFArenaOutcome::Team1Won);
	if (!bWon)
	{
		return 0;
	}
	int32 Total = 0;
	for (const FGFArenaUnit& U : Units)
	{
		Total += U.Team == Team ? 1 : 0;
	}
	return 1 + (AliveCount(Team) == Total ? 1 : 0) + (Time < 90.f ? 1 : 0);
}

void FGFArenaSim::Advance(const FGFArenaData& Data, float Dt)
{
	Pending += FMath::Max(0.f, Dt);
	int32 Guard = 0;
	while (Pending >= StepSeconds && !IsOver() && Guard++ < 2000)
	{
		Pending -= StepSeconds;
		Step(Data, StepSeconds);
	}
}

EGFArenaOutcome FGFArenaSim::RunToEnd(const FGFArenaData& Data)
{
	int32 Guard = 0;
	while (!IsOver() && Guard++ < 100000)
	{
		Step(Data, StepSeconds);
	}
	return Outcome;
}

void FGFArenaSim::Step(const FGFArenaData& Data, float Dt)
{
	if (IsOver())
	{
		return;
	}
	Time += Dt;
	for (FGFArenaUnit& U : Units)
	{
		if (U.IsAlive())
		{
			TickUnit(Data, U, Dt);
		}
	}
	CheckEnd();
	if (!IsOver() && Time >= MaxSeconds)
	{
		Outcome = EGFArenaOutcome::Draw; // timeout
	}
}

void FGFArenaSim::CheckEnd()
{
	const int32 A = AliveCount(0);
	const int32 B = AliveCount(1);
	if (A == 0 && B == 0)
	{
		Outcome = EGFArenaOutcome::Draw;
	}
	else if (B == 0)
	{
		Outcome = EGFArenaOutcome::Team0Won;
	}
	else if (A == 0)
	{
		Outcome = EGFArenaOutcome::Team1Won;
	}
}

int32 FGFArenaSim::NearestEnemy(const FGFArenaUnit& U, float MaxDist) const
{
	int32 Best = INDEX_NONE;
	float BestD = MaxDist;
	for (const FGFArenaUnit& T : Units)
	{
		if (T.Team != U.Team && T.IsAlive())
		{
			const float D = FVector2D::Distance(U.Pos, T.Pos);
			if (D < BestD)
			{
				BestD = D;
				Best = T.Id;
			}
		}
	}
	return Best;
}

int32 FGFArenaSim::CountEnemiesWithin(const FGFArenaUnit& U, float Radius) const
{
	int32 N = 0;
	for (const FGFArenaUnit& T : Units)
	{
		N += T.Team != U.Team && T.IsAlive() && FVector2D::Distance(U.Pos, T.Pos) <= Radius ? 1 : 0;
	}
	return N;
}

int32 FGFArenaSim::SelectTarget(const FGFArenaUnit& U) const
{
	if (U.Has(EGFArenaStatus::Taunt))
	{
		const FGFArenaUnit* T = Find(U.TauntBy);
		if (T && T->IsAlive() && T->Team != U.Team)
		{
			return T->Id;
		}
	}
	switch (U.Role)
	{
	case EGFArenaRole::Tank:
	{
		// Protect the most hurt friend: go for whoever hit them last.
		const FGFArenaUnit* Weak = nullptr;
		for (const FGFArenaUnit& A : Units)
		{
			if (A.Team == U.Team && A.IsAlive() && A.Id != U.Id && (!Weak || A.HPFraction() < Weak->HPFraction()))
			{
				Weak = &A;
			}
		}
		const FGFArenaUnit* Threat = Weak ? Find(Weak->LastAttacker) : nullptr;
		if (Threat && Threat->IsAlive() && Weak->HPFraction() < 0.75f)
		{
			return Threat->Id;
		}
		return NearestEnemy(U);
	}
	case EGFArenaRole::Assassin:
	{
		int32 Best = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Max();
		for (const FGFArenaUnit& T : Units)
		{
			if (T.Team != U.Team && T.IsAlive())
			{
				const float Score = T.HPFraction() - (T.Spot >= 3 ? 0.2f : 0.f) + FVector2D::Distance(U.Pos, T.Pos) / 10000.f;
				if (Score < BestScore)
				{
					BestScore = Score;
					Best = T.Id;
				}
			}
		}
		return Best;
	}
	case EGFArenaRole::Skirmisher:
	{
		int32 Best = INDEX_NONE;
		float BestHP = TNumericLimits<float>::Max();
		for (const FGFArenaUnit& T : Units)
		{
			if (T.Team != U.Team && T.IsAlive() && FVector2D::Distance(U.Pos, T.Pos) <= 800.f && T.HP < BestHP)
			{
				BestHP = T.HP;
				Best = T.Id;
			}
		}
		return Best != INDEX_NONE ? Best : NearestEnemy(U);
	}
	default:
		return NearestEnemy(U);
	}
}

void FGFArenaSim::SetStatus(FGFArenaUnit& U, EGFArenaStatus S, float Seconds)
{
	const int32 i = static_cast<int32>(S);
	if (S != EGFArenaStatus::None && U.Status.IsValidIndex(i) && Seconds > 0.f)
	{
		U.Status[i] = FMath::Max(U.Status[i], Seconds);
	}
}

void FGFArenaSim::OnFainted(FGFArenaUnit& U)
{
	U.HP = 0.f;
	U.bFainted = true; // cozy: fainted, not dead
	for (float& S : U.Status)
	{
		S = 0.f;
	}
	U.CommandSlot = INDEX_NONE;
	if (!U.PossessedBy.IsEmpty())
	{
		const FString Key = U.PossessedBy;
		U.PossessedBy.Reset();
		// Her dino fainted under her control: hop to her next living dino (or back to the balcony).
		for (const FGFArenaUnit& Next : Units)
		{
			if (Next.OwnerKey == Key && Next.IsAlive() && Next.PossessedBy.IsEmpty())
			{
				Find(Next.Id)->PossessedBy = Key;
				break;
			}
		}
	}
}

void FGFArenaSim::Damage(FGFArenaUnit& From, FGFArenaUnit& To, float Amount)
{
	if (!To.IsAlive() || Amount <= 0.f)
	{
		return;
	}
	To.HP -= Amount;
	To.LastAttacker = From.Id;
	if (To.HP <= 0.f)
	{
		OnFainted(To);
	}
}

void FGFArenaSim::BasicAttack(FGFArenaUnit& U, FGFArenaUnit& T)
{
	Damage(U, T, FGFArenaMath::Damage(U.Attack, 1.f, U.bWellFed, T.Has(EGFArenaStatus::Shred), T.Has(EGFArenaStatus::Guard)));
	U.AttackTimer = U.AttackInterval;
}

bool FGFArenaSim::CanUseOn(const FGFDinoAbilityRow& Row, const FGFArenaUnit& U, const FGFArenaUnit* T) const
{
	switch (Row.Target)
	{
	case EGFAbilityTarget::Enemy:
		return T && T->IsAlive() && T->Team != U.Team && FVector2D::Distance(U.Pos, T->Pos) <= Row.Range + 30.f;
	case EGFAbilityTarget::EnemyArea:
		return CountEnemiesWithin(U, Row.Range) > 0;
	default:
		return true;
	}
}

bool FGFArenaSim::UseAbility(const FGFArenaData& Data, FGFArenaUnit& U, int32 SlotIndex, int32 TargetId)
{
	const FGFDinoAbilityRow* Row = Data.Ability(U.Species, SlotIndex);
	if (!Row || SlotIndex >= U.AbilityCount || U.Cooldowns[SlotIndex] > 0.f)
	{
		return false;
	}
	FGFArenaUnit* T = Find(TargetId);
	if (Row->Target == EGFAbilityTarget::Enemy && T && Row->bDash && T->IsAlive() && T->Team != U.Team && !U.Has(EGFArenaStatus::Root))
	{
		// Leap: lands next to the target if it is within the leap range.
		const float D = FVector2D::Distance(U.Pos, T->Pos);
		if (D <= Row->Range + 30.f && D > 1.f)
		{
			U.Pos = T->Pos + (U.Pos - T->Pos) / D * 120.f;
		}
	}
	if (!CanUseOn(*Row, U, T))
	{
		return false;
	}
	auto Hit = [&](FGFArenaUnit& Victim)
	{
		float Combo = 1.f;
		if ((Row->BonusVsStatus != EGFArenaStatus::None && Victim.Has(Row->BonusVsStatus)) ||
			(Row->BonusVsStatus2 != EGFArenaStatus::None && Victim.Has(Row->BonusVsStatus2)))
		{
			Combo = Row->BonusMult;
			++Combos;
		}
		if (Row->DamageMult > 0.f)
		{
			Damage(U, Victim, FGFArenaMath::Damage(U.Attack, Row->DamageMult, U.bWellFed, Victim.Has(EGFArenaStatus::Shred), Victim.Has(EGFArenaStatus::Guard), Combo));
		}
		if (Victim.IsAlive())
		{
			SetStatus(Victim, Row->Status, Row->StatusSeconds);
			if (Row->Status == EGFArenaStatus::Taunt)
			{
				Victim.TauntBy = U.Id;
			}
		}
	};
	switch (Row->Target)
	{
	case EGFAbilityTarget::Enemy:
		Hit(*T);
		break;
	case EGFAbilityTarget::EnemyArea:
		for (FGFArenaUnit& V : Units)
		{
			if (V.Team != U.Team && V.IsAlive() && FVector2D::Distance(U.Pos, V.Pos) <= Row->Range)
			{
				Hit(V);
			}
		}
		break;
	case EGFAbilityTarget::AllyArea:
		for (FGFArenaUnit& A : Units)
		{
			if (A.Team == U.Team && A.IsAlive() && FVector2D::Distance(U.Pos, A.Pos) <= Row->Range)
			{
				SetStatus(A, Row->Status, Row->StatusSeconds);
				A.HP = FMath::Min(A.MaxHP, A.HP + A.MaxHP * Row->HealFraction);
			}
		}
		break;
	case EGFAbilityTarget::Self:
		U.HP = FMath::Min(U.MaxHP, U.HP + U.MaxHP * Row->HealFraction);
		break;
	}
	SetStatus(U, Row->SelfStatus, Row->SelfStatusSeconds);
	U.Cooldowns[SlotIndex] = Row->Cooldown;
	U.AttackTimer = FMath::Max(U.AttackTimer, 0.4f); // short recovery
	++U.AbilitiesUsed;
	return true;
}

bool FGFArenaSim::TryAbilities(const FGFArenaData& Data, FGFArenaUnit& U, int32 TargetId)
{
	const EGFArenaAI Profile = TeamAI[U.Team & 1];
	if (U.DecisionTimer > 0.f)
	{
		return false;
	}
	U.DecisionTimer = Profile == EGFArenaAI::Easy ? 0.8f : (Profile == EGFArenaAI::Normal ? 0.3f : 0.1f);
	if (Profile == EGFArenaAI::Easy && Rng.FRand() < 0.5f)
	{
		return false; // Easy opponents hesitate
	}
	const FGFArenaUnit* T = Find(TargetId);
	const int32 EnemiesLeft = AliveCount(1 - U.Team);
	for (int32 i = U.AbilityCount - 1; i >= 0; --i)
	{
		const FGFDinoAbilityRow* Row = Data.Ability(U.Species, i);
		if (!Row || U.Cooldowns[i] > 0.f)
		{
			continue;
		}
		bool bWant = false;
		switch (Row->Target)
		{
		case EGFAbilityTarget::Self:
			bWant = Row->HealFraction > 0.f ? U.HPFraction() < 0.5f : (U.HPFraction() < 0.7f && CountEnemiesWithin(U, 350.f) > 0);
			break;
		case EGFAbilityTarget::AllyArea:
			bWant = CountEnemiesWithin(U, 600.f) > 0;
			break;
		case EGFAbilityTarget::EnemyArea:
		{
			const int32 Need = Profile == EGFArenaAI::Easy ? 1 : FMath::Min(2, EnemiesLeft);
			bWant = CountEnemiesWithin(U, Row->Range) >= FMath::Max(1, Need);
			break;
		}
		case EGFAbilityTarget::Enemy:
			// Champions save combo finishers for when the setup status is on; everyone else just fires.
			bWant = T && CanUseOn(*Row, U, T);
			if (bWant && Profile == EGFArenaAI::Champion && Row->BonusVsStatus != EGFArenaStatus::None && !T->Has(Row->BonusVsStatus) &&
				(Row->BonusVsStatus2 == EGFArenaStatus::None || !T->Has(Row->BonusVsStatus2)) && Rng.FRand() < 0.6f)
			{
				bWant = false;
			}
			break;
		}
		if (bWant && UseAbility(Data, U, i, TargetId))
		{
			return true;
		}
	}
	return false;
}

bool FGFArenaSim::MoveToward(FGFArenaUnit& U, const FVector2D& Goal, float StopAt, float Dt)
{
	if (U.Has(EGFArenaStatus::Root))
	{
		return false;
	}
	const FVector2D Delta = Goal - U.Pos;
	const float D = Delta.Size();
	if (D <= StopAt)
	{
		return false;
	}
	const float Speed = U.Speed * (U.Has(EGFArenaStatus::Wet) ? 0.8f : 1.f) * (U.Has(EGFArenaStatus::Haste) ? 1.25f : 1.f);
	const float Step = FMath::Min(D - StopAt, Speed * Dt);
	U.Pos += Delta / D * Step;
	U.Pos.X = FMath::Clamp(U.Pos.X, -HalfWidth, HalfWidth);
	U.Pos.Y = FMath::Clamp(U.Pos.Y, -HalfHeight, HalfHeight);
	return Step > KINDA_SMALL_NUMBER;
}

void FGFArenaSim::TickUnit(const FGFArenaData& Data, FGFArenaUnit& U, float Dt)
{
	// Statuses and timers.
	if (U.Has(EGFArenaStatus::Burn))
	{
		U.HP -= U.MaxHP * BurnFractionPerSec * Dt;
		if (U.HP <= 0.f)
		{
			OnFainted(U);
			return;
		}
	}
	for (float& S : U.Status)
	{
		S = FMath::Max(0.f, S - Dt);
	}
	const float Rate = U.Has(EGFArenaStatus::Haste) ? 1.25f : 1.f;
	for (float& C : U.Cooldowns)
	{
		C = FMath::Max(0.f, C - Dt * Rate);
	}
	U.AttackTimer = FMath::Max(0.f, U.AttackTimer - Dt * Rate);
	U.DecisionTimer = FMath::Max(0.f, U.DecisionTimer - Dt);
	if (U.Has(EGFArenaStatus::Stun))
	{
		U.IdleTime = 0.f; // stunned isn't stuck
		return;
	}

	bool bActed = false;

	// A Slow Time order runs first.
	if (U.CommandSlot != INDEX_NONE)
	{
		const FGFDinoAbilityRow* Row = Data.Ability(U.Species, U.CommandSlot);
		FGFArenaUnit* T = Find(U.CommandTarget);
		if (!Row || (Row->Target == EGFAbilityTarget::Enemy && (!T || !T->IsAlive())))
		{
			U.CommandSlot = INDEX_NONE; // target gone: order dropped, the AI takes over
		}
		else if (UseAbility(Data, U, U.CommandSlot, U.CommandTarget))
		{
			U.CommandSlot = INDEX_NONE;
			bActed = true;
		}
		else if (Row->Target == EGFAbilityTarget::Enemy || Row->Target == EGFAbilityTarget::EnemyArea)
		{
			const FGFArenaUnit* Goal = T ? T : Find(NearestEnemy(U));
			bActed = Goal && MoveToward(U, Goal->Pos, Row->Range * 0.8f, Dt);
		}
		else
		{
			U.CommandSlot = INDEX_NONE;
		}
		U.IdleTime = 0.f;
		return;
	}

	// Direct control: her input goes through the same ability and attack paths.
	if (!U.PossessedBy.IsEmpty())
	{
		if (!U.MoveInput.IsNearlyZero() && !U.Has(EGFArenaStatus::Root))
		{
			const FVector2D Dir = U.MoveInput.GetSafeNormal();
			MoveToward(U, U.Pos + Dir * 10000.f, 0.f, Dt);
		}
		const int32 Near = NearestEnemy(U, U.Range + 60.f);
		if (U.WantAbility != INDEX_NONE)
		{
			const int32 Target = NearestEnemy(U);
			UseAbility(Data, U, U.WantAbility, Target);
			U.WantAbility = INDEX_NONE;
		}
		if (U.bWantAttack && U.AttackTimer <= 0.f && Near != INDEX_NONE)
		{
			BasicAttack(U, *Find(Near));
		}
		U.IdleTime = 0.f;
		return;
	}

	// AI.
	const int32 TargetId = SelectTarget(U);
	FGFArenaUnit* T = Find(TargetId);
	if (!T)
	{
		return;
	}
	const bool bFragile = U.Role == EGFArenaRole::Skirmisher || U.Role == EGFArenaRole::Assassin;
	if (bFragile && U.HPFraction() < 0.25f && U.RetreatTime < 2.f)
	{
		U.RetreatTime += Dt;
		const FVector2D Away = U.Pos + (U.Pos - T->Pos).GetSafeNormal() * 1000.f;
		bActed = MoveToward(U, Away, 0.f, Dt);
		if (!bActed)
		{
			U.RetreatTime = 2.f; // cornered: turn and fight
		}
	}
	else
	{
		bActed = TryAbilities(Data, U, TargetId);
		if (!bActed)
		{
			const float D = FVector2D::Distance(U.Pos, T->Pos);
			if (D <= U.Range)
			{
				if (U.AttackTimer <= 0.f)
				{
					BasicAttack(U, *T);
				}
				bActed = true; // in range and swinging (or winding up)
			}
			else
			{
				bActed = MoveToward(U, T->Pos, U.Range * 0.9f, Dt);
			}
		}
	}
	U.IdleTime = bActed ? 0.f : U.IdleTime + Dt;
	U.MaxIdle = FMath::Max(U.MaxIdle, U.IdleTime);
}

// ---------------------------------------------------------------- commands

FName FGFArenaSim::CheckCommand(const FGFArenaData& Data, const FGFArenaCommand& Cmd) const
{
	const FGFArenaUnit* U = Find(Cmd.UnitId);
	if (!U || U->OwnerKey.IsEmpty() || U->OwnerKey != Cmd.IssuerKey)
	{
		return TEXT("NotOwner");
	}
	if (!U->IsAlive())
	{
		return TEXT("Fainted");
	}
	const FGFDinoAbilityRow* Row = Data.Ability(U->Species, Cmd.Slot);
	if (!Row || Cmd.Slot < 0 || Cmd.Slot >= U->AbilityCount)
	{
		return TEXT("Locked");
	}
	if (U->Cooldowns[Cmd.Slot] > 0.f)
	{
		return TEXT("Cooldown");
	}
	if (Row->Target == EGFAbilityTarget::Enemy)
	{
		const FGFArenaUnit* T = Find(Cmd.TargetId);
		if (!T || !T->IsAlive() || T->Team == U->Team)
		{
			return TEXT("BadTarget");
		}
	}
	return TEXT("Ok");
}

void FGFArenaSim::ApplyCommands(const FGFArenaData& Data, const TArray<FGFArenaCommand>& Cmds)
{
	for (const FGFArenaCommand& Cmd : Cmds)
	{
		FGFArenaUnit* U = Find(Cmd.UnitId);
		if (!U || CheckCommand(Data, Cmd) != FName(TEXT("Ok")))
		{
			continue; // things changed during the window (fainted, target gone)
		}
		U->CommandSlot = Cmd.Slot;
		U->CommandTarget = Cmd.TargetId;
		// In range now: it goes off immediately, in issue order.
		if (UseAbility(Data, *U, Cmd.Slot, Cmd.TargetId))
		{
			U->CommandSlot = INDEX_NONE;
		}
	}
	CheckEnd();
}

// ---------------------------------------------------------------- possession

int32 FGFArenaSim::PossessedBy(const FString& Key) const
{
	for (const FGFArenaUnit& U : Units)
	{
		if (!Key.IsEmpty() && U.PossessedBy == Key)
		{
			return U.Id;
		}
	}
	return INDEX_NONE;
}

FName FGFArenaSim::Possess(const FString& Key, int32 UnitId)
{
	FGFArenaUnit* U = Find(UnitId);
	if (!U || Key.IsEmpty() || U->OwnerKey != Key)
	{
		return TEXT("NotOwner");
	}
	if (!U->IsAlive())
	{
		return TEXT("Fainted");
	}
	if (!U->PossessedBy.IsEmpty() && U->PossessedBy != Key)
	{
		return TEXT("Taken");
	}
	Release(Key);
	U->PossessedBy = Key;
	U->CommandSlot = INDEX_NONE;
	return TEXT("Ok");
}

int32 FGFArenaSim::Cycle(const FString& Key)
{
	const int32 Current = PossessedBy(Key);
	TArray<int32> Mine;
	for (const FGFArenaUnit& U : Units)
	{
		if (U.OwnerKey == Key && U.IsAlive())
		{
			Mine.Add(U.Id);
		}
	}
	if (Mine.Num() == 0)
	{
		Release(Key);
		return INDEX_NONE;
	}
	const int32 At = Mine.IndexOfByKey(Current);
	const int32 Next = Mine[(At + 1) % Mine.Num()]; // At == INDEX_NONE -> first
	Possess(Key, Next);
	return PossessedBy(Key);
}

void FGFArenaSim::Release(const FString& Key)
{
	for (FGFArenaUnit& U : Units)
	{
		if (!Key.IsEmpty() && U.PossessedBy == Key)
		{
			U.PossessedBy.Reset();
			U.MoveInput = FVector2D::ZeroVector;
			U.bWantAttack = false;
			U.WantAbility = INDEX_NONE;
		}
	}
}

FName FGFArenaSim::SetInput(const FString& Key, const FVector2D& Move, bool bAttack, int32 AbilitySlot)
{
	FGFArenaUnit* U = Find(PossessedBy(Key));
	if (!U)
	{
		return TEXT("NotPossessing");
	}
	U->MoveInput = Move.ClampAxes(-1.f, 1.f);
	U->bWantAttack = bAttack;
	if (AbilitySlot >= 0)
	{
		U->WantAbility = AbilitySlot;
	}
	return TEXT("Ok");
}
