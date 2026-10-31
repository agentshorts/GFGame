#include "GFArenaTypes.h"

namespace
{
	struct FArenaOption
	{
		float Score = 0.f;
		int32 Slot = -1;
		int32 Target = INDEX_NONE;
		bool bRetreat = false;
	};

	float SnapLaneY(float Y)
	{
		if (Y < -175.f)
		{
			return -350.f;
		}
		if (Y > 175.f)
		{
			return 350.f;
		}
		return 0.f;
	}

	bool OptionLess(const FArenaOption& A, const FArenaOption& B)
	{
		if (FMath::Abs(A.Score - B.Score) > 0.01f)
		{
			return A.Score > B.Score;
		}
		if (A.Slot != B.Slot)
		{
			return A.Slot < B.Slot;
		}
		if (A.bRetreat != B.bRetreat)
		{
			return A.bRetreat < B.bRetreat;
		}
		return A.Target < B.Target;
	}
}

TArray<FName>& FGFArenaSim::LoadoutFor(int32 Team)
{
	return Team == 1 ? Loadout1 : Loadout0;
}

const TArray<FName>& FGFArenaSim::LoadoutFor(int32 Team) const
{
	return Team == 1 ? Loadout1 : Loadout0;
}

TMap<FName, int32>& FGFArenaSim::SpellLockFor(int32 Team)
{
	return Team == 1 ? SpellLock1 : SpellLock0;
}

int32 FGFArenaSim::SpellLockOf(int32 Team, FName SpellId) const
{
	const TMap<FName, int32>& Lock = Team == 1 ? SpellLock1 : SpellLock0;
	const int32* Found = Lock.Find(SpellId);
	return Found ? *Found : 0;
}

void FGFArenaSim::SetLoadout(int32 Team, const TArray<FName>& Spells)
{
	TArray<FName>& Dest = LoadoutFor(Team);
	Dest.Reset();
	for (const FName Id : Spells)
	{
		if (Dest.Num() >= 4)
		{
			break;
		}
		if (!Id.IsNone() && !Dest.Contains(Id))
		{
			Dest.Add(Id);
		}
	}
}

void FGFArenaSim::OnSlowWindowEnded()
{
	auto Decay = [](TMap<FName, int32>& Lock)
	{
		for (TPair<FName, int32>& Pair : Lock)
		{
			if (Pair.Value > 0)
			{
				--Pair.Value;
			}
		}
	};
	Decay(SpellLock0);
	Decay(SpellLock1);
}

float FGFArenaSim::TerrainTerm(const FGFArenaUnit& U) const
{
	if (U.Terrain > 0.f)
	{
		return FGFArenaMath::HighGroundWeight * U.Terrain;
	}
	if (U.Terrain < 0.f)
	{
		return FGFArenaMath::MireWeight * (-U.Terrain);
	}
	return 0.f;
}

bool FGFArenaSim::DiesWithin(const FGFArenaUnit& U, float Seconds) const
{
	if (!U.IsAlive() || Seconds <= 0.f)
	{
		return false;
	}
	const FGFArenaUnit* Enemy = Find(NearestEnemy(U, 900.f));
	if (!Enemy)
	{
		return false;
	}
	const float Hits = Seconds / FMath::Max(0.2f, Enemy->AttackInterval);
	const bool bClose = FVector2D::Distance(U.Pos, Enemy->Pos) <= Enemy->Range + Enemy->Speed * Seconds;
	return bClose && U.HP <= Enemy->Attack * Hits * 1.2f;
}

