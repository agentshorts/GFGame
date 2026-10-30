// Big Grumble (giant dino event) rules and sim, no world: trigger math, safe territory (through IGFSafeZoneQuery:
// one or several growing regions, or any other shape), target choice, herding, never entering safety, no damage, chat by phase and cadence, {Target} names, the leave timer, the slowdown,
// eating attackers (only one, never a bystander) and eating a target who stands still.
#include "GFGiantTypes.h"
#include "GFModeTypes.h"
#include "GFDataTables.h"
#include "GFLineTokens.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFGiantTest
{
	static FGFGiantPlayerView Player(const TCHAR* Key, const FVector& At, double Play = 0.0, int64 FirstSeen = 0)
	{
		FGFGiantPlayerView P;
		P.Key = Key;
		P.Name = FString(Key) + TEXT("Name");
		P.Location = At;
		P.PlaySeconds = Play;
		P.FirstSeenTicks = FirstSeen;
		P.RunSpeed = 600.f;
		return P;
	}

	/** Tuning for sim tests: no shoves (so CloseCall lines don't mix into cadence checks) unless a test wants them. */
	static FGFGiantTuning Quiet()
	{
		FGFGiantTuning T;
		T.ShoveRange = 0.f;
		return T;
	}

	/** Moves every player around the centre (tangentially) at Speed, keeping their distance: always moving, always outside. */
	static void Orbit(TArray<FGFGiantPlayerView>& Players, float Speed, float Dt)
	{
		for (FGFGiantPlayerView& P : Players)
		{
			const float R = FVector(P.Location.X, P.Location.Y, 0.f).Size();
			if (R < 1.f)
			{
				continue;
			}
			const float A = FMath::Atan2(P.Location.Y, P.Location.X) + Speed * Dt / R;
			P.Location = FVector(R * FMath::Cos(A), R * FMath::Sin(A), P.Location.Z);
		}
	}

	/** The default single safe region at the origin for N players. */
	static FGFSafeCircles HomeZone(const FGFGiantTuning& T, int32 N)
	{
		return FGFSafeCircles({ FGFGiantRules::DefaultRegion(T, FVector::ZeroVector, N) });
	}

	static FGFSafeRegion Region(const FVector& Center, int32 Members, float Base = 1000.f, float Per = 300.f, float Max = 2200.f)
	{
		FGFSafeRegion R;
		R.Id = *FString::Printf(TEXT("R%d_%d"), int32(Center.X), int32(Center.Y));
		R.Center = Center;
		R.Members = Members;
		R.BaseRadius = Base;
		R.RadiusPerExtraMember = Per;
		R.MaxRadius = Max;
		return R;
	}

	/** A non-circle safe zone (everything with X <= Edge), to prove the giant only uses the interface. */
	struct FHalfPlaneZone : public IGFSafeZoneQuery
	{
		float Edge = 0.f;
		virtual bool IsInsideSafe(const FVector& L) const override { return L.X <= Edge; }
		virtual FVector ClosestSafePoint(const FVector& L) const override { return L.X <= Edge ? L : FVector(Edge, L.Y, L.Z); }
		virtual bool ClosestEdge(const FVector& L, FVector& P, FVector& N) const override { P = FVector(Edge, L.Y, L.Z); N = FVector(1.f, 0.f, 0.f); return true; }
		virtual bool HasAnySafe() const override { return true; }
	};

	static int32 CountPhase(const TArray<FGFGiantChatLine>& Chat, EGFGiantLinePhase Phase)
	{
		int32 N = 0;
		for (const FGFGiantChatLine& L : Chat)
		{
			N += L.Phase == Phase ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantLinesTest, "GFGame.Giant.Lines", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantLinesTest::RunTest(const FString&)
{
	FGFDataTables::ResetCache();
	const FGFGiantLines L = FGFGiantLines::Load();
	// 72 earlier lines plus GD_Handoff_01..14. {Target} is on 7 of the earlier lines and 3 handoff lines.
	TestEqual(TEXT("86 lines"), L.Total(), 86);
	TestEqual(TEXT("Spotted"), L.Num(EGFGiantLinePhase::Spotted), 6);
	TestEqual(TEXT("Chase"), L.Num(EGFGiantLinePhase::Chase), 8);
	TestEqual(TEXT("CloseCall"), L.Num(EGFGiantLinePhase::CloseCall), 6);
	TestEqual(TEXT("Herded"), L.Num(EGFGiantLinePhase::Herded), 6);
	TestEqual(TEXT("Circling"), L.Num(EGFGiantLinePhase::Circling), 8);
	TestEqual(TEXT("Leaving"), L.Num(EGFGiantLinePhase::Leaving), 6);
	TestEqual(TEXT("Return"), L.Num(EGFGiantLinePhase::Return), 6);
	TestEqual(TEXT("Snacked"), L.Num(EGFGiantLinePhase::Snacked), 6);
	TestEqual(TEXT("Target"), L.Num(EGFGiantLinePhase::Target), 20);
	TestEqual(TEXT("Handoff"), L.Num(EGFGiantLinePhase::Handoff), 14);

	bool bNamedLine = false, bWhyRunning = false, bRumble = false, bIdsMatchPhase = true;
	TArray<FString> Tokened;
	int32 BadBraces = 0;
	for (const TPair<EGFGiantLinePhase, TArray<FGFGiantLines::FLine>>& P : L.ByPhase)
	{
		const FString PhaseName = StaticEnum<EGFGiantLinePhase>()->GetNameStringByValue(int64(P.Key));
		for (const FGFGiantLines::FLine& Line : P.Value)
		{
			bNamedLine |= Line.Text == TEXT("Where you going, {Target}? Huh? Where you going?");
			bWhyRunning |= Line.Text == TEXT("Why you running, {Target}?");
			bRumble |= Line.Text.Contains(TEXT("Rumblejaw"));
			bIdsMatchPhase &= Line.Id.ToString().StartsWith(FString(TEXT("GD_")) + PhaseName + TEXT("_"));
			if (Line.Text.Contains(FGFGiantRules::TargetToken, ESearchCase::CaseSensitive))
			{
				Tokened.Add(Line.Id.ToString());
			}
			// Tokens are {Target} and {Dragger} only. Filling both leaves no braces behind.
			const FString Filled = FGFLineTokens::FillDragger(FGFGiantRules::FillTarget(Line.Text, TEXT("Pip")), TEXT("Rex"));
			const FString Unnamed = FGFLineTokens::FillDragger(FGFGiantRules::FillTarget(Line.Text, FString()), FString());
			if (Filled.Contains(TEXT("{")) || Filled.Contains(TEXT("}")) || Unnamed.Contains(TEXT("{")) || Unnamed.Contains(TEXT("}"))
				|| Unnamed.Contains(TEXT(" ,")) || Unnamed.Contains(TEXT(" ?")) || Unnamed.Contains(TEXT(" .")) || Unnamed.Contains(TEXT(",?"))
				|| Unnamed.Contains(TEXT(",.")))
			{
				++BadBraces;
				AddError(FString::Printf(TEXT("%s fills badly: '%s' / '%s'"), *Line.Id.ToString(), *Filled, *Unnamed));
			}
		}
	}
	TestTrue(TEXT("The named-target line is in (with the target's name)"), bNamedLine);
	TestTrue(TEXT("\"Why you running, {Target}?\" is in"), bWhyRunning);
	Tokened.Sort();
	TestEqual(TEXT("10 lines name the target"), Tokened.Num(), 10);
	TestEqual(TEXT("The lines that name the target"), FString::Join(Tokened, TEXT(",")),
		FString(TEXT("GD_CloseCall_01,GD_CloseCall_05,GD_Handoff_04,GD_Handoff_09,GD_Handoff_11,GD_Target_01,GD_Target_02,GD_Target_09,GD_Target_12,GD_Target_18")));
	TestEqual(TEXT("Every line fills cleanly"), BadBraces, 0);
	TestFalse(TEXT("No old placeholder name"), bRumble);
	TestTrue(TEXT("Every id matches its phase"), bIdsMatchPhase);

	const UDataTable* Info = FGFDataTables::Get<FGFGiantInfoRow>(TEXT("DT_GiantDino"));
	const FGFGiantInfoRow* Row = Info ? Info->FindRow<FGFGiantInfoRow>(TEXT("Giant"), TEXT("Test"), false) : nullptr;
	if (TestNotNull(TEXT("DT_GiantDino row"), Row))
	{
		TestEqual(TEXT("Name lives in data"), Row->DisplayName, FString(TEXT("Big Grumble")));
		TestEqual(TEXT("T. rex placeholder"), Row->AssetKey, FName(TEXT("Dino.Rex")));
		TestEqual(TEXT("3.5x"), Row->MeshScale, 3.5f);
		TestEqual(TEXT("Art mesh scale lives in data (4x, about 38 m)"), Row->ArtMeshScale, 4.f);
		TestEqual(TEXT("Art mesh scale default"), FGFGiantInfoRow().ArtMeshScale, 4.f);
		TestEqual(TEXT("Greybox body uses MeshScale"), Row->BodyScale(false), Row->MeshScale);
		TestEqual(TEXT("Art body uses ArtMeshScale"), Row->BodyScale(true), Row->ArtMeshScale);
		TestEqual(TEXT("Prowl speed at 1x lives in data"), Row->ProwlSpeed, 254.f);
		TestEqual(TEXT("Waddle speed at 1x lives in data"), Row->WaddleSpeed, 143.f);
		TestEqual(TEXT("Speed defaults"), FGFGiantInfoRow().ProwlSpeed + FGFGiantInfoRow().WaddleSpeed, 397.f);
		TestEqual(TEXT("Greybox prowl speed unscaled"), Row->ProwlSpeedFor(false), Row->ProwlSpeed);
		TestEqual(TEXT("Greybox waddle speed unscaled"), Row->WaddleSpeedFor(false), Row->WaddleSpeed);
		TestTrue(TEXT("Art prowl speed x ArtMeshScale"), FMath::IsNearlyEqual(Row->ProwlSpeedFor(true), Row->ProwlSpeed * Row->ArtMeshScale));
		TestTrue(TEXT("Art waddle speed x ArtMeshScale"), FMath::IsNearlyEqual(Row->WaddleSpeedFor(true), Row->WaddleSpeed * Row->ArtMeshScale));
		FGFGiantInfoRow Bigger = *Row;
		Bigger.ArtMeshScale = 2.f;
		TestTrue(TEXT("Art speeds follow the scale"), FMath::IsNearlyEqual(Bigger.ProwlSpeedFor(true), 508.f) && FMath::IsNearlyEqual(Bigger.WaddleSpeedFor(true), 286.f));
		TestEqual(TEXT("All chat"), Row->ChatChannel, FName(TEXT("All")));
	}

	// Picking by phase: always from that phase.
	FGFGiantTuning T;
	FRandomStream Rng(7);
	TArray<FName> Recent;
	for (EGFGiantLinePhase Phase : { EGFGiantLinePhase::Spotted, EGFGiantLinePhase::Chase, EGFGiantLinePhase::CloseCall, EGFGiantLinePhase::Herded,
		EGFGiantLinePhase::Circling, EGFGiantLinePhase::Leaving, EGFGiantLinePhase::Return, EGFGiantLinePhase::Snacked, EGFGiantLinePhase::Target,
		EGFGiantLinePhase::Handoff })
	{
		const FString Prefix = FString(TEXT("GD_")) + StaticEnum<EGFGiantLinePhase>()->GetNameStringByValue(int64(Phase)) + TEXT("_");
		for (int32 i = 0; i < 20; ++i)
		{
			FName Id;
			FString Text;
			TestTrue(TEXT("Picked"), FGFGiantRules::PickLine(T, L, Phase, Recent, Rng, Id, Text));
			if (!Id.ToString().StartsWith(Prefix))
			{
				AddError(FString::Printf(TEXT("%s picked for %s"), *Id.ToString(), *Prefix));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantTriggerTest, "GFGame.Giant.Trigger", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantTriggerTest::RunTest(const FString&)
{
	const FGFGiantTuning T;
	TestEqual(TEXT("Default: 2 days"), T.MinRealSecondsBetween, 172800.0);
	TestEqual(TEXT("Default: 3 h of play"), T.MinPlaySecondsSinceLast, 10800.0);
	const int64 Day = int64(24.0 * 3600.0 * FGFGiantRules::TicksPerSecond);
	const int64 T0 = FDateTime(2026, 10, 30).GetTicks();

	FGFGiantSave S;
	TestFalse(TEXT("Clock not started: never"), FGFGiantRules::ShouldTrigger(T, S, T0 + 10 * Day, true));
	FGFGiantRules::StartClockIfNeeded(S, T0);
	FGFGiantRules::StartClockIfNeeded(S, T0 + Day); // only the first call counts
	TestEqual(TEXT("Clock starts once"), S.LastEventUtcTicks, T0);

	// Two players for 1.5 h = 3 h of player time.
	for (int32 i = 0; i < 5400; ++i)
	{
		FGFGiantRules::AccumulatePlay(S, 1.0, 2);
	}
	TestEqual(TEXT("Play adds up per player"), S.PlaySecondsSinceLast, 10800.0);
	TestFalse(TEXT("Not before 2 days"), FGFGiantRules::ShouldTrigger(T, S, T0 + 2 * Day - 1, true));
	TestTrue(TEXT("2 days and 3 h: fires"), FGFGiantRules::ShouldTrigger(T, S, T0 + 2 * Day, true));
	TestFalse(TEXT("Only with someone outside the circle"), FGFGiantRules::ShouldTrigger(T, S, T0 + 2 * Day, false));

	FGFGiantSave Light = S;
	Light.PlaySecondsSinceLast = 10799.0;
	TestFalse(TEXT("Not without heavy play"), FGFGiantRules::ShouldTrigger(T, Light, T0 + 30 * Day, true));

	TestFalse(TEXT("First event is not a return"), FGFGiantRules::MarkEventStarted(S, T0 + 2 * Day));
	TestEqual(TEXT("Accumulator resets"), S.PlaySecondsSinceLast, 0.0);
	TestEqual(TEXT("Event counted"), S.EventCount, 1);
	TestFalse(TEXT("Right after: no"), FGFGiantRules::ShouldTrigger(T, S, T0 + 2 * Day + 1, true));
	S.PlaySecondsSinceLast = 20000.0;
	TestFalse(TEXT("Lots of play but only 1 day: no"), FGFGiantRules::ShouldTrigger(T, S, T0 + 3 * Day, true));
	TestTrue(TEXT("Next one after 2 more days"), FGFGiantRules::ShouldTrigger(T, S, T0 + 4 * Day, true));
	TestTrue(TEXT("Second event is a return"), FGFGiantRules::MarkEventStarted(S, T0 + 4 * Day));

	// Who is new.
	S.Touch(TEXT("A"), T0);
	S.Touch(TEXT("B"), T0 + Day);
	S.Touch(TEXT("A"), T0 + 5 * Day); // already known: first join unchanged
	S.PlayerSeconds[S.IndexOf(TEXT("A"))] = 500.0;
	TestEqual(TEXT("First join kept"), S.FirstSeenOf(TEXT("A")), T0);
	TestEqual(TEXT("B first join"), S.FirstSeenOf(TEXT("B")), T0 + Day);

	// Persists across saves (mode blob round trip), and the trigger answers the same after loading.
	TArray<uint8> Bytes;
	FGFModeBytes::Write(S, Bytes);
	FGFGiantSave Loaded;
	TestTrue(TEXT("Blob reads back"), FGFModeBytes::Read(Loaded, Bytes));
	TestEqual(TEXT("Clock saved"), Loaded.LastEventUtcTicks, S.LastEventUtcTicks);
	TestEqual(TEXT("Accumulator saved"), Loaded.PlaySecondsSinceLast, S.PlaySecondsSinceLast);
	TestEqual(TEXT("Events saved"), Loaded.EventCount, 2);
	TestEqual(TEXT("Players saved"), Loaded.PlayerKeys.Num(), 2);
	TestEqual(TEXT("Play time saved"), Loaded.SecondsOf(TEXT("A")), 500.0);
	TestEqual(TEXT("First join saved"), Loaded.FirstSeenOf(TEXT("B")), T0 + Day);
	Loaded.PlaySecondsSinceLast = S.PlaySecondsSinceLast = 10800.0;
	for (int64 Offset : { int64(0), Day, 2 * Day, 3 * Day })
	{
		TestTrue(TEXT("Deterministic after load"), FGFGiantRules::ShouldTrigger(T, Loaded, T0 + 4 * Day + Offset, true) == FGFGiantRules::ShouldTrigger(T, S, T0 + 4 * Day + Offset, true));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantTargetTest, "GFGame.Giant.Target", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantTargetTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	const FGFGiantTuning T;
	TestEqual(TEXT("Default region: 1 player"), FGFGiantRules::DefaultRegion(T, FVector::ZeroVector, 1).Radius(), 1200.f);
	TestEqual(TEXT("Default region grows with players"), FGFGiantRules::DefaultRegion(T, FVector::ZeroVector, 3).Radius(), 2000.f);
	TestEqual(TEXT("Default region capped"), FGFGiantRules::DefaultRegion(T, FVector::ZeroVector, 12).Radius(), 3200.f);

	const FGFSafeCircles C = HomeZone(T, 4);
	const float R = C.Regions[0].Radius();
	TArray<FGFGiantPlayerView> P = {
		Player(TEXT("Vet"), FVector(R + 2000.f, 0, 0), 50000.0, 100),
		Player(TEXT("Newbie"), FVector(R + 300.f, 0, 0), 600.0, 200),
		Player(TEXT("Mid"), FVector(0, R + 5000.f, 0), 8000.0, 300),
		Player(TEXT("HomeNewest"), FVector(10.f, 0, 0), 0.0, 400), // inside: never a target
	};
	int32 i = FGFGiantRules::PickTarget(T, C, P);
	TestEqual(TEXT("Newest (least play) outside wins over farther players"), i != INDEX_NONE ? P[i].Key : FString(), FString(TEXT("Newbie")));

	// Play time within the tie window: the most recent first join wins.
	P[2].PlaySeconds = 620.0;
	i = FGFGiantRules::PickTarget(T, C, P);
	TestEqual(TEXT("Tie on play time: latest first join"), i != INDEX_NONE ? P[i].Key : FString(), FString(TEXT("Mid")));

	// Same play time and join: farthest out.
	P[2].FirstSeenTicks = 200;
	i = FGFGiantRules::PickTarget(T, C, P);
	TestEqual(TEXT("Full tie: farthest out"), i != INDEX_NONE ? P[i].Key : FString(), FString(TEXT("Mid")));

	// The newbie goes home: next newest.
	P[1].Location = FVector(100.f, 0, 0);
	P[2].PlaySeconds = 90000.0;
	i = FGFGiantRules::PickTarget(T, C, P);
	TestEqual(TEXT("Only players outside count"), i != INDEX_NONE ? P[i].Key : FString(), FString(TEXT("Vet")));

	P[0].Location = P[2].Location = FVector(5.f, 5.f, 0);
	TestEqual(TEXT("Everyone home: no target"), FGFGiantRules::PickTarget(T, C, P), int32(INDEX_NONE));

	// The sim chases the newest from the start.
	TArray<FGFGiantPlayerView> Q = { Player(TEXT("Old"), FVector(3000, 0, 0), 9000.0, 1), Player(TEXT("New"), FVector(-2500, 0, 0), 30.0, 2) };
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	Sim.Begin(Quiet(), FGFGiantLines::Load(), C, Q, false, 3, Out);
	TestFalse(TEXT("Spawns outside safety"), C.IsInsideSafe(Sim.Location));

	// Several regions: "farthest out" means farthest from the nearest safety, not from one centre.
	const FGFSafeCircles Two({ Region(FVector(-3000.f, 0.f, 0.f), 1), Region(FVector(3000.f, 0.f, 0.f), 1) });
	TArray<FGFGiantPlayerView> W = {
		Player(TEXT("NearB"), FVector(4300.f, 0.f, 0.f), 100.0, 5),   // 300 outside region B
		Player(TEXT("Between"), FVector(0.f, 0.f, 0.f), 100.0, 5),    // 2000 outside both
		Player(TEXT("InA"), FVector(-3000.f, 200.f, 0.f), 0.0, 9),    // inside region A: never a target
	};
	i = FGFGiantRules::PickTarget(T, Two, W);
	TestEqual(TEXT("Regions: farthest from any safety"), i != INDEX_NONE ? W[i].Key : FString(), FString(TEXT("Between")));
	TestEqual(TEXT("Sim targets the newest"), Sim.TargetKey, FString(TEXT("New")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantHerdTest, "GFGame.Giant.Herd", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantHerdTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	FGFGiantTuning T;
	const FVector C(100.f, -200.f, 0.f);
	FGFSafeRegion Off = FGFGiantRules::DefaultRegion(T, C, 1);
	const FGFSafeCircles One({ Off });
	const FVector Target(3100.f, -200.f, 50.f);
	const FVector Goal = FGFGiantRules::HerdGoal(T, One, Target);
	TestTrue(TEXT("Goal is behind the target (farther from safety)"), FGFSafeZone::DistanceOutside(One, Goal) > FGFSafeZone::DistanceOutside(One, Target));
	TestTrue(TEXT("Goal is on the target's side"), FVector::DotProduct((Goal - C).GetSafeNormal2D(), (Target - C).GetSafeNormal2D()) > 0.99f);
	const FVector Shove = FGFGiantRules::ShoveVelocity(T, One, Target);
	TestTrue(TEXT("Shove points at safety"), FVector::DotProduct(Shove.GetSafeNormal2D(), (C - Target).GetSafeNormal2D()) > 0.99f);
	TestEqual(TEXT("Shove strength"), double(FVector(Shove.X, Shove.Y, 0.f).Size()), double(T.ShoveSpeed), 1.0);

	// Several regions: shoves go to the nearest one.
	const FGFSafeCircles Three({ Region(FVector(-3000.f, 0.f, 0.f), 1), Region(FVector(3000.f, 0.f, 0.f), 2), Region(FVector(0.f, 4000.f, 0.f), 3) });
	const FVector NearThird(200.f, 6500.f, 0.f);
	TestTrue(TEXT("Shove toward the nearest region"), FGFGiantRules::ShoveVelocity(T, Three, NearThird).GetSafeNormal2D().Y < -0.9f);
	const FVector NearSecond(5000.f, -300.f, 0.f);
	TestTrue(TEXT("Shove toward another region"), FGFGiantRules::ShoveVelocity(T, Three, NearSecond).GetSafeNormal2D().X < -0.9f);

	// A passive player outside: the giant gets behind them, shoves them home, then circles. Players obey shoves.
	T.bEatIdleTarget = false;
	T.EventSeconds = 600.f;
	TArray<FGFGiantPlayerView> P = { Player(TEXT("A"), FVector(4000.f, 1500.f, 0.f), 10.0, 1) };
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	const FGFSafeCircles Zone1 = HomeZone(T, P.Num());
	Sim.Begin(T, FGFGiantLines::Load(), Zone1, P, false, 11, Out);
	FVector Vel = FVector::ZeroVector;
	const float Start = FVector::Dist2D(P[0].Location, FVector::ZeroVector);
	int32 Shoves = 0;
	float Brought = -1.f;
	for (int32 Step = 0; Step < 1200 && Brought < 0.f; ++Step)
	{
		const float Dt = 0.05f;
		FGFGiantStepOut O;
		Sim.Step(Dt, P, O);
		for (const FGFGiantShove& S : O.Shoves)
		{
			TestTrue(TEXT("Every shove points at safety"), FVector::DotProduct(S.Velocity.GetSafeNormal2D(), (FVector::ZeroVector - P[0].Location).GetSafeNormal2D()) > 0.99f);
			Vel = FVector(S.Velocity.X, S.Velocity.Y, 0.f);
			++Shoves;
		}
		P[0].Location += Vel * Dt;
		Vel *= 0.9f; // friction
		if (Zone1.IsInsideSafe(P[0].Location))
		{
			Brought = Step * Dt;
		}
	}
	TestTrue(TEXT("It shoved"), Shoves > 0);
	TestTrue(TEXT("Herded home within a minute"), Brought >= 0.f);
	TestTrue(TEXT("Moved toward safety"), FVector::Dist2D(P[0].Location, FVector::ZeroVector) < Start);
	FGFGiantStepOut O;
	Sim.Step(0.05f, P, O);
	TestTrue(TEXT("Everyone home: circling"), Sim.State == EGFGiantState::Circling);
	TestEqual(TEXT("Nobody eaten while herding"), Sim.EatenKey, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantCircleTest, "GFGame.Giant.NeverEntersSafe", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantCircleTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	FGFGiantTuning T;
	T.bEatIdleTarget = false;
	T.EventSeconds = 1000.f;
	const FGFGiantLines Lines = FGFGiantLines::Load();
	FRandomStream Rng(99);
	int32 Violations = 0;
	float Closest = TNumericLimits<float>::Max();
	for (int32 Run = 0; Run < 60; ++Run)
	{
		// 1 to 3 regions (some overlapping), each with its own member count; members change mid-run.
		const int32 NumRegions = 1 + Run % 3;
		FGFSafeCircles Zone;
		for (int32 r = 0; r < NumRegions; ++r)
		{
			Zone.Regions.Add(Region(FVector(Rng.FRandRange(-4000.f, 4000.f), Rng.FRandRange(-4000.f, 4000.f), 0.f), Rng.RandRange(1, 5)));
		}
		const int32 N = Rng.RandRange(1, 5);
		TArray<FGFGiantPlayerView> P;
		for (int32 k = 0; k < N; ++k)
		{
			const float A = Rng.FRandRange(0.f, 2.f * PI);
			const float D = Rng.FRandRange(0.f, 8000.f);
			P.Add(Player(*FString::Printf(TEXT("P%d"), k), FVector(D * FMath::Cos(A), D * FMath::Sin(A), 0.f), Rng.FRandRange(0.f, 9000.f), k));
		}
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.Begin(T, Lines, Zone, P, false, Run + 1, Out);
		for (int32 Step = 0; Step < 400; ++Step)
		{
			// Players wander, some walk straight through safety (the giant must slide around, not in).
			for (FGFGiantPlayerView& V : P)
			{
				V.Location += FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), 0.f) * 60.f;
			}
			if (Step == 200)
			{
				Zone.Regions[0].Members += 3; // a group grows: its territory widens under the giant
			}
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			const float Gap = Zone.IsInsideSafe(Sim.Location) ? -1.f : FGFSafeZone::DistanceOutside(Zone, Sim.Location) - T.GiantRadius;
			Closest = FMath::Min(Closest, Gap);
			if (Gap < -1.f || Zone.IsInsideSafe(Sim.Location))
			{
				++Violations;
			}
		}
	}
	TestEqual(TEXT("Never inside any safe region (incl. its own body radius)"), Violations, 0);
	AddInfo(FString::Printf(TEXT("Closest approach to a safe border: %.1f cm"), Closest));

	// ClampOutside pushes out of one region, and out of two overlapping ones without landing in the other.
	const FGFSafeCircles One({ Region(FVector::ZeroVector, 1) });
	TestEqual(TEXT("ClampOutside pushes out"), double(FVector::Dist2D(FGFGiantRules::ClampOutside(T, One, FVector(10.f, 0.f, 0.f)), FVector::ZeroVector)), double(1000.f + T.GiantRadius), 1.0);
	const FGFSafeCircles Overlap({ Region(FVector::ZeroVector, 1), Region(FVector(1500.f, 0.f, 0.f), 1) });
	const FVector Pushed = FGFGiantRules::ClampOutside(T, Overlap, FVector(750.f, 10.f, 0.f));
	TestTrue(TEXT("Overlapping regions: pushed clear of both"), FGFSafeZone::IsClear(Overlap, Pushed, T.GiantRadius));

	// Any safe-zone implementation works (here: a half-plane), the giant only asks the interface.
	FHalfPlaneZone Half;
	Half.Edge = 0.f;
	TArray<FGFGiantPlayerView> H = { Player(TEXT("H"), FVector(3000.f, 0.f, 0.f), 1.0, 1) };
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	FGFGiantTuning TH = T;
	TH.EventSeconds = 120.f;
	Sim.Begin(TH, Lines, Half, H, false, 5, Out);
	FVector Vel = FVector::ZeroVector;
	bool bHome = false;
	for (int32 Step = 0; Step < 2400 && Sim.IsActive(); ++Step)
	{
		FGFGiantStepOut O;
		Sim.Step(0.05f, H, O);
		TestTrue(TEXT("Half-plane: giant stays out"), Sim.Location.X >= TH.GiantRadius - 1.f);
		for (const FGFGiantShove& S : O.Shoves)
		{
			TestTrue(TEXT("Half-plane: shoves point at safety"), S.Velocity.X < 0.f && FMath::Abs(S.Velocity.Y) < 1.f);
			Vel = FVector(S.Velocity.X, S.Velocity.Y, 0.f);
		}
		H[0].Location += Vel * 0.05f;
		Vel *= 0.9f;
		bHome |= Half.IsInsideSafe(H[0].Location);
	}
	TestTrue(TEXT("Half-plane: herded home"), bHome);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantSafeZoneTest, "GFGame.Giant.SafeZone", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantSafeZoneTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	// Region growth: each region grows with its own member count, up to its cap.
	FGFSafeRegion R = Region(FVector::ZeroVector, 1);
	TestEqual(TEXT("1 member"), R.Radius(), 1000.f);
	R.Members = 3;
	TestEqual(TEXT("3 members"), R.Radius(), 1600.f);
	R.Members = 40;
	TestEqual(TEXT("Capped"), R.Radius(), 2200.f);

	// Up to three regions of different sizes.
	const FGFSafeCircles Z({ Region(FVector(-4000.f, 0.f, 0.f), 1), Region(FVector(4000.f, 0.f, 0.f), 3), Region(FVector(0.f, 5000.f, 0.f), 5) });
	TestTrue(TEXT("Inside small region"), Z.IsInsideSafe(FVector(-4000.f, 900.f, 0.f)));
	TestFalse(TEXT("Just outside small region"), Z.IsInsideSafe(FVector(-4000.f, 1100.f, 0.f)));
	TestTrue(TEXT("Inside bigger region (grew with members)"), Z.IsInsideSafe(FVector(4000.f, 1500.f, 0.f)));
	TestTrue(TEXT("Inside biggest region"), Z.IsInsideSafe(FVector(0.f, 5000.f + 2100.f, 0.f)));
	TestFalse(TEXT("Between regions is unsafe"), Z.IsInsideSafe(FVector::ZeroVector));
	TestEqual(TEXT("Closest safe point of an inside spot is itself"), Z.ClosestSafePoint(FVector(4000.f, 10.f, 7.f)), FVector(4000.f, 10.f, 7.f));
	const FVector CS = Z.ClosestSafePoint(FVector(6000.f, 0.f, 0.f));
	TestEqual(TEXT("Closest safe point on the nearest region's border"), double(CS.X), 5600.0, 1.0);
	TestEqual(TEXT("Distance outside"), double(FGFSafeZone::DistanceOutside(Z, FVector(6000.f, 0.f, 0.f))), 400.0, 1.0);

	// The nearest border of a union skips arcs covered by another region.
	const FGFSafeCircles O({ Region(FVector::ZeroVector, 1), Region(FVector(1500.f, 0.f, 0.f), 1) });
	FVector EP, EN;
	TestTrue(TEXT("Has an edge"), O.ClosestEdge(FVector(700.f, 50.f, 0.f), EP, EN));
	TestFalse(TEXT("Edge point isn't inside the other region"), FVector::Dist2D(EP, FVector(1500.f, 0.f, 0.f)) < 999.f && FVector::Dist2D(EP, FVector::ZeroVector) < 999.f);

	// Respawn: just inside the nearest region's border.
	FGFGiantTuning T;
	const FVector At = FGFGiantRules::RespawnPoint(T, Z, FVector(6500.f, 300.f, 40.f));
	TestTrue(TEXT("Respawn inside the nearest region"), Z.IsInsideSafe(At) && FVector::Dist2D(At, FVector(4000.f, 0.f, 0.f)) >= 1600.f - T.RespawnInset - 1.f);
	TestEqual(TEXT("Respawn keeps height"), double(At.Z), 40.0, 0.01);

	// Everyone home in different regions: it walks the borders, never inside, and keeps grumbling.
	FGFGiantTuning TC;
	TC.EventSeconds = 200.f;
	TArray<FGFGiantPlayerView> P = { Player(TEXT("A"), FVector(-4000.f, 100.f, 0.f), 5.0, 1), Player(TEXT("B"), FVector(4100.f, 0.f, 0.f), 6.0, 2) };
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	Sim.Begin(TC, FGFGiantLines::Load(), Z, P, false, 17, Out);
	float MaxEdgeGap = 0.f;
	int32 Inside = 0, HerdedLines = 0;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		FGFGiantStepOut S;
		Sim.Step(0.1f, P, S);
		HerdedLines += CountPhase(S.Chat, EGFGiantLinePhase::Herded);
		Inside += Z.IsInsideSafe(Sim.Location) || !FGFSafeZone::IsClear(Z, Sim.Location, TC.GiantRadius) ? 1 : 0;
		if (Step > 300)
		{
			MaxEdgeGap = FMath::Max(MaxEdgeGap, FGFSafeZone::DistanceOutside(Z, Sim.Location));
		}
	}
	TestTrue(TEXT("Circling with everyone home"), Sim.State == EGFGiantState::Circling);
	TestEqual(TEXT("Already home: no Herded line"), HerdedLines, 0);
	TestEqual(TEXT("Edge-walking never enters a region"), Inside, 0);
	TestTrue(TEXT("Edge-walking hugs a border"), MaxEdgeGap <= TC.GiantRadius + 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantNoDamageTest, "GFGame.Giant.NoDamage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantNoDamageTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	// The sim's only effects on players are shoves (bounded, toward the circle) and the one snack (a faint, not damage).
	FGFGiantTuning T;
	T.bEatIdleTarget = false;
	TArray<FGFGiantPlayerView> P = { Player(TEXT("A"), FVector(2500, 0, 0), 1.0, 1), Player(TEXT("B"), FVector(-2600, 300, 0), 2.0, 2) };
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	const FGFSafeCircles Zone3 = HomeZone(T, P.Num());
	Sim.Begin(T, FGFGiantLines::Load(), Zone3, P, false, 5, Out);
	int32 Shoves = 0;
	bool bBounded = true, bInward = true;
	for (int32 Step = 0; Step < 3000 && Sim.IsActive(); ++Step)
	{
		FGFGiantStepOut O;
		Sim.Step(0.1f, P, O);
		TestTrue(TEXT("Nobody eaten without attacking or standing still"), O.Eaten.IsEmpty());
		for (const FGFGiantShove& S : O.Shoves)
		{
			++Shoves;
			bBounded &= S.Velocity.Size() <= FVector(T.ShoveSpeed, 0.f, T.ShoveUpSpeed).Size() + 1.f;
			const FGFGiantPlayerView* V = P.FindByPredicate([&](const FGFGiantPlayerView& X) { return X.Key == S.Key; });
			bInward &= V && FVector::DotProduct(S.Velocity.GetSafeNormal2D(), (-V->Location).GetSafeNormal2D()) > 0.99f;
		}
		Orbit(P, 300.f, 0.1f);
	}
	TestTrue(TEXT("It did shove"), Shoves > 0);
	TestTrue(TEXT("Shoves are bounded knockbacks"), bBounded);
	TestTrue(TEXT("Shoves only push toward the circle"), bInward);
	TestEqual(TEXT("Event ran out (not cut short)"), Sim.EatenKey, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantPhasesTest, "GFGame.Giant.Phases", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantPhasesTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	FGFGiantTuning T = Quiet();
	T.bEatIdleTarget = false;
	const FGFGiantLines Lines = FGFGiantLines::Load();
	TArray<FGFGiantPlayerView> P = { Player(TEXT("A"), FVector(3000, 0, 0), 1.0, 1) };

	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	const FGFSafeCircles Zone4 = HomeZone(T, P.Num());
	Sim.Begin(T, Lines, Zone4, P, false, 21, Out);
	TestTrue(TEXT("First visit opens with a Spotted line"), Out.Chat.Num() == 1 && Out.Chat[0].Phase == EGFGiantLinePhase::Spotted);
	TestEqual(TEXT("All chat"), Out.Chat.Num() > 0 ? Out.Chat[0].Channel : NAME_None, FName(TEXT("All")));

	FGFGiantSim Again;
	FGFGiantStepOut Out2;
	const FGFSafeCircles Zone5 = HomeZone(T, P.Num());
	Again.Begin(T, Lines, Zone5, P, true, 21, Out2);
	TestTrue(TEXT("Later visits open with a Return line"), Out2.Chat.Num() == 1 && Out2.Chat[0].Phase == EGFGiantLinePhase::Return);

	// Chase a bit, then everybody goes home: Herded, then Circling lines on the circling cooldown.
	TArray<FGFGiantChatLine> All;
	for (int32 i = 0; i < 100; ++i)
	{
		FGFGiantStepOut O;
		Sim.Step(0.1f, P, O);
		Orbit(P, 300.f, 0.1f);
		All.Append(O.Chat);
	}
	TestTrue(TEXT("Chase talk while chasing"), CountPhase(All, EGFGiantLinePhase::Chase) + CountPhase(All, EGFGiantLinePhase::Target) > 0);
	TestEqual(TEXT("No circling talk while chasing"), CountPhase(All, EGFGiantLinePhase::Circling), 0);
	All.Reset();
	P[0].Location = FVector(200, 0, 0);
	for (int32 i = 0; i < 300; ++i)
	{
		FGFGiantStepOut O;
		Sim.Step(0.1f, P, O);
		All.Append(O.Chat);
	}
	TestTrue(TEXT("Herded line first"), All.Num() > 0 && All[0].Phase == EGFGiantLinePhase::Herded);
	TestEqual(TEXT("One Herded line"), CountPhase(All, EGFGiantLinePhase::Herded), 1);
	TestTrue(TEXT("Circling lines on a cooldown (30 s -> 3..5)"), CountPhase(All, EGFGiantLinePhase::Circling) >= 3 && CountPhase(All, EGFGiantLinePhase::Circling) <= 5);
	TestEqual(TEXT("No chase talk while circling"), CountPhase(All, EGFGiantLinePhase::Chase) + CountPhase(All, EGFGiantLinePhase::Target), 0);
	TestTrue(TEXT("Circling"), Sim.State == EGFGiantState::Circling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantLeaveTest, "GFGame.Giant.LeaveTimer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantLeaveTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	FGFGiantTuning T = Quiet();
	TestEqual(TEXT("Default: about 5 minutes"), T.EventSeconds, 300.f);
	TArray<FGFGiantPlayerView> P = { Player(TEXT("A"), FVector(3000, 0, 0), 1.0, 1) };
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	const FGFSafeCircles Zone6 = HomeZone(T, P.Num());
	Sim.Begin(T, FGFGiantLines::Load(), Zone6, P, false, 4, Out);
	const float Dt = 0.1f;
	float LeftAt = -1.f, GoneAt = -1.f;
	FGFGiantChatLine Bye;
	for (int32 i = 0; i < 4000 && GoneAt < 0.f; ++i)
	{
		FGFGiantStepOut O;
		Sim.Step(Dt, P, O);
		Orbit(P, 300.f, Dt);
		for (const FGFGiantChatLine& L : O.Chat)
		{
			if (L.Phase == EGFGiantLinePhase::Leaving && LeftAt < 0.f)
			{
				LeftAt = Sim.Elapsed;
				Bye = L;
			}
		}
		if (O.bEnded)
		{
			GoneAt = Sim.Elapsed;
		}
	}
	TestTrue(TEXT("Leaves at 5 minutes"), LeftAt >= 300.f - 0.01f && LeftAt <= 300.f + Dt + 0.01f);
	TestTrue(TEXT("Says a Leaving line"), Bye.LineId.ToString().StartsWith(TEXT("GD_Leaving_")));
	TestTrue(TEXT("Gone after its walk-off"), GoneAt >= LeftAt + T.LeaveWalkSeconds - 0.01f && GoneAt <= LeftAt + T.LeaveWalkSeconds + Dt + 0.01f);
	TestTrue(TEXT("Gone"), Sim.State == EGFGiantState::Gone);
	TestEqual(TEXT("Nobody eaten"), Sim.EatenKey, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantSlowdownTest, "GFGame.Giant.Slowdown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantSlowdownTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	FGFGiantTuning T = Quiet();
	TestEqual(TEXT("Starts at 1.1x run speed"), FGFGiantRules::ChaseSpeed(T, 600.f, 0.f), 660.f, 0.01f);
	TestEqual(TEXT("Halfway"), FGFGiantRules::ChaseSpeed(T, 600.f, 10.f), 570.f, 0.01f);
	TestEqual(TEXT("0.8x after 20 s"), FGFGiantRules::ChaseSpeed(T, 600.f, 20.f), 480.f, 0.01f);
	TestEqual(TEXT("Stays at 0.8x"), FGFGiantRules::ChaseSpeed(T, 600.f, 120.f), 480.f, 0.01f);
	TestEqual(TEXT("Fallback run speed"), FGFGiantRules::ChaseSpeed(T, 0.f, 0.f), T.PlayerRunSpeed * 1.1f, 0.01f);

	// In the sim: slows down on one target, then starts fast again on a new one.
	T.bEatIdleTarget = false;
	TArray<FGFGiantPlayerView> P = { Player(TEXT("New"), FVector(4000, 0, 0), 1.0, 2), Player(TEXT("Old"), FVector(-4000, 0, 0), 9000.0, 1) };
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	const FGFSafeCircles Zone7 = HomeZone(T, P.Num());
	Sim.Begin(T, FGFGiantLines::Load(), Zone7, P, false, 8, Out);
	FGFGiantStepOut O;
	Sim.Step(0.1f, P, O);
	TestEqual(TEXT("Fast at first"), Sim.LastSpeed, FGFGiantRules::ChaseSpeed(T, 600.f, 0.1f), 0.1f);
	for (int32 i = 0; i < 250; ++i)
	{
		FGFGiantStepOut X;
		Sim.Step(0.1f, P, X);
		Orbit(P, 300.f, 0.1f);
	}
	TestEqual(TEXT("Eased down to 0.8x after ~20 s"), Sim.LastSpeed, 480.f, 0.5f);
	P[0].Location = FVector(100, 0, 0); // the newbie makes it home: on to the next
	FGFGiantStepOut Y;
	Sim.Step(0.1f, P, Y);
	TestEqual(TEXT("New target"), Sim.TargetKey, FString(TEXT("Old")));
	TestEqual(TEXT("Fast again on a new target"), Sim.LastSpeed, FGFGiantRules::ChaseSpeed(T, 600.f, 0.1f), 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantChatTest, "GFGame.Giant.Chat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantChatTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	// {Target}
	TestEqual(TEXT("Name filled in"), FGFGiantRules::FillTarget(TEXT("Hey {Target}! Where you going?"), TEXT("Pip")), FString(TEXT("Hey Pip! Where you going?")));
	TestEqual(TEXT("Lines without the token unchanged"), FGFGiantRules::FillTarget(TEXT("Why you running?"), TEXT("Pip")), FString(TEXT("Why you running?")));
	TestEqual(TEXT("Named-target line with the name"), FGFGiantRules::FillTarget(TEXT("Where you going, {Target}? Huh? Where you going?"), TEXT("Pip")),
		FString(TEXT("Where you going, Pip? Huh? Where you going?")));
	TestEqual(TEXT("Token is case-insensitive"), FGFGiantRules::FillTarget(TEXT("Hey {target}!"), TEXT("Pip")), FString(TEXT("Hey Pip!")));
	TestEqual(TEXT("No name: comma lead-in dropped"), FGFGiantRules::FillTarget(TEXT("Where you going, {Target}? Huh? Where you going?"), FString()),
		FString(TEXT("Where you going? Huh? Where you going?")));
	TestEqual(TEXT("No name: space lead-in dropped"), FGFGiantRules::FillTarget(TEXT("Hey {Target}! Yes YOU!"), FString()), FString(TEXT("Hey! Yes YOU!")));
	TestEqual(TEXT("No name: sentence token dropped"), FGFGiantRules::FillTarget(TEXT("Hey. Hey. {Target}. Where you going?"), FString()),
		FString(TEXT("Hey. Hey. Where you going?")));

	FGFGiantLines Custom;
	Custom.ByPhase.Add(EGFGiantLinePhase::Spotted, { { TEXT("S1"), TEXT("Hi {Target}") } });
	Custom.ByPhase.Add(EGFGiantLinePhase::Target, { { TEXT("T1"), TEXT("{Target}, where you going?") } });
	Custom.ByPhase.Add(EGFGiantLinePhase::Chase, { { TEXT("C1"), TEXT("Run!") } });
	FGFGiantTuning T = Quiet();
	T.bEatIdleTarget = false;
	T.TargetLineWeight = 1.f;
	TArray<FGFGiantPlayerView> P = { Player(TEXT("K"), FVector(3000, 0, 0), 1.0, 1) };
	P[0].Name = TEXT("Pip");
	{
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		const FGFSafeCircles Zone8 = HomeZone(T, P.Num());
		Sim.Begin(T, Custom, Zone8, P, false, 2, Out);
		TestEqual(TEXT("Opening line names the target"), Out.Chat.Num() > 0 ? Out.Chat[0].Text : FString(), FString(TEXT("Hi Pip")));
		FGFGiantChatLine Got;
		for (int32 i = 0; i < 100 && Got.LineId.IsNone(); ++i)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			Orbit(P, 300.f, 0.1f);
			if (O.Chat.Num() > 0)
			{
				Got = O.Chat[0];
			}
		}
		TestEqual(TEXT("Target line filled"), Got.Text, FString(TEXT("Pip, where you going?")));
		TestEqual(TEXT("Aimed at the target"), Got.TargetKey, FString(TEXT("K")));
	}

	// Cadence with the real lines: a line every 4-6 s while chasing, ~70 % Target lines, no repeats in the last 4.
	T = Quiet();
	T.bEatIdleTarget = false;
	T.EventSeconds = 5000.f;
	P[0].Name = TEXT("Pip");
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	const FGFSafeCircles Zone9 = HomeZone(T, P.Num());
	Sim.Begin(T, FGFGiantLines::Load(), Zone9, P, false, 77, Out);
	TArray<FGFGiantChatLine> All = Out.Chat;
	const float Dt = 0.05f;
	for (int32 i = 0; i < int32(1800.f / Dt); ++i)
	{
		FGFGiantStepOut O;
		Sim.Step(Dt, P, O);
		Orbit(P, 300.f, Dt);
		All.Append(O.Chat);
	}
	TestTrue(TEXT("Still chasing"), Sim.State == EGFGiantState::Chase);
	int32 Target = 0, Chase = 0, BadGap = 0, Repeats = 0, Named = 0, Unfilled = 0;
	for (const FGFGiantChatLine& L : All)
	{
		Named += L.Text.Contains(TEXT("Pip")) ? 1 : 0;
		Unfilled += L.Text.Contains(TEXT("{")) ? 1 : 0;
	}
	TestTrue(TEXT("Real v5 lines call the target by name"), Named > 0);
	TestEqual(TEXT("No raw {Target} reaches chat"), Unfilled, 0);
	for (int32 i = 1; i < All.Num(); ++i)
	{
		const float Gap = All[i].At - All[i - 1].At;
		if (Gap < T.ChaseChatMinSeconds - Dt - 0.001f || Gap > T.ChaseChatMaxSeconds + Dt + 0.001f)
		{
			++BadGap;
		}
		Target += All[i].Phase == EGFGiantLinePhase::Target ? 1 : 0;
		Chase += All[i].Phase == EGFGiantLinePhase::Chase ? 1 : 0;
		for (int32 k = FMath::Max(0, i - T.NoRepeatCount); k < i; ++k)
		{
			Repeats += All[k].LineId == All[i].LineId ? 1 : 0;
		}
		if (All[i].Phase == EGFGiantLinePhase::Target && All[i].TargetKey != TEXT("K"))
		{
			AddError(TEXT("Target line not aimed at the target"));
		}
	}
	const int32 Lines = Target + Chase;
	AddInfo(FString::Printf(TEXT("%d lines in 30 min: %d Target, %d Chase"), Lines, Target, Chase));
	TestEqual(TEXT("Only Target and Chase lines while chasing"), Lines, All.Num() - 1);
	TestTrue(TEXT("Talks almost nonstop (one line every 4-6 s)"), Lines >= int32(1800.f / 6.f) - 2 && Lines <= int32(1800.f / 4.f) + 2);
	TestEqual(TEXT("Every gap 4-6 s"), BadGap, 0);
	const float Share = Lines > 0 ? float(Target) / Lines : 0.f;
	TestTrue(TEXT("About 70 % Target lines"), Share >= 0.6f && Share <= 0.8f);
	TestEqual(TEXT("No repeats within the last 4 lines"), Repeats, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantEatTest, "GFGame.Giant.Eat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantEatTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	const FGFGiantLines Lines = FGFGiantLines::Load();
	FGFGiantTuning T = Quiet();
	T.bEatIdleTarget = false;

	// Attacking: the attacker is eaten (not the target), it leaves at once with a Snacked line, and only once.
	{
		TArray<FGFGiantPlayerView> P = { Player(TEXT("Newbie"), FVector(3000, 0, 0), 1.0, 2), Player(TEXT("Brave"), FVector(-3000, 0, 0), 9000.0, 1) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		const FGFSafeCircles Zone10 = HomeZone(T, P.Num());
		Sim.Begin(T, Lines, Zone10, P, false, 9, Out);
		for (int32 i = 0; i < 50; ++i)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			Orbit(P, 300.f, 0.1f);
		}
		FGFGiantStepOut O;
		TestTrue(TEXT("Attacker is eaten"), Sim.Attacked(TEXT("Brave"), P, O));
		TestEqual(TEXT("Eaten: the attacker"), O.Eaten, FString(TEXT("Brave")));
		TestTrue(TEXT("Event ends at once"), O.bEnded);
		TestTrue(TEXT("Gone"), Sim.State == EGFGiantState::Gone);
		TestTrue(TEXT("Snacked line"), O.Chat.Num() == 1 && O.Chat[0].Phase == EGFGiantLinePhase::Snacked && O.Chat[0].TargetKey == TEXT("Brave"));
		FGFGiantStepOut O2;
		TestFalse(TEXT("Only one player is ever eaten"), Sim.Attacked(TEXT("Newbie"), P, O2));
		TestTrue(TEXT("Nothing more happens"), O2.Eaten.IsEmpty() && O2.Chat.Num() == 0);
		Sim.Step(0.1f, P, O2);
		TestTrue(TEXT("No more steps after leaving"), O2.Chat.Num() == 0 && O2.Shoves.Num() == 0);
	}

	// A bystander who never attacks (and keeps moving) is never eaten, for the whole event.
	{
		FGFGiantTuning T2 = T;
		T2.bEatIdleTarget = true;
		T2.ShoveRange = 700.f;
		TArray<FGFGiantPlayerView> P = { Player(TEXT("Runner"), FVector(2600, 0, 0), 1.0, 2), Player(TEXT("Other"), FVector(-2600, 0, 0), 9000.0, 1) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		const FGFSafeCircles Zone11 = HomeZone(T2, P.Num());
		Sim.Begin(T2, Lines, Zone11, P, false, 10, Out);
		bool bAnyEaten = false;
		for (int32 i = 0; i < 4000 && Sim.IsActive(); ++i)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			bAnyEaten |= !O.Eaten.IsEmpty();
			Orbit(P, 300.f, 0.1f);
		}
		TestFalse(TEXT("Non-attackers who keep moving are never eaten"), bAnyEaten);
		TestTrue(TEXT("It left on its own"), Sim.State == EGFGiantState::Gone);
	}

	// Standing still: warning taunt (Target line) at 1.5 s, eaten at 3 s, giant leaves at once.
	{
		FGFGiantTuning T3 = T;
		T3.bEatIdleTarget = true;
		TArray<FGFGiantPlayerView> P = { Player(TEXT("Frozen"), FVector(2000, 0, 0), 1.0, 2) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		const FGFSafeCircles Zone12 = HomeZone(T3, P.Num());
		Sim.Begin(T3, Lines, Zone12, P, false, 12, Out);
		// Let it close in while the player runs (no idle time yet), then they stop.
		for (int32 i = 0; i < 400 && FVector::Dist2D(Sim.Location, P[0].Location) > T3.IdleRange * 0.8f; ++i)
		{
			FGFGiantStepOut O;
			Sim.Step(0.05f, P, O);
			Orbit(P, 200.f, 0.05f);
		}
		TestTrue(TEXT("It got within 15 m"), FVector::Dist2D(Sim.Location, P[0].Location) <= T3.IdleRange);
		const float StopAt = Sim.Elapsed;
		float WarnAt = -1.f, EatAt = -1.f;
		for (int32 i = 0; i < 200 && EatAt < 0.f; ++i)
		{
			FGFGiantStepOut O;
			Sim.Step(0.05f, P, O);
			for (const FGFGiantChatLine& L : O.Chat)
			{
				if (L.Phase == EGFGiantLinePhase::Target && WarnAt < 0.f && Sim.Elapsed - StopAt >= 1.4f)
				{
					WarnAt = Sim.Elapsed - StopAt;
					TestEqual(TEXT("Warning aimed at the player"), L.TargetKey, FString(TEXT("Frozen")));
				}
			}
			if (!O.Eaten.IsEmpty())
			{
				EatAt = Sim.Elapsed - StopAt;
				TestEqual(TEXT("Eats the one standing still"), O.Eaten, FString(TEXT("Frozen")));
				TestTrue(TEXT("Leaves at once"), O.bEnded && Sim.State == EGFGiantState::Gone);
			}
		}
		TestTrue(TEXT("Warning taunt at ~1.5 s"), WarnAt >= 1.45f && WarnAt <= 1.65f);
		TestTrue(TEXT("Eaten at ~3 s"), EatAt >= 2.95f && EatAt <= 3.15f);
	}

	// Standing still but slowly moving (60 cm/s), in a menu, or far away: not eaten.
	for (int32 Variant = 0; Variant < 3; ++Variant)
	{
		FGFGiantTuning T4 = T;
		T4.bEatIdleTarget = true;
		TArray<FGFGiantPlayerView> P = { Player(TEXT("X"), FVector(2000, 0, 0), 1.0, 2) };
		P[0].bBusy = Variant == 1;
		if (Variant == 2)
		{
			T4.IdleRange = 1.f; // never close enough
		}
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		const FGFSafeCircles Zone13 = HomeZone(T4, P.Num());
		Sim.Begin(T4, Lines, Zone13, P, false, 13, Out);
		bool bEaten = false;
		for (int32 i = 0; i < 400; ++i)
		{
			FGFGiantStepOut O;
			Sim.Step(0.05f, P, O);
			bEaten |= !O.Eaten.IsEmpty();
			if (Variant == 0)
			{
				P[0].Location += FVector(0.f, 60.f * 0.05f, 0.f);
			}
		}
		TestFalse(*FString::Printf(TEXT("Not eaten (%s)"), Variant == 0 ? TEXT("walking at 60 cm/s") : Variant == 1 ? TEXT("in a menu") : TEXT("out of range")), bEaten);
	}

	// Standing still inside the circle is always safe.
	{
		FGFGiantTuning T5 = T;
		T5.bEatIdleTarget = true;
		TArray<FGFGiantPlayerView> P = { Player(TEXT("Home"), FVector(100, 0, 0), 1.0, 2) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		const FGFSafeCircles Zone14 = HomeZone(T5, P.Num());
		Sim.Begin(T5, Lines, Zone14, P, false, 14, Out);
		bool bEaten = false;
		for (int32 i = 0; i < 400; ++i)
		{
			FGFGiantStepOut O;
			Sim.Step(0.05f, P, O);
			bEaten |= !O.Eaten.IsEmpty();
		}
		TestFalse(TEXT("Safe inside the circle"), bEaten);
	}

	// Respawn point: just inside the edge, on the side the player was on.
	const FGFSafeCircles Home1 = HomeZone(T, 1);
	const FVector At = FGFGiantRules::RespawnPoint(T, Home1, FVector(0.f, -5000.f, 40.f));
	TestTrue(TEXT("Respawn inside safety near the edge"), Home1.IsInsideSafe(At) && FVector::Dist2D(At, FVector::ZeroVector) >= 1000.f);
	TestTrue(TEXT("Same side"), At.Y < 0.f);
	return true;
}

// {Dragger}: who turned the target's faction protection off (AGFPlayerState::LastDrag) fills lines next to {Target}.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantDraggerTokenTest, "GFGame.Giant.DraggerToken", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantDraggerTokenTest::RunTest(const FString&)
{
	using namespace GFGiantTest;
	FGFGiantLines Custom;
	Custom.ByPhase.Add(EGFGiantLinePhase::Spotted, { { TEXT("S1"), TEXT("{Dragger} left {Target} out here") } });
	Custom.ByPhase.Add(EGFGiantLinePhase::Return, { { TEXT("R1"), TEXT("Thanks for {Target}, {Dragger}!") } });
	FGFGiantTuning T = Quiet();
	T.bEatIdleTarget = false;
	TArray<FGFGiantPlayerView> P = { Player(TEXT("K"), FVector(3000, 0, 0), 1.0, 1) };
	P[0].Name = TEXT("Pip");
	P[0].Dragger = TEXT("Rex");
	P[0].DraggedAtUtcTicks = 1;
	{
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		const FGFSafeCircles Zone = HomeZone(T, P.Num());
		Sim.Begin(T, Custom, Zone, P, false, 2, Out);
		TestEqual(TEXT("Opening line names the dragger and the target"), Out.Chat.Num() > 0 ? Out.Chat[0].Text : FString(), FString(TEXT("Rex left Pip out here")));
		FGFGiantSim Back;
		FGFGiantStepOut BackOut;
		Back.Begin(T, Custom, Zone, P, true, 2, BackOut);
		TestEqual(TEXT("Return line names both"), BackOut.Chat.Num() > 0 ? BackOut.Chat[0].Text : FString(), FString(TEXT("Thanks for Pip, Rex!")));
	}
	P[0].Dragger.Reset();
	{
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		const FGFSafeCircles Zone = HomeZone(T, P.Num());
		Sim.Begin(T, Custom, Zone, P, false, 2, Out);
		TestEqual(TEXT("Nobody dragged them: the line still reads"), Out.Chat.Num() > 0 ? Out.Chat[0].Text : FString(), FString(TEXT("someone left Pip out here")));
		FGFGiantSim Back;
		FGFGiantStepOut BackOut;
		Back.Begin(T, Custom, Zone, P, true, 2, BackOut);
		TestEqual(TEXT("Comma lead-in dropped"), BackOut.Chat.Num() > 0 ? BackOut.Chat[0].Text : FString(), FString(TEXT("Thanks for Pip!")));
	}
	return true;
}

#endif
