#include "GFDepthsTypes.h"
#include "GFDataTables.h"

namespace GFDepthsSim
{
	static bool Overlaps(const FBox2D& A, const FBox2D& B)
	{
		return A.Min.X < B.Max.X && A.Max.X > B.Min.X && A.Min.Y < B.Max.Y && A.Max.Y > B.Min.Y;
	}

	static FBox2D Box(float X1, float Z1, float X2, float Z2)
	{
		return FBox2D(FVector2D(X1, Z1), FVector2D(X2, Z2));
	}
}

// ---------------------------------------------------------------- params / level

FGFDepthsMoveParams FGFDepthsMoveParams::ForDepth(int32 Depth)
{
	FGFDepthsMoveParams P;
	P.bDoubleJump = Depth >= 2;
	return P;
}

int32 FGFDepthsLevel::SectionAt(float X) const
{
	int32 S = 0;
	for (int32 i = 0; i < Checkpoints.Num(); ++i)
	{
		if (X >= Checkpoints[i].X)
		{
			S = i + 1;
		}
	}
	return S;
}

FGFDepthsLevel FGFDepthsLevel::MakeGreybox(int32 Depth)
{
	using GFDepthsSim::Box;
	FGFDepthsLevel L;
	L.Depth = FMath::Clamp(Depth, 1, MaxDepth);
	// Section 0: the entrance hall, then a gap the Z platform ferries across.
	L.Solids.Add(Box(-400.f, -200.f, -300.f, 1500.f)); // left wall
	L.Solids.Add(Box(-300.f, -200.f, 1200.f, 0.f));
	L.Entrance = FVector2D(0.f, 60.f);
	L.Pickups.Add(FVector2D(800.f, 60.f));
	FGFDepthsPlatform Z;
	Z.Key = TEXT("Z");
	Z.A = FVector2D(1300.f, -20.f);
	Z.B = FVector2D(1600.f, -20.f);
	L.Platforms.Add(Z);
	// Section 1: checkpoint 1, spikes, a patrolling shade.
	L.Solids.Add(Box(1700.f, -200.f, 3600.f, 0.f));
	L.Checkpoints.Add(FVector2D(1900.f, 60.f));
	L.Hazards.Add(Box(2300.f, 0.f, 2400.f, 30.f));
	L.Pickups.Add(FVector2D(2100.f, 60.f));
	L.Pickups.Add(FVector2D(2800.f, 60.f));
	FGFDepthsEnemyDef E1;
	E1.X1 = 2600.f; E1.X2 = 3200.f; E1.Z = 60.f; E1.Section = 1;
	L.Enemies.Add(E1);
	// The X lift up the cliff (its wall doubles as a wall-jump practice wall).
	L.Solids.Add(Box(3600.f, -200.f, 5000.f, 400.f));
	FGFDepthsPlatform X;
	X.Key = TEXT("X");
	X.A = FVector2D(3480.f, -20.f);
	X.B = FVector2D(3480.f, 380.f);
	X.HalfSize = FVector2D(80.f, 20.f);
	L.Platforms.Add(X);
	// Section 2: checkpoint 2 on the cliff top, another shade, the C bridge, the exit lift.
	L.Checkpoints.Add(FVector2D(3800.f, 460.f));
	L.Pickups.Add(FVector2D(4200.f, 460.f));
	FGFDepthsEnemyDef E2;
	E2.X1 = 4300.f; E2.X2 = 4900.f; E2.Z = 460.f; E2.Section = 2; E2.BaseHP = 3;
	L.Enemies.Add(E2);
	FGFDepthsPlatform C;
	C.Key = TEXT("C");
	C.A = FVector2D(5100.f, 380.f);
	C.B = FVector2D(5400.f, 380.f);
	L.Platforms.Add(C);
	L.Solids.Add(Box(5500.f, -200.f, 6200.f, 400.f));
	L.Solids.Add(Box(6200.f, -200.f, 6300.f, 1500.f)); // right wall
	L.Pickups.Add(FVector2D(5800.f, 460.f));
	L.Exit = FVector2D(6000.f, 460.f);
	if (L.Depth >= 3)
	{
		L.Hazards.Add(Box(4550.f, 400.f, 4620.f, 430.f));
	}
	return L;
}