float FGFArenaSim::ScoreFor(int32 Team) const
{
	if (Outcome == EGFArenaOutcome::Team0Won)
	{
		return Team == 0 ? 10000.f : -10000.f;
	}
	if (Outcome == EGFArenaOutcome::Team1Won)
	{
		return Team == 1 ? 10000.f : -10000.f;
	}
	if (Outcome == EGFArenaOutcome::Draw)
	{
		return 0.f;
	}

	const FGFArenaUnit* Anchor = nullptr;
	for (const FGFArenaUnit& U : Units)
	{
		if (U.Team != Team)
		{
			continue;
		}
		if (!Anchor || U.MaxHP > Anchor->MaxHP || (U.MaxHP == Anchor->MaxHP && U.Id < Anchor->Id))
		{
			Anchor = &U;
		}
	}

	float Score = 0.f;
	for (const FGFArenaUnit& U : Units)
	{
		const float Sign = U.Team == Team ? 1.f : -1.f;
		const float Weight = (Anchor && U.Id == Anchor->Id) ? 8.f : 1.7f;
		Score += Sign * U.HP * Weight;
		Score += Sign * TerrainTerm(U);
		if (!U.IsAlive())
		{
			Score += U.Team == Team ? -400.f : 400.f;
		}
	}
	for (const FGFArenaUnit& U : Units)
	{
		if (U.Team == Team && U.IsAlive() && DiesWithin(U, 2.f))
		{
			Score -= 1200.f;
		}
	}
	return Score;
}

void FGFArenaSim::Project(const FGFArenaData& Data, float Horizon)
{
	const int32 Steps = FMath::Clamp(FMath::RoundToInt(Horizon / StepSeconds), 1, 80);
	for (int32 n = 0; n < Steps && !IsOver(); ++n)
	{
		Time += StepSeconds;
		TickSpark(StepSeconds);
		for (FGFArenaUnit& U : Units)
		{
			if (!U.IsAlive())
			{
				continue;
			}
			if (U.Has(EGFArenaStatus::Burn))
			{
				U.HP -= U.MaxHP * BurnFractionPerSec * StepSeconds;
				if (U.HP <= 0.f)
				{
					OnFainted(U);
					continue;
				}
			}
			for (float& Status : U.Status)
			{
				Status = FMath::Max(0.f, Status - StepSeconds);
			}
			const float Rate = U.Has(EGFArenaStatus::Haste) ? 1.25f : 1.f;
			for (float& Cool : U.Cooldowns)
			{
				Cool = FMath::Max(0.f, Cool - StepSeconds * Rate);
			}
			U.AttackTimer = FMath::Max(0.f, U.AttackTimer - StepSeconds * Rate);
			if (U.Has(EGFArenaStatus::Stun))
			{
				continue;
			}
			const int32 TargetId = SelectTarget(U);
			FGFArenaUnit* Target = Find(TargetId);
			if (!Target)
			{
				continue;
			}
			if (U.Has(EGFArenaStatus::Root))
			{
				if (FVector2D::Distance(U.Pos, Target->Pos) <= U.Range && U.AttackTimer <= 0.f)
				{
					BasicAttack(U, *Target);
				}
				continue;
			}
			if (FVector2D::Distance(U.Pos, Target->Pos) <= U.Range)
			{
				if (U.AttackTimer <= 0.f)
				{
					BasicAttack(U, *Target);
				}
			}
			else
			{
				MoveToward(U, Target->Pos, U.Range * 0.9f, StepSeconds);
			}
		}
		TickHotSand();
		CheckEnd();
		if (!IsOver() && Time >= MaxSeconds)
		{
			Outcome = EGFArenaOutcome::Draw;
		}
	}
}

