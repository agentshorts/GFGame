// Hallow Night (PLAN.md M7): the custom 2.5D controls (run, coyote time, jump buffer, double jump from depth 2,
// dash, wall jump, knockback), the shared Z/X/C platforms as a function of server time, the run rules
// (5 HP, 3 heals per section, 2 checkpoints, half of unbanked loot lost on death, Lift Home) and the
// tier-4 minerals that only drop there. All pure (no world).
#include "Misc/AutomationTest.h"
#include "GFDepthsTypes.h"
#include "GFArenaTypes.h"
#include "GFRewards.h"
#include "GFItemTypes.h"
#include "GFDataTables.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFDepthsTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFDepthsTestUtil
{
	static const float Dt = FGFDepthsMover::StepSeconds;

	static FGFDepthsLevel Flat(float X1 = -2000.f, float X2 = 2000.f)
	{
		FGFDepthsLevel L;
		L.Solids.Add(FBox2D(FVector2D(X1, -100.f), FVector2D(X2, 0.f)));
		return L;
	}

	static FGFDepthsBody Standing(float X, float FloorZ = 0.f)
	{
		FGFDepthsBody B;
		B.Pos = FVector2D(X, FloorZ + 60.f);
		return B;
	}

	static FGFDepthsInput In(float Move, bool bJumpPressed = false, bool bJumpHeld = false, bool bDash = false)
	{
		FGFDepthsInput I;
		I.MoveX = Move;
		I.bJumpPressed = bJumpPressed;
		I.bJumpHeld = bJumpHeld || bJumpPressed;
		I.bDashPressed = bDash;
		return I;
	}

	static void Steps(const FGFDepthsMoveParams& P, const FGFDepthsLevel& L, FGFDepthsBody& B, const FGFDepthsInput& I, int32 N, double& Now, const TArray<FGFDepthsSwitch>& Sw = TArray<FGFDepthsSwitch>())
	{
		for (int32 i = 0; i < N; ++i)
		{
			Now += Dt;
			FGFDepthsMover::Step(P, L, Sw, Now, I, B, Dt);
		}
	}

	static const UDataTable* Items() { return FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items")); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDepthsRunTest, "GFGame.Depths.Move.Run", GFDepthsTestFlags)
bool FGFDepthsRunTest::RunTest(const FString&)
{
	using namespace GFDepthsTestUtil;
	const FGFDepthsMoveParams P = FGFDepthsMoveParams::ForDepth(1);
	const FGFDepthsLevel L = Flat();
	FGFDepthsBody B = Standing(0.f);
	double Now = 0.0;
	Steps(P, L, B, In(0.f), 2, Now);
	TestTrue(TEXT("Lands on the floor"), B.bGrounded && FMath::IsNearlyEqual(B.Pos.Y, 60.f, 0.1f));
	Steps(P, L, B, In(1.f), 6, Now);
	TestTrue(TEXT("Tight: full run speed within 0.1 s"), FMath::IsNearlyEqual(B.Vel.X, 500.f, 1.f));
	const float X0 = B.Pos.X;
	Steps(P, L, B, In(1.f), 60, Now);
	TestTrue(TEXT("500 cm/s"), FMath::IsNearlyEqual(B.Pos.X - X0, 500.f, 5.f));
	Steps(P, L, B, In(0.f), 6, Now);
	TestTrue(TEXT("Stops within 0.1 s"), FMath::IsNearlyZero(B.Vel.X));
	Steps(P, L, B, In(-1.f), 1, Now);
	TestEqual(TEXT("Turns to face left"), B.Facing, -1);
	TestTrue(TEXT("Stays on the side plane (Z only changes by jumping)"), FMath::IsNearlyEqual(B.Pos.Y, 60.f, 0.1f));
	// A held jump goes higher than a tapped one.
	FGFDepthsBody Held = Standing(0.f), Tap = Standing(0.f);
	double T1 = 0.0, T2 = 0.0;
	Steps(P, L, Held, In(0.f), 2, T1);
	Steps(P, L, Tap, In(0.f), 2, T2);
	float TopHeld = 0.f, TopTap = 0.f;
	Steps(P, L, Held, In(0.f, true, true), 1, T1);
	Steps(P, L, Tap, In(0.f, true, true), 1, T2);
	for (int32 i = 0; i < 60; ++i)
	{
		Steps(P, L, Held, In(0.f, false, true), 1, T1);
		Steps(P, L, Tap, In(0.f, false, false), 1, T2);
		TopHeld = FMath::Max(TopHeld, Held.Pos.Y);
		TopTap = FMath::Max(TopTap, Tap.Pos.Y);
	}
	AddInfo(FString::Printf(TEXT("Jump height held %.0f cm, tapped %.0f cm"), TopHeld - 60.f, TopTap - 60.f));
	TestTrue(TEXT("Held jump is ~1.8 m"), TopHeld - 60.f > 170.f && TopHeld - 60.f < 200.f);
	TestTrue(TEXT("Tapped jump is lower"), TopTap < TopHeld - 60.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDepthsCoyoteTest, "GFGame.Depths.Move.CoyoteAndBuffer", GFDepthsTestFlags)
bool FGFDepthsCoyoteTest::RunTest(const FString&)
{
	using namespace GFDepthsTestUtil;
	const FGFDepthsMoveParams P = FGFDepthsMoveParams::ForDepth(1);
	const FGFDepthsLevel L = Flat(-2000.f, 0.f); // a ledge at X = 0
	// Coyote time: a jump just after running off the ledge still counts; a late one doesn't.
	auto JumpAfterLedge = [&](int32 LateSteps) -> bool
	{
		FGFDepthsBody B = Standing(-200.f);
		double Now = 0.0;
		Steps(P, L, B, In(0.f), 2, Now);
		int32 Guard = 0;
		while (B.bGrounded && Guard++ < 200)
		{
			Steps(P, L, B, In(1.f), 1, Now);
		}
		Steps(P, L, B, In(1.f), LateSteps, Now);
		Steps(P, L, B, In(1.f, true, true), 1, Now);
		return B.Vel.Y > 0.f;
	};
	TestTrue(TEXT("Jump 50 ms after the ledge works (coyote 80 ms)"), JumpAfterLedge(2));
	TestFalse(TEXT("Jump 120 ms after the ledge falls"), JumpAfterLedge(7));

	// Jump buffer: pressing just before landing jumps on landing; pressing too early doesn't.
	const FGFDepthsLevel Floor = Flat();
	auto LandingStep = [&]() -> int32
	{
		FGFDepthsBody B;
		B.Pos = FVector2D(0.f, 400.f);
		double Now = 0.0;
		for (int32 i = 1; i < 200; ++i)
		{
			Steps(P, Floor, B, In(0.f), 1, Now);
			if (B.bGrounded)
			{
				return i;
			}
		}
		return -1;
	};
	const int32 N = LandingStep();
	TestTrue(TEXT("Falls and lands"), N > 10);
	auto BufferedJump = [&](int32 PressStep) -> bool
	{
		FGFDepthsBody B;
		B.Pos = FVector2D(0.f, 400.f);
		double Now = 0.0;
		for (int32 i = 1; i <= N + 3; ++i)
		{
			Steps(P, Floor, B, In(0.f, i == PressStep, i >= PressStep), 1, Now);
			if (i > N && B.Vel.Y > 0.f)
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("Pressed 50 ms before landing: jumps on landing"), BufferedJump(N - 3));
	TestFalse(TEXT("Pressed 150 ms before landing: no jump"), BufferedJump(N - 9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDepthsAirTest, "GFGame.Depths.Move.DoubleJumpDashWall", GFDepthsTestFlags)
bool FGFDepthsAirTest::RunTest(const FString&)
{
	using namespace GFDepthsTestUtil;
	const FGFDepthsLevel L = Flat();
	// Double jump only from depth 2.
	for (const int32 D : { 1, 2 })
	{
		const FGFDepthsMoveParams P = FGFDepthsMoveParams::ForDepth(D);
		FGFDepthsBody B = Standing(0.f);
		double Now = 0.0;
		Steps(P, L, B, In(0.f), 2, Now);
		Steps(P, L, B, In(0.f, true, true), 1, Now);
		Steps(P, L, B, In(0.f, false, true), 30, Now);
		const float Before = B.Vel.Y;
		Steps(P, L, B, In(0.f, true, true), 1, Now);
		if (D == 1)
		{
			TestTrue(TEXT("Depth 1: no double jump"), B.Vel.Y <= Before);
		}
		else
		{
			TestTrue(TEXT("Depth 2: double jump"), B.Vel.Y > 500.f);
			Steps(P, L, B, In(0.f, false, true), 5, Now);
			const float V = B.Vel.Y;
			Steps(P, L, B, In(0.f, true, true), 1, Now);
			TestTrue(TEXT("Only one air jump"), B.Vel.Y <= V);
		}
	}

	// Dash: 300 cm, 0.6 s cooldown.
	{
		const FGFDepthsMoveParams P = FGFDepthsMoveParams::ForDepth(1);
		FGFDepthsBody B = Standing(0.f);
		double Now = 0.0;
		Steps(P, L, B, In(0.f), 2, Now);
		const float X0 = B.Pos.X;
		Steps(P, L, B, In(0.f, false, false, true), 1, Now);
		int32 Guard = 0;
		while (B.DashLeft > 0.f && Guard++ < 30)
		{
			Steps(P, L, B, In(0.f), 1, Now);
		}
		AddInfo(FString::Printf(TEXT("Dash covered %.1f cm"), B.Pos.X - X0));
		TestTrue(TEXT("Dash is ~300 cm"), FMath::Abs(B.Pos.X - X0 - 300.f) <= 25.f);
		const float X1 = B.Pos.X;
		Steps(P, L, B, In(0.f, false, false, true), 1, Now);
		TestTrue(TEXT("No second dash inside the cooldown"), B.DashLeft <= 0.f && B.Pos.X - X1 < 20.f);
		Steps(P, L, B, In(0.f), 36, Now);
		Steps(P, L, B, In(0.f, false, false, true), 1, Now);
		TestTrue(TEXT("Dash again after 0.6 s"), B.DashLeft > 0.f);
	}

	// Wall slide and wall jump.
	{
		const FGFDepthsMoveParams P = FGFDepthsMoveParams::ForDepth(1);
		FGFDepthsLevel W = Flat();
		W.Solids.Add(FBox2D(FVector2D(100.f, 0.f), FVector2D(200.f, 3000.f)));
		FGFDepthsBody B;
		B.Pos = FVector2D(50.f, 1500.f);
		double Now = 0.0;
		Steps(P, W, B, In(1.f), 40, Now);
		TestEqual(TEXT("Touching the wall on the right"), B.WallDir, 1);
		TestTrue(TEXT("Wall slide caps the fall"), B.Vel.Y >= -P.WallSlideSpeed - 1.f && B.Vel.Y < 0.f);
		Steps(P, W, B, In(1.f, true, true), 1, Now);
		TestTrue(TEXT("Wall jump kicks away and up"), B.Vel.X < 0.f && B.Vel.Y > 500.f);
		TestEqual(TEXT("Faces away from the wall"), B.Facing, -1);
	}

	// Knockback with 0.8 s of invulnerability.
	{
		const FGFDepthsMoveParams P = FGFDepthsMoveParams::ForDepth(1);
		FGFDepthsBody B = Standing(0.f);
		double Now = 0.0;
		Steps(P, L, B, In(0.f), 2, Now);
		TestTrue(TEXT("Knocked back"), FGFDepthsMover::Knockback(P, B, -10.f) && B.Vel.X > 0.f && B.Vel.Y > 0.f);
		Steps(P, L, B, In(0.f), 40, Now);
		TestFalse(TEXT("Invulnerable for 0.8 s"), FGFDepthsMover::Knockback(P, B, -10.f));
		Steps(P, L, B, In(0.f), 10, Now);
		TestTrue(TEXT("Hittable again after"), FGFDepthsMover::Knockback(P, B, -10.f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDepthsPlatformTest, "GFGame.Depths.Platforms", GFDepthsTestFlags)
bool FGFDepthsPlatformTest::RunTest(const FString&)
{
	using namespace GFDepthsTestUtil;
	const FGFDepthsLevel& L = FGFDepthsLevel::Get(1);
	TestEqual(TEXT("Three shared platforms: Z, X, C"), L.Platforms.Num(), 3);
	const FGFDepthsPlatform& Z = L.Platforms[0];
	TArray<FGFDepthsSwitch> Sw;
	TestTrue(TEXT("Rests at A"), FGFDepthsPlatforms::PositionAt(Z, Sw, 5.0).Equals(Z.A));
	TestTrue(TEXT("Anyone presses Z"), FGFDepthsPlatforms::Press(Sw, TEXT("Z"), 10.0, TEXT("Owner")));
	TestFalse(TEXT("0.4 s cooldown per key"), FGFDepthsPlatforms::Press(Sw, TEXT("Z"), 10.2, TEXT("Friend")));
	TestFalse(TEXT("Only Z, X and C"), FGFDepthsPlatforms::Press(Sw, TEXT("Q"), 10.2, TEXT("Friend")));
	TestTrue(TEXT("Other keys have their own cooldown"), FGFDepthsPlatforms::Press(Sw, TEXT("C"), 10.2, TEXT("Friend")));
	const FVector2D Mid = FGFDepthsPlatforms::PositionAt(Z, Sw, 10.5);
	TestTrue(TEXT("Halfway after 0.5 s"), Mid.Equals((Z.A + Z.B) * 0.5f, 0.5f));
	TestTrue(TEXT("At B after 1 s"), FGFDepthsPlatforms::PositionAt(Z, Sw, 11.0).Equals(Z.B, 0.5f));
	// Same switch state + same server time = same place on every machine.
	const TArray<FGFDepthsSwitch> Copy = Sw;
	TestTrue(TEXT("Deterministic from server time"), FGFDepthsPlatforms::PositionAt(Z, Copy, 10.73).Equals(FGFDepthsPlatforms::PositionAt(Z, Sw, 10.73)));
	// Worked again mid-travel: turns around from where it is (no jump).
	TArray<FGFDepthsSwitch> Rev = Sw;
	TestTrue(TEXT("Friend turns it around"), FGFDepthsPlatforms::Press(Rev, TEXT("Z"), 10.5, TEXT("Friend")));
	TestTrue(TEXT("No jump when turned around"), FGFDepthsPlatforms::PositionAt(Z, Rev, 10.5).Equals(Mid, 0.5f));
	TestTrue(TEXT("Back at A 0.5 s later"), FGFDepthsPlatforms::PositionAt(Z, Rev, 11.0).Equals(Z.A, 0.5f));
	TestEqual(TEXT("Last worked by"), FGFDepthsPlatforms::Find(Rev, TEXT("Z"))->LastBy, FString(TEXT("Friend")));

	// Riding the X lift up the cliff, then walking off onto the top.
	const FGFDepthsMoveParams P = FGFDepthsMoveParams::ForDepth(1);
	const FGFDepthsPlatform& X = L.Platforms[1];
	TArray<FGFDepthsSwitch> Lift;
	FGFDepthsBody B = Standing(X.A.X, 0.f);
	double Now = 20.0;
	Steps(P, L, B, In(0.f), 3, Now, Lift);
	TestEqual(TEXT("Standing on the lift"), B.OnPlatform, 1);
	FGFDepthsPlatforms::Press(Lift, TEXT("X"), Now, TEXT("Owner"));
	Steps(P, L, B, In(0.f), 75, Now, Lift);
	TestTrue(TEXT("Carried to the top"), FMath::IsNearlyEqual(B.Pos.Y, X.B.Y + X.HalfSize.Y + 60.f, 2.f) && B.bGrounded);
	Steps(P, L, B, In(1.f), 60, Now, Lift);
	TestTrue(TEXT("Walks onto the cliff top"), B.Pos.X > 3700.f && B.bGrounded && FMath::IsNearlyEqual(B.Pos.Y, 460.f, 2.f));

	// Section 0 is passable: run, hop onto the Z platform and jump the gap.
	FGFDepthsBody Run = Standing(L.Entrance.X);
	TArray<FGFDepthsSwitch> None;
	Now = 0.0;
	bool bJumped = false;
	for (int32 i = 0; i < 600 && Run.Pos.X < 1850.f; ++i)
	{
		const bool bPress = !bJumped && Run.Pos.X > 1340.f && Run.bGrounded;
		bJumped |= bPress;
		Steps(P, L, Run, In(1.f, bPress, true), 1, Now, None);
	}
	TestTrue(TEXT("Section 0 can be crossed"), Run.Pos.X >= 1850.f && Run.Pos.Y > 0.f);
	TestEqual(TEXT("Checkpoint 1 starts section 1"), L.SectionAt(1950.f), 1);
	TestEqual(TEXT("Two checkpoints per depth"), L.Checkpoints.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDepthsRulesTest, "GFGame.Depths.Rules", GFDepthsTestFlags)
bool FGFDepthsRulesTest::RunTest(const FString&)
{
	FGFDepthsRunState S;
	TestEqual(TEXT("5 HP"), S.HP, 5);
	TestEqual(TEXT("3 heals"), S.Heals, 3);
	TestFalse(TEXT("No heal at full HP"), FGFDepthsRules::Heal(S));
	FGFDepthsRules::Damage(S, 4);
	TestTrue(TEXT("Heal 1"), FGFDepthsRules::Heal(S) && S.HP == 3);
	TestTrue(TEXT("Heal 2"), FGFDepthsRules::Heal(S) && S.HP == 5);
	FGFDepthsRules::Damage(S, 1);
	TestTrue(TEXT("Heal 3 caps at 5"), FGFDepthsRules::Heal(S) && S.HP == 5);
	FGFDepthsRules::Damage(S, 1);
	TestFalse(TEXT("Only 3 heals per section"), FGFDepthsRules::Heal(S));

	S.Unbanked = { FGFItemStack(TEXT("Moonstone"), 2), FGFItemStack(TEXT("Stone"), 7) };
	TArray<FGFItemStack> Bank;
	TestTrue(TEXT("Checkpoint 1"), FGFDepthsRules::ReachCheckpoint(S, 0, Bank));
	TestEqual(TEXT("Checkpoint banks the loot"), FGFRewards::CountOf(Bank, TEXT("Stone")), 7);
	TestEqual(TEXT("Nothing left unbanked"), S.Unbanked.Num(), 0);
	TestEqual(TEXT("New section: 3 fresh heals"), S.Heals, 3);
	TestEqual(TEXT("Section 1"), S.Section, 1);
	TestFalse(TEXT("Same checkpoint twice banks nothing"), FGFDepthsRules::ReachCheckpoint(S, 0, Bank));
	TestFalse(TEXT("Only 2 checkpoints"), FGFDepthsRules::ReachCheckpoint(S, 2, Bank));

	S.Unbanked = { FGFItemStack(TEXT("Moonstone"), 5), FGFItemStack(TEXT("Stone"), 1), FGFItemStack(TEXT("Crystal"), 4) };
	TestTrue(TEXT("5 damage takes them down"), FGFDepthsRules::Damage(S, 9));
	const TArray<FGFItemStack> Lost = FGFDepthsRules::Die(S);
	TestEqual(TEXT("Loses floor(half): Moonstone 2"), FGFRewards::CountOf(Lost, TEXT("Moonstone")), 2);
	TestEqual(TEXT("Loses floor(half): Crystal 2"), FGFRewards::CountOf(Lost, TEXT("Crystal")), 2);
	TestEqual(TEXT("A single Stone is kept"), FGFRewards::CountOf(Lost, TEXT("Stone")), 0);
	TestEqual(TEXT("Keeps Moonstone 3"), FGFRewards::CountOf(S.Unbanked, TEXT("Moonstone")), 3);
	TestEqual(TEXT("Keeps Stone 1"), FGFRewards::CountOf(S.Unbanked, TEXT("Stone")), 1);
	TestTrue(TEXT("Back to 5 HP and 3 heals"), S.HP == 5 && S.Heals == 3 && S.Deaths == 1);
	TestEqual(TEXT("Respawns at their checkpoint"), S.Checkpoint, 0);
	const TArray<FGFItemStack> Home = FGFDepthsRules::LiftHome(S);
	TestEqual(TEXT("Lift Home banks everything"), FGFRewards::CountOf(Home, TEXT("Moonstone")) + FGFRewards::CountOf(Home, TEXT("Crystal")) + FGFRewards::CountOf(Home, TEXT("Stone")), 6);
	TestEqual(TEXT("Nothing unbanked after the lift"), S.Unbanked.Num(), 0);

	TestEqual(TEXT("Shade HP depth 1 solo"), FGFDepthsRules::EnemyHP(2, 1, 1), 2);
	TestEqual(TEXT("Shade HP depth 2 solo"), FGFDepthsRules::EnemyHP(2, 2, 1), 3);
	TestEqual(TEXT("Shade HP depth 3, four players"), FGFDepthsRules::EnemyHP(2, 3, 4), 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDepthsRewardsTest, "GFGame.Depths.Rewards", GFDepthsTestFlags)
bool FGFDepthsRewardsTest::RunTest(const FString&)
{
	using namespace GFDepthsTestUtil;
	const UDataTable* ItemsT = Items();
	if (!TestNotNull(TEXT("DT_Items"), ItemsT))
	{
		return false;
	}
	for (int32 D = 1; D <= FGFDepthsLevel::MaxDepth; ++D)
	{
		const FGFDepthsRewardRow* Row = FGFDepthsRules::RewardRow(D);
		if (!TestNotNull(*FString::Printf(TEXT("DT_DepthsRewards depth %d"), D), Row))
		{
			return false;
		}
		for (const TArray<FGFRewardEntry>* E : { &Row->Pickup, &Row->Enemy, &Row->Exit })
		{
			for (const FGFRewardEntry& R : *E)
			{
				const FGFItemRow* I = ItemsT->FindRow<FGFItemRow>(R.ItemId, TEXT("Depths"), false);
				TestTrue(*FString::Printf(TEXT("%s is a real item"), *R.ItemId.ToString()), I != nullptr);
				TestTrue(*FString::Printf(TEXT("%s is a material, not money"), *R.ItemId.ToString()), I && I->Category == EGFItemCategory::Material);
			}
		}
		TestTrue(*FString::Printf(TEXT("Depth %d drops tier-4 minerals"), D), FGFRewards::ContainsExclusive(ItemsT, Row->Exit, EGFExclusive::HallowNight));
	}
	// Every Hallow Night exclusive drops somewhere in the Depths...
	TArray<FGFItemStack> All;
	FRandomStream Rng(77);
	for (int32 i = 0; i < 3000; ++i)
	{
		FGFDepthsRules::RollLoot(1 + i % 3, i % 2 ? TEXT("Pickup") : TEXT("Exit"), Rng, All);
	}
	for (const TCHAR* Id : { TEXT("Moonstone"), TEXT("Starglass"), TEXT("Gloomsilk"), TEXT("EmberCore") })
	{
		TestTrue(*FString::Printf(TEXT("%s drops in the Depths"), Id), FGFRewards::CountOf(All, Id) > 0);
	}
	// ...and nowhere else.
	if (const UDataTable* Arena = FGFDataTables::Get<FGFArenaRewardRow>(TEXT("DT_ArenaRewards")))
	{
		Arena->ForeachRow<FGFArenaRewardRow>(TEXT("Depths"), [&](const FName& Name, const FGFArenaRewardRow& Row)
		{
			TestFalse(*FString::Printf(TEXT("Arena %s has no Hallow Night minerals"), *Name.ToString()), FGFRewards::ContainsExclusive(ItemsT, Row.Rolls, EGFExclusive::HallowNight));
		});
	}
	return true;
}

#endif