const FGFDepthsLevel& FGFDepthsLevel::Get(int32 Depth)
{
	static const FGFDepthsLevel Levels[MaxDepth] = { MakeGreybox(1), MakeGreybox(2), MakeGreybox(3) };
	return Levels[FMath::Clamp(Depth, 1, MaxDepth) - 1];
}

// ---------------------------------------------------------------- platforms

const FName FGFDepthsPlatforms::Keys[3] = { FName(TEXT("Z")), FName(TEXT("X")), FName(TEXT("C")) };

bool FGFDepthsPlatforms::IsKey(FName Key)
{
	return Key == Keys[0] || Key == Keys[1] || Key == Keys[2];
}

const FGFDepthsSwitch* FGFDepthsPlatforms::Find(const TArray<FGFDepthsSwitch>& Switches, FName Key)
{
	return Switches.FindByPredicate([&](const FGFDepthsSwitch& S) { return S.Key == Key; });
}

double FGFDepthsPlatforms::Alpha(const FGFDepthsSwitch* S, double Now)
{
	if (!S)
	{
		return 0.0;
	}
	const double T = FMath::Clamp((Now - S->ChangedAt) / TravelSeconds, 0.0, 1.0);
	return S->bAtB ? T : 1.0 - T;
}

bool FGFDepthsPlatforms::Press(TArray<FGFDepthsSwitch>& Switches, FName Key, double Now, const FString& By)
{
	if (!IsKey(Key))
	{
		return false;
	}
	FGFDepthsSwitch* S = Switches.FindByPredicate([&](const FGFDepthsSwitch& X) { return X.Key == Key; });
	if (!S)
	{
		S = &Switches.AddDefaulted_GetRef();
		S->Key = Key;
	}
	if (Now - S->PressedAt < SwitchCooldown)
	{
		return false;
	}
	// Turn around from wherever it is: pick ChangedAt so the new travel starts at the current alpha.
	const double A = Alpha(S, Now);
	S->bAtB = !S->bAtB;
	const double Done = S->bAtB ? A : 1.0 - A; // fraction of the new trip already covered
	S->ChangedAt = Now - Done * TravelSeconds;
	S->PressedAt = Now;
	S->LastBy = By;
	return true;
}

FVector2D FGFDepthsPlatforms::PositionAt(const FGFDepthsPlatform& P, const TArray<FGFDepthsSwitch>& Switches, double Now)
{
	const double A = Alpha(Find(Switches, P.Key), Now);
	return P.A + (P.B - P.A) * static_cast<float>(A);
}

FBox2D FGFDepthsPlatforms::BoxAt(const FGFDepthsPlatform& P, const TArray<FGFDepthsSwitch>& Switches, double Now)
{
	const FVector2D C = PositionAt(P, Switches, Now);
	return FBox2D(C - P.HalfSize, C + P.HalfSize);
}

// ---------------------------------------------------------------- mover

FBox2D FGFDepthsMover::BodyBox(const FGFDepthsMoveParams& P, const FVector2D& Pos)
{
	return FBox2D(Pos - FVector2D(P.HalfWidth, P.HalfHeight), Pos + FVector2D(P.HalfWidth, P.HalfHeight));
}

bool FGFDepthsMover::Knockback(const FGFDepthsMoveParams& P, FGFDepthsBody& B, float FromX)
{
	if (B.InvulnLeft > 0.f)
	{
		return false;
	}
	const float Dir = B.Pos.X >= FromX ? 1.f : -1.f;
	B.Vel = FVector2D(Dir * P.KnockbackX, P.KnockbackZ);
	B.LockLeft = P.KnockbackLock;
	B.InvulnLeft = P.Invulnerable;
	B.DashLeft = 0.f;
	B.bGrounded = false;
	B.OnPlatform = INDEX_NONE;
	B.JumpsUsed = FMath::Max(B.JumpsUsed, 1);
	return true;
}