FName FGFArenaSim::CastSpell(const FGFArenaData& Data, int32 Team, FName SpellId, int32 TargetId, bool bAlt, bool bFromPlayer)
{
	const int32 Side = Team == 1 ? 1 : 0;
	const FGFArenaSpellRow* Row = Data.Spell(SpellId);
	if (!Row)
	{
		return TEXT("BadSpell");
	}
	if (bFromPlayer && !bSpellsOpen)
	{
		return TEXT("NoSlowTime");
	}
	if (!LoadoutFor(Side).Contains(SpellId))
	{
		return TEXT("NoLoadout");
	}
	if (SpellLockOf(Side, SpellId) > 0)
	{
		return TEXT("Cooldown");
	}
	const bool bUseAlt = bAlt && Row->AltCost > 0;
	const int32 Cost = bUseAlt ? Row->AltCost : Row->Cost;
	if (Spark[Side] < Cost)
	{
		return TEXT("NoSpark");
	}

	FGFArenaUnit* Target = Find(TargetId);
	TArray<FGFArenaUnit*> Hits;
	if (Row->bLaneBlock)
	{
		FGFArenaWall Wall;
		Wall.LaneY = Target ? SnapLaneY(Target->Pos.Y) : 0.f;
		Wall.Until = Time + FMath::Max(0.1f, Row->Duration);
		Walls.Add(Wall);
	}
	else if (Row->Radius > 0.f)
	{
		if (!Target || !Target->IsAlive())
		{
			return TEXT("BadTarget");
		}
		for (FGFArenaUnit& U : Units)
		{
			if (U.Team != Side && U.IsAlive() && FVector2D::Distance(U.Pos, Target->Pos) <= Row->Radius)
			{
				Hits.Add(&U);
			}
		}
		if (Hits.Num() == 0)
		{
			return TEXT("BadTarget");
		}
	}
	else if (Row->bAllies)
	{
		if (Row->bPush)
		{
			for (FGFArenaUnit& U : Units)
			{
				if (U.Team == Side && U.IsAlive())
				{
					Hits.Add(&U);
				}
			}
		}
		else if (Target && Target->IsAlive() && Target->Team == Side)
		{
			Hits.Add(Target);
		}
		else
		{
			FGFArenaUnit* Weak = nullptr;
			for (FGFArenaUnit& U : Units)
			{
				if (U.Team == Side && U.IsAlive() && (!Weak || U.HPFraction() < Weak->HPFraction()))
				{
					Weak = &U;
				}
			}
			if (!Weak)
			{
				return TEXT("BadTarget");
			}
			Hits.Add(Weak);
		}
	}
	else
	{
		if (!Target || !Target->IsAlive() || Target->Team == Side)
		{
			return TEXT("BadTarget");
		}
		Hits.Add(Target);
	}

	int32 SourceId = INDEX_NONE;
	for (const FGFArenaUnit& U : Units)
	{
		if (U.Team == Side && U.IsAlive())
		{
			SourceId = U.Id;
			break;
		}
	}

	const int32 Points = bUseAlt ? Row->AltDamagePoints : Row->DamagePoints;
	const float Heal = bUseAlt && Row->AltHealFraction > 0.f ? Row->AltHealFraction : Row->HealFraction;
	const EGFArenaStatus Status = Row->Status;
	const float StatusSeconds = Row->StatusSeconds;
	const FVector2D Whistle = Target && Target->Team == Side ? Target->Pos : FVector2D::ZeroVector;

	for (FGFArenaUnit* Hit : Hits)
	{
		if (!Hit || !Hit->IsAlive())
		{
			continue;
		}
		if (Points > 0 && Hit->Team != Side)
		{
			NoteHurt(*Hit, FGFArenaMath::SpellDamage(Hit->MaxHP, Points, Hit->Has(EGFArenaStatus::Shred), Hit->Has(EGFArenaStatus::Guard)), SourceId);
		}
		if (!Hit->IsAlive())
		{
			continue;
		}
		if (Hit->Team != Side)
		{
			SetStatus(*Hit, bUseAlt && Row->AltStatus != EGFArenaStatus::None ? Row->AltStatus : Status, bUseAlt && Row->AltStatusSeconds > 0.f ? Row->AltStatusSeconds : StatusSeconds);
		}
		else
		{
			SetStatus(*Hit, Status, StatusSeconds);
			if (Heal > 0.f)
			{
				Hit->HP = FMath::Min(Hit->MaxHP, Hit->HP + Hit->MaxHP * Heal);
			}
			if (Row->AttackMul > 1.01f)
			{
				Hit->AttackMul = Row->AttackMul;
				Hit->AttackMulUntil = Time + FMath::Max(0.1f, Row->Duration);
			}
			if (Row->bPush && !Hit->Has(EGFArenaStatus::Root))
			{
				const FVector2D Delta = Whistle - Hit->Pos;
				if (Delta.SizeSquared() > 1.f)
				{
					Hit->Pos += Delta.GetSafeNormal() * FMath::Min(200.f, Delta.Size());
					Hit->Pos.X = FMath::Clamp(Hit->Pos.X, -HalfWidth, HalfWidth);
					Hit->Pos.Y = FMath::Clamp(Hit->Pos.Y, -HalfHeight, HalfHeight);
				}
			}
		}
	}

	Spark[Side] -= Cost;
	if (Row->WindowCooldown > 0)
	{
		SpellLockFor(Side).Add(SpellId, Row->WindowCooldown);
	}
	return TEXT("Ok");
}

