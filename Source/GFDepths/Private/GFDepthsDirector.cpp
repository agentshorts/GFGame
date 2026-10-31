#include "GFDepthsDirector.h"
#include "GFDepthsSite.h"
#include "GFHelperTools.h"
#include "GFMiniGameCatalog.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFRewards.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

const FName AGFDepthsDirector::ModeName(TEXT("Depths"));

namespace GFDepthsVerbs
{
	static const FName Enter(TEXT("Enter"));
	static const FName Leave(TEXT("Leave"));
	static const FName Input(TEXT("Input"));
	static const FName Switch(TEXT("Switch"));
	static const FName Heal(TEXT("Heal"));
	static const FName Attack(TEXT("Attack"));
	static const FName Revive(TEXT("Revive"));
	static const FName LiftHome(TEXT("LiftHome"));
	static const FName Helper(TEXT("Helper"));
}

AGFDepthsDirector::AGFDepthsDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30.f);

	// GetActorLocation reads the root. Without one it is always the origin, so the four
	// sims look stacked even when Get() asked for the pad. Movable so the hub can park it later.
	USceneComponent* PadRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PadRoot"));
	PadRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = PadRoot;
}

AGFDepthsDirector* AGFDepthsDirector::Get(UWorld* World, bool bSpawn)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFDepthsDirector> It(World); It; ++It)
	{
		return *It;
	}
	if (bSpawn && World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector At = FGFMiniGameCatalog::DefaultSpawn(ModeName);
		AGFDepthsDirector* Director = World->SpawnActor<AGFDepthsDirector>(AGFDepthsDirector::StaticClass(), FTransform(At), P);
		if (Director)
		{
			Director->SetActorLocation(At);
		}
		return Director;
	}
	return nullptr;
}

void AGFDepthsDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFDepthsDirector, Depth);
	DOREPLIFETIME(AGFDepthsDirector, Players);
	DOREPLIFETIME(AGFDepthsDirector, Switches);
	DOREPLIFETIME(AGFDepthsDirector, Enemies);
	DOREPLIFETIME(AGFDepthsDirector, PickupTaken);
	DOREPLIFETIME(AGFDepthsDirector, FloatedAreas);
}

void AGFDepthsDirector::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (AGFGameState* G = GS())
		{
			G->RegisterModeHandler(this);
		}
		EnsurePlayableSite();
	}
}

void AGFDepthsDirector::EnsurePlayableSite()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	if (AGFGameState* G = GS())
	{
		G->RegisterModeHandler(this);
	}
	AGFDepthsSite* Site = AGFDepthsSite::Get(World);
	if (!Site)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Site = World->SpawnActor<AGFDepthsSite>(AGFDepthsSite::StaticClass(), FTransform(GetActorLocation()), Params);
	}
	if (!Site)
	{
		return;
	}
	Site->AttachToActor(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Site->BuildIfNeeded();
}

void AGFDepthsDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority())
	{
		// Map closing: everyone still inside keeps a leave share, banked. A faint in the run keeps it all.
		TArray<FString> Keys;
		for (const FGFDepthsPlayerView& P : Players)
		{
			Keys.Add(P.PlayerKey);
		}
		for (const FString& K : Keys)
		{
			RemovePlayer(K);
		}
		if (AGFGameState* G = GS())
		{
			G->UnregisterModeHandler(this);
		}
	}
	Super::EndPlay(Reason);
}

AGFGameState* AGFDepthsDirector::GS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
}

double AGFDepthsDirector::ServerTime() const
{
	if (const UWorld* W = GetWorld())
	{
		if (const AGameStateBase* G = W->GetGameState())
		{
			return G->GetServerWorldTimeSeconds();
		}
		return W->GetTimeSeconds();
	}
	return 0.0;
}