void FGFDepthsMover::Step(const FGFDepthsMoveParams& P, const FGFDepthsLevel& L, const TArray<FGFDepthsSwitch>& Switches, double Now, const FGFDepthsInput& In, FGFDepthsBody& B, float Dt)
{
	using GFDepthsSim::Overlaps;
	// Ride the platform she stands on.
	if (B.bGrounded && L.Platforms.IsValidIndex(B.OnPlatform))
	{
		const FGFDepthsPlatform& Pl = L.Platforms[B.OnPlatform];
		B.Pos += FGFDepthsPlatforms::PositionAt(Pl, Switches, Now) - FGFDepthsPlatforms::PositionAt(Pl, Switches, Now - Dt);
	}

	B.DashCooldown = FMath::Max(0.f, B.DashCooldown - Dt);
	B.InvulnLeft = FMath::Max(0.f, B.InvulnLeft - Dt);
	B.LockLeft = FMath::Max(0.f, B.LockLeft - Dt);
	B.Buffer = FMath::Max(0.f, B.Buffer - Dt);
	if (In.bJumpPressed)
	{
		B.Buffer = P.JumpBuffer;
	}
	if (B.bGrounded)
	{
		B.Coyote = P.CoyoteTime;
		B.JumpsUsed = 0;
	}
	else
	{
		B.Coyote = FMath::Max(0.f, B.Coyote - Dt);
	}

	const float Move = FMath::Clamp(In.MoveX, -1.f, 1.f);
	if (FMath::Abs(Move) > 0.1f && B.LockLeft <= 0.f && B.DashLeft <= 0.f)
	{
		B.Facing = Move > 0.f ? 1 : -1;
	}
	if (In.bDashPressed && B.DashCooldown <= 0.f && B.DashLeft <= 0.f && B.LockLeft <= 0.f)
	{
		B.DashLeft = P.DashTime;
		B.DashDir = B.Facing;
		B.DashCooldown = P.DashCooldown;
	}

	if (B.DashLeft > 1e-4f)
	{
		B.Vel = FVector2D(B.DashDir * P.DashDistance / P.DashTime, B.bGrounded ? -1.f : 0.f);
		B.DashLeft -= Dt;
		if (B.DashLeft <= 1e-4f)
		{
			B.DashLeft = 0.f;
		}
	}
	else
	{
		if (FMath::Abs(B.Vel.X) > P.RunSpeed && B.LockLeft <= 0.f)
		{
			B.Vel.X = FMath::Sign(B.Vel.X) * P.RunSpeed; // a dash ends at run speed
		}
		if (B.LockLeft <= 0.f)
		{
			B.Vel.X = FMath::FInterpConstantTo(B.Vel.X, Move * P.RunSpeed, Dt, B.bGrounded ? P.GroundAccel : P.AirAccel);
		}
		if (B.Buffer > 0.f)
		{
			if (B.Coyote > 0.f && B.JumpsUsed == 0)
			{
				B.Vel.Y = P.JumpSpeed;
				B.JumpsUsed = 1;
				B.Coyote = 0.f;
				B.Buffer = 0.f;
				B.bGrounded = false;
				B.OnPlatform = INDEX_NONE;
			}
			else if (P.bWallJump && B.WallDir != 0 && !B.bGrounded)
			{
				B.Vel = FVector2D(-B.WallDir * P.WallJumpX, P.WallJumpZ);
				B.Facing = -B.WallDir;
				B.LockLeft = P.WallJumpLock;
				B.Buffer = 0.f;
				B.JumpsUsed = 1;
			}
			else if (P.bDoubleJump && B.JumpsUsed < 2 && !B.bGrounded)
			{
				B.Vel.Y = P.JumpSpeed * 0.9f;
				B.JumpsUsed = 2;
				B.Buffer = 0.f;
			}
		}
		if (!In.bJumpHeld && B.JumpsUsed > 0 && B.LockLeft <= 0.f && B.Vel.Y > P.JumpCutSpeed)
		{
			B.Vel.Y = P.JumpCutSpeed;
		}
		B.Vel.Y -= P.Gravity * Dt;
		if (B.WallDir != 0 && Move * B.WallDir > 0.f && B.Vel.Y < -P.WallSlideSpeed)
		{
			B.Vel.Y = -P.WallSlideSpeed;
		}
		B.Vel.Y = FMath::Max(B.Vel.Y, -P.MaxFall);
	}

	// Collision: static solids plus platforms where they are now.
	TArray<FBox2D, TInlineAllocator<16>> Boxes;
	TArray<int32, TInlineAllocator<16>> PlatformOf;
	for (const FBox2D& S : L.Solids)
	{
		Boxes.Add(S);
		PlatformOf.Add(INDEX_NONE);
	}
	for (int32 i = 0; i < L.Platforms.Num(); ++i)
	{
		Boxes.Add(FGFDepthsPlatforms::BoxAt(L.Platforms[i], Switches, Now));
		PlatformOf.Add(i);
	}
	constexpr float Eps = 0.01f;

	B.Pos.X += B.Vel.X * Dt;
	for (const FBox2D& S : Boxes)
	{
		const FBox2D Me = BodyBox(P, B.Pos);
		if (Overlaps(Me, S))
		{
			if (B.Vel.X > 0.f)
			{
				B.Pos.X = S.Min.X - P.HalfWidth - Eps;
			}
			else if (B.Vel.X < 0.f)
			{
				B.Pos.X = S.Max.X + P.HalfWidth + Eps;
			}
			else
			{
				B.Pos.X = (B.Pos.X < S.GetCenter().X) ? S.Min.X - P.HalfWidth - Eps : S.Max.X + P.HalfWidth + Eps;
			}
			B.Vel.X = 0.f;
		}
	}

	B.Pos.Y += B.Vel.Y * Dt;
	B.bGrounded = false;
	B.OnPlatform = INDEX_NONE;
	for (int32 i = 0; i < Boxes.Num(); ++i)
	{
		const FBox2D Me = BodyBox(P, B.Pos);
		if (Overlaps(Me, Boxes[i]))
		{
			if (B.Vel.Y <= 0.f)
			{
				B.Pos.Y = Boxes[i].Max.Y + P.HalfHeight;
				B.bGrounded = true;
				B.OnPlatform = PlatformOf[i];
			}
			else
			{
				B.Pos.Y = Boxes[i].Min.Y - P.HalfHeight - Eps;
			}
			B.Vel.Y = 0.f;
		}
	}
	if (B.bGrounded)
	{
		B.JumpsUsed = 0;
		B.Coyote = P.CoyoteTime;
		// Feet level with a platform's top (e.g. a lift resting flush with the floor): ride the platform.
		const FBox2D Me = BodyBox(P, B.Pos);
		for (int32 i = 0; i < Boxes.Num(); ++i)
		{
			if (PlatformOf[i] != INDEX_NONE && FMath::Abs(Boxes[i].Max.Y - Me.Min.Y) < 0.5f && Me.Min.X < Boxes[i].Max.X && Me.Max.X > Boxes[i].Min.X)
			{
				B.OnPlatform = PlatformOf[i];
			}
		}
	}

	// Walls she can slide on and kick off.
	B.WallDir = 0;
	if (P.bWallJump && !B.bGrounded)
	{
		for (const int32 Dir : { -1, 1 })
		{
			const FBox2D Probe = BodyBox(P, B.Pos + FVector2D(Dir * 2.f, 0.f));
			for (int32 i = 0; i < Boxes.Num(); ++i)
			{
				if (PlatformOf[i] == INDEX_NONE && Overlaps(Probe, Boxes[i]))
				{
					B.WallDir = Dir;
				}
			}
		}
	}
}