void FGFArenaSim::RememberPlan(const FGFArenaUnit& U, int32 Slot, int32 TargetId, bool bRetreat)
{
	if (bReplayPlans || !PlanLog)
	{
		return;
	}
	FGFArenaPlan Plan;
	Plan.Time = Time;
	Plan.UnitId = U.Id;
	Plan.Slot = Slot;
	Plan.TargetId = TargetId;
	Plan.bRetreat = bRetreat;
	PlanLog->Add(Plan);
}

void FGFArenaSim::RememberHash()
{
	if (HashLog)
	{
		HashLog->Add(StateHash());
	}
}

uint32 FGFArenaSim::StateHash() const
{
	uint32 Hash = 2166136261u;
	auto Mix = [&Hash](uint32 Value)
	{
		Hash ^= Value;
		Hash *= 16777619u;
	};
	auto MixFloat = [&Mix](float Value)
	{
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		Mix(Bits);
	};
	Mix(static_cast<uint32>(Outcome));
	MixFloat(Time);
	Mix(static_cast<uint32>(Spark[0]));
	Mix(static_cast<uint32>(Spark[1]));
	Mix(static_cast<uint32>(Walls.Num()));
	for (const FGFArenaUnit& U : Units)
	{
		Mix(static_cast<uint32>(U.Id));
		MixFloat(U.HP);
		MixFloat(U.Pos.X);
		MixFloat(U.Pos.Y);
		Mix(U.bFainted ? 1u : 0u);
		Mix(static_cast<uint32>(U.StatusMask()));
		Mix(static_cast<uint32>(U.CommandSlot + 3));
		Mix(U.bIntentRetreat ? 1u : 0u);
		Mix(static_cast<uint32>(U.IntentSlot + 3));
	}
	return Hash;
}