FVector2D AGFDepthsDirector::PlatformPosition(int32 Index) const
{
	const FGFDepthsLevel& L = Level();
	return L.Platforms.IsValidIndex(Index) ? FGFDepthsPlatforms::PositionAt(L.Platforms[Index], Switches, ServerTime()) : FVector2D::ZeroVector;
}

const FGFDepthsPlayerView* AGFDepthsDirector::FindPlayer(const FString& Key) const
{
	return Players.FindByPredicate([&](const FGFDepthsPlayerView& P) { return P.PlayerKey == Key; });
}

FGFDepthsPlayerView* AGFDepthsDirector::Edit(const FString& Key)
{
	return Players.FindByPredicate([&](const FGFDepthsPlayerView& P) { return P.PlayerKey == Key; });
}

void AGFDepthsDirector::StartRun(int32 InDepth)
{
	Depth = InDepth;
	Switches.Reset();
	Enemies.Reset();
	const FGFDepthsLevel& L = Level();
	for (const FGFDepthsEnemyDef& D : L.Enemies)
	{
		FGFDepthsEnemyView& E = Enemies.AddDefaulted_GetRef();
		E.Pos = FVector2D(D.X1, D.Z);
	}
	PickupTaken.Init(false, L.Pickups.Num());
	FloatedAreas.Reset();
	Rng.Initialize(ForcedSeed != 0 ? ForcedSeed : FMath::Rand());
	Accum = 0.f;
}

void AGFDepthsDirector::RescaleEnemies()
{
	const FGFDepthsLevel& L = Level();
	for (int32 i = 0; i < Enemies.Num() && i < L.Enemies.Num(); ++i)
	{
		FGFDepthsEnemyView& E = Enemies[i];
		if (!E.bDefeated && E.HP == E.MaxHP) // only fresh shades grow when a friend joins
		{
			E.MaxHP = FGFDepthsRules::EnemyHP(L.Enemies[i].BaseHP, Depth, Players.Num());
			E.HP = E.MaxHP;
		}
	}
}

void AGFDepthsDirector::Bank(FGFDepthsPlayerView& P, const TArray<FGFItemStack>& Stacks, FName Reason)
{
	if (Stacks.Num() == 0)
	{
		return;
	}
	if (AGFGameState* G = GS())
	{
		G->ServerMint(FGuid::NewGuid(), P.PlayerKey, Stacks, Reason);
	}
	FGFRewards::AddAll(P.Banked, Stacks);
}

void AGFDepthsDirector::Respawn(FGFDepthsPlayerView& P)
{
	const FGFDepthsLevel& L = Level();
	P.Body = FGFDepthsBody();
	P.Body.Pos = L.Checkpoints.IsValidIndex(P.Run.Checkpoint) ? L.Checkpoints[P.Run.Checkpoint] : L.Entrance;
	P.Body.InvulnLeft = FGFDepthsMoveParams().Invulnerable;
}

void AGFDepthsDirector::Faint(FGFDepthsPlayerView& P)
{
	FGFDepthsRules::CheckpointRespawn(P.Run);
	Respawn(P);
}

void AGFDepthsDirector::Hurt(FGFDepthsPlayerView& P, int32 Amount)
{
	if (!FGFDepthsRules::Damage(P.Run, Amount))
	{
		return;
	}
	const bool bFriendUp = Players.ContainsByPredicate([&](const FGFDepthsPlayerView& O) { return O.PlayerKey != P.PlayerKey && !O.Run.bDowned; });
	if (bFriendUp)
	{
		P.Run.bDowned = true;
		P.Run.DownedLeft = FGFDepthsRunState::DownedSeconds;
		P.Body.Vel = FVector2D::ZeroVector;
	}
	else
	{
		Faint(P);
	}
}