// ---------------------------------------------------------------- rules

bool FGFDepthsRules::Damage(FGFDepthsRunState& S, int32 Amount)
{
	if (S.bDowned || S.HP <= 0 || Amount <= 0)
	{
		return false;
	}
	S.HP = FMath::Max(0, S.HP - Amount);
	return S.HP == 0;
}

bool FGFDepthsRules::Heal(FGFDepthsRunState& S)
{
	if (S.bDowned || S.Heals <= 0 || S.HP >= FGFDepthsRunState::MaxHP)
	{
		return false;
	}
	S.HP = FMath::Min(FGFDepthsRunState::MaxHP, S.HP + FGFDepthsRunState::HealAmount);
	--S.Heals;
	return true;
}

bool FGFDepthsRules::ReachCheckpoint(FGFDepthsRunState& S, int32 Index, TArray<FGFItemStack>& OutBank)
{
	if (Index <= S.Checkpoint || Index < 0 || Index > 1 || S.bDowned)
	{
		return false;
	}
	S.Checkpoint = Index;
	S.Section = Index + 1;
	S.Heals = FGFDepthsRunState::HealsPerSection;
	OutBank = MoveTemp(S.Unbanked);
	S.Unbanked.Reset();
	return true;
}

TArray<FGFItemStack> FGFDepthsRules::Die(FGFDepthsRunState& S)
{
	TArray<FGFItemStack> Lost;
	for (int32 i = S.Unbanked.Num() - 1; i >= 0; --i)
	{
		FGFItemStack& St = S.Unbanked[i];
		const int32 Lose = St.Count / 2;
		if (Lose > 0)
		{
			FGFRewards::AddTo(Lost, St.ItemId, Lose);
			St.Count -= Lose;
		}
		if (St.Count <= 0)
		{
			S.Unbanked.RemoveAt(i);
		}
	}
	S.HP = FGFDepthsRunState::MaxHP;
	S.Heals = FGFDepthsRunState::HealsPerSection;
	S.bDowned = false;
	S.DownedLeft = 0.f;
	++S.Deaths;
	return Lost;
}