void FGFArenaSim::ChooseIntent(const FGFArenaData& Data, FGFArenaUnit& U)
{
	const int32 TargetId = SelectTarget(U);
	const bool bFragile = U.Role == EGFArenaRole::Skirmisher || U.Role == EGFArenaRole::Assassin;
	if (((bFragile && U.HPFraction() < 0.25f) || DiesWithin(U, 2.f)) && U.RetreatTime < 2.f)
	{
		U.bIntentRetreat = true;
		U.IntentSlot = -1;
		U.IntentTarget = TargetId;
		U.bIntentSpent = false;
		RememberPlan(U, -1, TargetId, true);
		return;
	}

	U.bIntentRetreat = false;
	TArray<FArenaOption> Options;
	FArenaOption Baseline;
	Baseline.Slot = -1;
	Baseline.Target = TargetId;
	Options.Add(Baseline);

	for (int32 Slot = 0; Slot < U.AbilityCount; ++Slot)
	{
		const FGFDinoAbilityRow* Row = Data.Ability(U.Species, Slot);
		if (!Row || U.Cooldowns[Slot] > 0.f)
		{
			continue;
		}
		if (Row->Target == EGFAbilityTarget::Self && Row->HealFraction > 0.f && U.HPFraction() >= 0.5f)
		{
			continue;
		}
		const int32 Aim = (Row->Target == EGFAbilityTarget::Enemy || Row->Target == EGFAbilityTarget::EnemyArea) ? TargetId : INDEX_NONE;
		const FGFArenaUnit* AimUnit = Find(Aim);
		if ((Row->Target == EGFAbilityTarget::Enemy || Row->Target == EGFAbilityTarget::EnemyArea) && !Row->bDash && !CanUseOn(*Row, U, AimUnit))
		{
			continue;
		}
		FArenaOption Opt;
		Opt.Slot = Slot;
		Opt.Target = Aim;
		Options.Add(Opt);
	}

	const bool bChampion = TeamAI[U.Team & 1] == EGFArenaAI::Champion;
	if (bChampion)
	{
		const TArray<FName>& Spells = LoadoutFor(U.Team);
		for (int32 i = 0; i < Spells.Num(); ++i)
		{
			const FGFArenaSpellRow* Row = Data.Spell(Spells[i]);
			if (!Row || SpellLockOf(U.Team, Spells[i]) > 0 || Spark[U.Team & 1] < Row->Cost)
			{
				continue;
			}
			FArenaOption Opt;
			Opt.Slot = 8 + i;
			Opt.Target = Row->bAllies ? INDEX_NONE : TargetId;
			Options.Add(Opt);
		}
	}

	auto ScoreOption = [&](const FArenaOption& Opt, float Horizon) -> float
	{
		FGFArenaSim Child = *this;
		Child.PlanLog = nullptr;
		Child.HashLog = nullptr;
		Child.bReplayPlans = false;
		Child.Pending = 0.f;
		FGFArenaUnit* Actor = Child.Find(U.Id);
		if (!Actor)
		{
			return -100000.f;
		}
		if (Opt.Slot >= 8)
		{
			const TArray<FName>& Spells = Child.LoadoutFor(Actor->Team);
			const int32 Index = Opt.Slot - 8;
			if (Spells.IsValidIndex(Index))
			{
				Child.CastSpell(Data, Actor->Team, Spells[Index], Opt.Target, false, false);
			}
		}
		else if (Opt.Slot >= 0)
		{
			Child.UseAbility(Data, *Actor, Opt.Slot, Opt.Target);
		}
		Child.Project(Data, Horizon);
		return Child.ScoreFor(U.Team);
	};

	for (FArenaOption& Opt : Options)
	{
		Opt.Score = ScoreOption(Opt, LookaheadSeconds);
	}
	Options.Sort(OptionLess);

	if (bChampion && Options.Num() > 1)
	{
		const int32 N = FMath::Min(2, Options.Num());
		for (int32 i = 0; i < N; ++i)
		{
			Options[i].Score = ScoreOption(Options[i], LookaheadSeconds * 2.f);
		}
		FArenaOption A = Options[0];
		FArenaOption B = Options[1];
		Options[0] = OptionLess(A, B) ? A : B;
		Options[1] = OptionLess(A, B) ? B : A;
	}

	int32 Pick = 0;
	const EGFArenaAI Profile = TeamAI[U.Team & 1];
	if (Profile == EGFArenaAI::Easy && Options.Num() > 1)
	{
		Pick = Rng.RandHelper(FMath::Min(3, Options.Num()));
	}

	const FArenaOption& Best = Options[Pick];
	U.IntentSlot = Best.Slot;
	U.IntentTarget = Best.Target;
	U.bIntentRetreat = false;
	U.bIntentSpent = false;
	RememberPlan(U, Best.Slot, Best.Target, false);
}