bool AGFDepthsDirector::AtLift(const FGFDepthsPlayerView& P) const
{
	const FGFDepthsLevel& L = Level();
	auto Near = [&](const FVector2D& At) { return FMath::Abs(P.Body.Pos.X - At.X) <= LiftReach && FMath::Abs(P.Body.Pos.Y - At.Y) <= LiftReach; };
	if (Near(L.Entrance) || (P.Run.bReachedExit && Near(L.Exit)))
	{
		return true;
	}
	for (int32 i = 0; i <= P.Run.Checkpoint && i < L.Checkpoints.Num(); ++i)
	{
		if (Near(L.Checkpoints[i]))
		{
			return true;
		}
	}
	return false;
}

void AGFDepthsDirector::RemovePlayer(const FString& Key)
{
	if (FGFDepthsPlayerView* P = Edit(Key))
	{
		// Leaving mid-run keeps a share: half the unbanked loot is lost, the rest comes home.
		FGFDepthsRules::LeaveShare(P->Run);
		Bank(*P, FGFDepthsRules::LiftHome(P->Run), TEXT("DepthsLeave"));
	}
	Players.RemoveAll([&](const FGFDepthsPlayerView& P) { return P.PlayerKey == Key; });
	Inputs.Remove(Key);
	if (Players.Num() == 0)
	{
		Depth = 0;
		Enemies.Reset();
		Switches.Reset();
		PickupTaken.Reset();
		FloatedAreas.Reset();
	}
}

void AGFDepthsDirector::TeleportForTests(const FString& Key, const FVector2D& Pos)
{
	if (FGFDepthsPlayerView* P = Edit(Key))
	{
		P->Body = FGFDepthsBody();
		P->Body.Pos = Pos;
	}
}

void AGFDepthsDirector::HitForTests(const FString& Key, int32 Amount)
{
	if (FGFDepthsPlayerView* P = Edit(Key))
	{
		if (FGFDepthsMover::Knockback(FGFDepthsMoveParams::ForDepth(Depth), P->Body, P->Body.Pos.X - 1.f))
		{
			Hurt(*P, Amount);
		}
	}
}

bool AGFDepthsDirector::ArriveAtExit(const FString& Key)
{
	if (!HasAuthority() || Depth == 0)
	{
		return false;
	}
	FGFDepthsPlayerView* P = Edit(Key);
	if (!P || P->Run.bDowned)
	{
		return false;
	}
	if (P->Run.bReachedExit)
	{
		return true;
	}
	if (FPlayerInput* In = Inputs.Find(Key))
	{
		*In = FPlayerInput();
	}
	// One step is enough when the exit floor holds them. A second covers a snap that
	// landed just outside the exit radius on the first try.
	for (int32 Attempt = 0; Attempt < 2 && !P->Run.bReachedExit; ++Attempt)
	{
		P->Body = FGFDepthsBody();
		P->Body.Pos = Level().Exit;
		P->Body.bGrounded = true;
		SimStep(FGFDepthsMover::StepSeconds, ServerTime());
		P = Edit(Key);
		if (!P)
		{
			return false;
		}
	}
	return P->Run.bReachedExit;
}