TArray<FGFItemStack> FGFDepthsRules::LiftHome(FGFDepthsRunState& S)
{
	TArray<FGFItemStack> Out = MoveTemp(S.Unbanked);
	S.Unbanked.Reset();
	return Out;
}

int32 FGFDepthsRules::EnemyHP(int32 BaseHP, int32 Depth, int32 Players)
{
	const float Scale = (1.f + 0.5f * (FMath::Max(1, Depth) - 1)) * (1.f + 0.25f * (FMath::Max(1, Players) - 1));
	return FMath::Max(1, FMath::RoundToInt(BaseHP * Scale));
}

const FGFDepthsRewardRow* FGFDepthsRules::RewardRow(int32 Depth)
{
	const UDataTable* T = FGFDataTables::Get<FGFDepthsRewardRow>(TEXT("DT_DepthsRewards"));
	if (!T)
	{
		return nullptr;
	}
	const FName Row(*FString::Printf(TEXT("Depth%d"), FMath::Clamp(Depth, 1, FGFDepthsLevel::MaxDepth)));
	return T->FindRow<FGFDepthsRewardRow>(Row, TEXT("Depths"), false);
}

void FGFDepthsRules::RollLoot(int32 Depth, FName Kind, FRandomStream& Rng, TArray<FGFItemStack>& Out)
{
	const FGFDepthsRewardRow* Row = RewardRow(Depth);
	if (!Row)
	{
		return;
	}
	const TArray<FGFRewardEntry>& E = Kind == TEXT("Enemy") ? Row->Enemy : Kind == TEXT("Exit") ? Row->Exit : Row->Pickup;
	FGFRewards::Roll(E, Rng, 1.f, Out);
}