void FGFArenaSim::ExecuteIntent(const FGFArenaData& Data, FGFArenaUnit& U, float Dt, bool& bActed)
{
	if (U.bIntentRetreat)
	{
		U.RetreatTime += Dt;
		const FGFArenaUnit* Threat = Find(U.IntentTarget);
		if (!Threat || !Threat->IsAlive())
		{
			Threat = Find(NearestEnemy(U));
		}
		if (Threat)
		{
			const FVector2D Away = U.Pos + (U.Pos - Threat->Pos).GetSafeNormal() * 1000.f;
			bActed = MoveToward(U, Away, 0.f, Dt);
		}
		if (!bActed)
		{
			U.RetreatTime = 2.f;
		}
		return;
	}

	if (U.IntentSlot >= 8 && !U.bIntentSpent)
	{
		const TArray<FName>& Spells = LoadoutFor(U.Team);
		const int32 Index = U.IntentSlot - 8;
		if (Spells.IsValidIndex(Index))
		{
			CastSpell(Data, U.Team, Spells[Index], U.IntentTarget, false, false);
		}
		U.bIntentSpent = true;
		bActed = true;
		return;
	}

	if (U.IntentSlot >= 0 && U.IntentSlot < 8 && !U.bIntentSpent)
	{
		if (UseAbility(Data, U, U.IntentSlot, U.IntentTarget))
		{
			U.bIntentSpent = true;
			bActed = true;
			return;
		}
		const FGFDinoAbilityRow* Row = Data.Ability(U.Species, U.IntentSlot);
		const FGFArenaUnit* Goal = Find(U.IntentTarget);
		if (Row && Goal && Goal->IsAlive() && (Row->Target == EGFAbilityTarget::Enemy || Row->Target == EGFAbilityTarget::EnemyArea))
		{
			bActed = MoveToward(U, Goal->Pos, Row->Range * 0.8f, Dt);
			return;
		}
		U.bIntentSpent = true;
	}

	int32 TargetId = U.IntentTarget;
	FGFArenaUnit* Target = Find(TargetId);
	if (!Target || !Target->IsAlive() || Target->Team == U.Team)
	{
		TargetId = SelectTarget(U);
		Target = Find(TargetId);
	}
	if (!Target)
	{
		return;
	}
	if (FVector2D::Distance(U.Pos, Target->Pos) <= U.Range)
	{
		if (U.AttackTimer <= 0.f)
		{
			BasicAttack(U, *Target);
		}
		bActed = true;
	}
	else
	{
		bActed = MoveToward(U, Target->Pos, U.Range * 0.9f, Dt);
	}
}

bool FGFArenaSim::RunBrain(const FGFArenaData& Data, FGFArenaUnit& U, float Dt)
{
	if (U.DecisionTimer <= 0.f)
	{
		if (bReplayPlans && PlanLog && ReplayCursor < PlanLog->Num() && (*PlanLog)[ReplayCursor].UnitId == U.Id)
		{
			const FGFArenaPlan& Plan = (*PlanLog)[ReplayCursor++];
			U.bIntentRetreat = Plan.bRetreat;
			U.IntentSlot = Plan.Slot;
			U.IntentTarget = Plan.TargetId;
			U.bIntentSpent = false;
		}
		else if (!bReplayPlans)
		{
			ChooseIntent(Data, U);
		}
		U.DecisionTimer = DecisionSeconds;
		RememberHash();
	}
	bool bActed = false;
	ExecuteIntent(Data, U, Dt, bActed);
	return bActed;
}

void FGFArenaSim::BuildGhostOrders(const FGFArenaData& Data, int32 Team, TArray<FGFArenaCommand>& Out)
{
	Out.Reset();
	for (const FGFArenaUnit& U : Units)
	{
		if (U.Team != Team || !U.IsAlive() || !U.PossessedBy.IsEmpty())
		{
			continue;
		}
		FGFArenaSim Child = *this;
		Child.PlanLog = nullptr;
		Child.HashLog = nullptr;
		Child.bReplayPlans = false;
		Child.Pending = 0.f;
		if (FGFArenaUnit* Copy = Child.Find(U.Id))
		{
			Child.ChooseIntent(Data, *Copy);
			FGFArenaCommand Cmd;
			Cmd.IssuerKey = U.OwnerKey;
			Cmd.UnitId = U.Id;
			Cmd.TargetId = Copy->IntentTarget;
			Cmd.Slot = (Copy->IntentSlot >= 0 && Copy->IntentSlot < 8) ? Copy->IntentSlot : INDEX_NONE;
			Out.Add(Cmd);
		}
	}
}

double FGFArenaSim::DebugDecisionMillis(const FGFArenaData& Data, int32 UnitId)
{
	FGFArenaUnit* U = Find(UnitId);
	if (!U || !U->IsAlive())
	{
		return 0.0;
	}
	FGFArenaSim Child = *this;
	Child.PlanLog = nullptr;
	Child.HashLog = nullptr;
	Child.bReplayPlans = false;
	Child.Pending = 0.f;
	FGFArenaUnit* Copy = Child.Find(UnitId);
	const double Started = FPlatformTime::Seconds();
	if (Copy)
	{
		Child.ChooseIntent(Data, *Copy);
	}
	return (FPlatformTime::Seconds() - Started) * 1000.0;
}