FGFModeResult AGFDepthsDirector::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& R)
{
	using namespace GFDepthsVerbs;
	if (!By)
	{
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	const FString Key = By->PlayerKey;
	if (R.Verb == Enter)
	{
		const int32 D = R.Int(0, 1);
		if (D < 1 || D > FGFDepthsLevel::MaxDepth)
		{
			return FGFModeResult::Fail(TEXT("BadDepth"));
		}
		if (D > By->Progress.DepthsReached + 1)
		{
			return FGFModeResult::Fail(TEXT("Locked"));
		}
		if (FindPlayer(Key))
		{
			return FGFModeResult::Fail(TEXT("AlreadyIn"));
		}
		if (Depth != 0 && Depth != D)
		{
			return FGFModeResult::Fail(TEXT("RunInProgress"));
		}
		if (Depth == 0)
		{
			StartRun(D);
		}
		FGFDepthsPlayerView& P = Players.AddDefaulted_GetRef();
		P.PlayerKey = Key;
		P.Body.Pos = Level().Entrance;
		Inputs.FindOrAdd(Key);
		RescaleEnemies();
		return FGFModeResult::Ok();
	}
	FGFDepthsPlayerView* P = Edit(Key);
	if (!P)
	{
		return FGFModeResult::Fail(TEXT("NotInRun"));
	}
	if (R.Verb == Leave)
	{
		RemovePlayer(Key);
		return FGFModeResult::Ok();
	}
	if (R.Verb == Input)
	{
		FPlayerInput& In = Inputs.FindOrAdd(Key);
		In.Held.MoveX = FMath::Clamp(R.Float(0), -1.f, 1.f);
		In.bJumpLatch |= R.Int(0) != 0;
		In.Held.bJumpHeld = R.Int(1) != 0;
		In.bDashLatch |= R.Int(2) != 0;
		return FGFModeResult::Ok();
	}
	if (R.Verb == Switch)
	{
		const FName K = R.Name(0);
		if (!FGFDepthsPlatforms::IsKey(K))
		{
			return FGFModeResult::Fail(TEXT("BadKey"));
		}
		return FGFDepthsPlatforms::Press(Switches, K, ServerTime(), Key) ? FGFModeResult::Ok() : FGFModeResult::Fail(TEXT("Cooldown"));
	}
	if (R.Verb == Heal)
	{
		if (P->Run.Heals <= 0)
		{
			return FGFModeResult::Fail(TEXT("NoHeals"));
		}
		return FGFDepthsRules::Heal(P->Run) ? FGFModeResult::Ok() : FGFModeResult::Fail(TEXT("CantHeal"));
	}
	if (R.Verb == Attack)
	{
		FPlayerInput& In = Inputs.FindOrAdd(Key);
		const double Now = ServerTime();
		if (P->Run.bDowned)
		{
			return FGFModeResult::Fail(TEXT("Downed"));
		}
		if (Now - In.LastAttack < AttackCooldown)
		{
			return FGFModeResult::Fail(TEXT("Cooldown"));
		}
		In.LastAttack = Now;
		FGFModeResult Res = FGFModeResult::Ok();
		int32 Hits = 0;
		for (FGFDepthsEnemyView& E : Enemies)
		{
			const float Dx = (E.Pos.X - P->Body.Pos.X) * P->Body.Facing;
			if (!E.bDefeated && Dx >= -20.f && Dx <= AttackReach && FMath::Abs(E.Pos.Y - P->Body.Pos.Y) <= 100.f)
			{
				++Hits;
				if (--E.HP <= 0)
				{
					E.bDefeated = true;
					FGFDepthsRules::RollLoot(Depth, TEXT("Enemy"), Rng, P->Run.Unbanked);
				}
			}
		}
		Res.Ints = { Hits };
		return Res;
	}
	if (R.Verb == Revive)
	{
		if (P->Run.bDowned)
		{
			return FGFModeResult::Fail(TEXT("Downed"));
		}
		for (FGFDepthsPlayerView& O : Players)
		{
			if (O.Run.bDowned && FVector2D::Distance(O.Body.Pos, P->Body.Pos) <= LiftReach)
			{
				O.Run.bDowned = false;
				O.Run.DownedLeft = 0.f;
				O.Run.HP = FGFDepthsRunState::ReviveHP;
				O.Body.InvulnLeft = FGFDepthsMoveParams().Invulnerable;
				return FGFModeResult::Ok();
			}
		}
		return FGFModeResult::Fail(TEXT("NoOneDown"));
	}
	if (R.Verb == Helper)
	{
		const FString DinoId = R.Name(0).IsNone() ? FString() : R.Name(0).ToString();
		const FName ToolId = R.Name(1);
		FGFHelperArgs Args;
		if (!R.Name(2).IsNone())
		{
			const FString Value = R.Name(2).ToString();
			if (ToolId == FGFHelperTools::FloatWater)
			{
				Args.Area = Value;
				Args.bArea = true;
			}
			else if (ToolId == FGFHelperTools::FlyOver)
			{
				Args.Level = Value;
				Args.bLevel = true;
			}
			else
			{
				Args.Where = Value;
				Args.bWhere = true;
			}
		}
		const FGFHelperResolution Resolved = FGFHelperTools::Resolve(FGFHelperTools::BearerFor(DinoId), ToolId, Args);
		if (Resolved.bDecline)
		{
			return FGFModeResult::Ok();
		}
		if (!Resolved.bOk)
		{
			FName Why = TEXT("BadArgs");
			if (Resolved.Fail == EGFHelperFail::UnknownTool)
			{
				Why = TEXT("UnknownTool");
			}
			else if (Resolved.Fail == EGFHelperFail::NotOffered)
			{
				Why = TEXT("NotOffered");
			}
			return FGFModeResult::Fail(Why);
		}
		FName Code;
		if (!FGFDepthsHelp::Apply(Level(), P->Body, P->Run.Checkpoint, FloatedAreas, Resolved, Code))
		{
			return FGFModeResult::Fail(Code);
		}
		if (Resolved.ToolId == FGFHelperTools::FloatWater)
		{
			if (const FGFDepthsWater* W = FGFDepthsHelp::FindWater(Level(), Resolved.Args.Area))
			{
				const FGFDepthsMoveParams Params = FGFDepthsMoveParams::ForDepth(Depth);
				for (FGFDepthsPlayerView& Other : Players)
				{
					if (Other.PlayerKey != P->PlayerKey)
					{
						FGFDepthsHelp::LiftIfInside(Params, *W, Other.Body);
					}
				}
			}
		}
		return FGFModeResult::Ok();
	}
	if (R.Verb == LiftHome)
	{
		if (P->Run.bDowned)
		{
			return FGFModeResult::Fail(TEXT("Downed"));
		}
		if (!AtLift(*P))
		{
			return FGFModeResult::Fail(TEXT("NotAtLift"));
		}
		FGFModeResult Res = FGFModeResult::Ok();
		Res.Stacks = FGFDepthsRules::LiftHome(P->Run);
		Bank(*P, Res.Stacks, TEXT("DepthsLiftHome"));
		if (P->Run.bReachedExit && Depth > By->Progress.DepthsReached)
		{
			By->Progress.DepthsReached = Depth;
			By->MarkCharacterChanged();
		}
		Res.Ints = { By->Progress.DepthsReached };
		Players.RemoveAll([&](const FGFDepthsPlayerView& X) { return X.PlayerKey == Key; });
		Inputs.Remove(Key);
		if (Players.Num() == 0)
		{
			RemovePlayer(FString()); // resets the run
		}
		return Res;
	}
	return FGFModeResult::Fail(TEXT("UnknownVerb"));
}

void AGFDepthsDirector::OnModePlayerLeft(AGFPlayerState* PS)
{
	if (PS && FindPlayer(PS->PlayerKey))
	{
		RemovePlayer(PS->PlayerKey);
	}
}

void AGFDepthsDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || Depth == 0)
	{
		return;
	}
	Accum = FMath::Min(Accum + DeltaSeconds, 6.f * FGFDepthsMover::StepSeconds);
	const double Now = ServerTime();
	int32 Steps = 0;
	while (Accum >= FGFDepthsMover::StepSeconds)
	{
		Accum -= FGFDepthsMover::StepSeconds;
		++Steps;
	}
	for (int32 i = Steps - 1; i >= 0; --i)
	{
		SimStep(FGFDepthsMover::StepSeconds, Now - i * FGFDepthsMover::StepSeconds);
	}
}

void AGFDepthsDirector::SimStep(float Dt, double Now)
{
	const FGFDepthsLevel& L = Level();
	const FGFDepthsMoveParams Params = FGFDepthsMoveParams::ForDepth(Depth);

	// Shades patrol.
	for (int32 i = 0; i < Enemies.Num() && i < L.Enemies.Num(); ++i)
	{
		FGFDepthsEnemyView& E = Enemies[i];
		const FGFDepthsEnemyDef& D = L.Enemies[i];
		if (E.bDefeated)
		{
			continue;
		}
		E.Pos.X += E.Dir * D.Speed * Dt;
		if (E.Pos.X >= D.X2) { E.Pos.X = D.X2; E.Dir = -1; }
		if (E.Pos.X <= D.X1) { E.Pos.X = D.X1; E.Dir = 1; }
	}

	for (FGFDepthsPlayerView& P : Players)
	{
		FPlayerInput& In = Inputs.FindOrAdd(P.PlayerKey);
		if (P.Run.bDowned)
		{
			P.Run.DownedLeft -= Dt;
			if (P.Run.DownedLeft <= 0.f)
			{
				Faint(P);
			}
			continue;
		}
		FGFDepthsInput Frame = In.Held;
		Frame.bJumpPressed = In.bJumpLatch;
		Frame.bDashPressed = In.bDashLatch;
		In.bJumpLatch = false;
		In.bDashLatch = false;
		TArray<FBox2D> Floors;
		FGFDepthsHelp::AppendFloatedFloors(L, FloatedAreas, Floors);
		FGFDepthsMover::Step(Params, L, Switches, Now, Frame, P.Body, Dt, Floors.Num() > 0 ? &Floors : nullptr);

		// Fell into the dark: 1 damage, back at the checkpoint.
		if (P.Body.Pos.Y < -1200.f)
		{
			Respawn(P);
			Hurt(P, 1);
			continue;
		}
		const FBox2D Me = FGFDepthsMover::BodyBox(Params, P.Body.Pos);
		for (const FBox2D& H : L.Hazards)
		{
			if (Me.Intersect(H) && FGFDepthsMover::Knockback(Params, P.Body, H.GetCenter().X))
			{
				Hurt(P, 1);
			}
		}
		for (const FGFDepthsEnemyView& E : Enemies)
		{
			if (!E.bDefeated && FMath::Abs(E.Pos.X - P.Body.Pos.X) < 60.f && FMath::Abs(E.Pos.Y - P.Body.Pos.Y) < 90.f &&
				FGFDepthsMover::Knockback(Params, P.Body, E.Pos.X))
			{
				Hurt(P, 1);
			}
		}
		if (P.Run.bDowned)
		{
			continue;
		}
		for (int32 i = 0; i < L.Pickups.Num(); ++i)
		{
			if (PickupTaken.IsValidIndex(i) && !PickupTaken[i] && FVector2D::Distance(L.Pickups[i], P.Body.Pos) < 70.f)
			{
				PickupTaken[i] = true;
				FGFDepthsRules::RollLoot(Depth, TEXT("Pickup"), Rng, P.Run.Unbanked);
			}
		}
		for (int32 i = 0; i < L.Checkpoints.Num(); ++i)
		{
			TArray<FGFItemStack> ToBank;
			if (FMath::Abs(L.Checkpoints[i].X - P.Body.Pos.X) < 80.f && FMath::Abs(L.Checkpoints[i].Y - P.Body.Pos.Y) < 120.f &&
				FGFDepthsRules::ReachCheckpoint(P.Run, i, ToBank))
			{
				Bank(P, ToBank, TEXT("DepthsCheckpoint"));
			}
		}
		if (!P.Run.bReachedExit && FMath::Abs(L.Exit.X - P.Body.Pos.X) < 80.f && FMath::Abs(L.Exit.Y - P.Body.Pos.Y) < 120.f)
		{
			P.Run.bReachedExit = true;
			FGFDepthsRules::RollLoot(Depth, TEXT("Exit"), Rng, P.Run.Unbanked);
		}
		P.Run.Section = FMath::Max(P.Run.Section, P.Run.Checkpoint + 1);
	}
}
