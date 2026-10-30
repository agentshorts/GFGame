// Big Grumble dig planner, no world and no voxel edits: a flat run opens a tunnel, a 30-degree descent
// stays inside the step-height limit, a vertical request clamps, and an untamed Grumble removes nothing.
#include "../GFGrumbleDig.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFGrumbleDigTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static FGFGrumbleDigContext TamedGrumble()
	{
		FGFGrumbleDigContext Context;
		Context.TameState = EGFTameState::Tamed;
		Context.Species = FGFGrumbleDig::SpeciesId;
		Context.bOwnerIsHost = true;
		Context.OwnerKey = TEXT("Host");
		return Context;
	}

	static int32 YieldCount(const TArray<FGFItemStack>& Yields)
	{
		int32 N = 0;
		for (const FGFItemStack& Stack : Yields)
		{
			N += Stack.Count;
		}
		return N;
	}

	/** Min and max Z of removed cells at one Y, keyed by X. */
	static TMap<int32, FIntPoint> Columns(const TArray<FIntVector>& Cells, int32 Y)
	{
		TMap<int32, FIntPoint> Out;
		for (const FIntVector& Cell : Cells)
		{
			if (Cell.Y != Y)
			{
				continue;
			}
			if (FIntPoint* Range = Out.Find(Cell.X))
			{
				Range->X = FMath::Min(Range->X, Cell.Z);
				Range->Y = FMath::Max(Range->Y, Cell.Z);
			}
			else
			{
				Out.Add(Cell.X, FIntPoint(Cell.Z, Cell.Z));
			}
		}
		return Out;
	}

	static bool HasCell(const TArray<FIntVector>& Cells, int32 X, int32 Y, int32 Z)
	{
		return Cells.Contains(FIntVector(X, Y, Z));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGrumbleDigFlatTunnelTest, "GFGame.GrumbleDig.FlatTunnel", GFGrumbleDigTests::Flags)
bool FGFGrumbleDigFlatTunnelTest::RunTest(const FString&)
{
	using namespace GFGrumbleDigTests;
	FGFGrumbleDigTuning Tuning;
	Tuning.CapsuleHeightCm = 3800.f;
	Tuning.CapsuleRadiusCm = 200.f;
	Tuning.CellSizeCm = GFBlockSize;

	FGFGrumbleDigMove Move;
	Move.Position = FVector::ZeroVector;
	Move.Velocity = FVector(500.f, 0.f, 0.f);
	Move.PitchDegrees = 0.f;
	Move.DeltaSeconds = 2.f;

	const FGFGrumbleDigResult Dug = FGFGrumbleDig::Plan(Move, TamedGrumble(), Tuning);
	TestTrue(TEXT("Tamed Grumble digs"), Dug.bDug);
	TestTrue(TEXT("No skip"), Dug.SkipReason.IsNone());
	TestEqual(TEXT("Flat pitch stays flat"), Dug.AppliedPitchDegrees, 0.f);
	TestEqual(TEXT("No riser on a flat run"), Dug.StepRiserCm, 0.f);
	TestTrue(TEXT("Cells removed"), Dug.Cells.Num() > 0);
	TestEqual(TEXT("One yield stack"), Dug.Yields.Num(), 1);
	if (Dug.Yields.Num() == 1)
	{
		TestEqual(TEXT("Fallback drop is Stone"), Dug.Yields[0].ItemId, FName(TEXT("Stone")));
	}
	TestEqual(TEXT("One yield per carved cell"), YieldCount(Dug.Yields), Dug.Cells.Num());

	bool bSorted = true;
	for (int32 i = 1; i < Dug.Cells.Num(); ++i)
	{
		const FIntVector& A = Dug.Cells[i - 1];
		const FIntVector& B = Dug.Cells[i];
		const bool bOrder = A.X < B.X || (A.X == B.X && A.Y < B.Y) || (A.X == B.X && A.Y == B.Y && A.Z < B.Z);
		bSorted = bSorted && bOrder && A != B;
	}
	TestTrue(TEXT("Cells are unique and ordered"), bSorted);

	// The pole runs Travel cm along +X. Interior cells are the ones it crosses; the capsule is 38 blocks tall.
	const float Travel = Move.Velocity.Size() * Move.DeltaSeconds;
	const int32 LastInterior = FMath::FloorToInt((Travel - 1.f) / GFBlockSize);
	const int32 Tall = FMath::RoundToInt(Tuning.CapsuleHeightCm / GFBlockSize);
	for (int32 X = 0; X <= LastInterior; ++X)
	{
		TestTrue(*FString::Printf(TEXT("Tunnel floor at X=%d"), X), HasCell(Dug.Cells, X, 0, 0));
		TestTrue(*FString::Printf(TEXT("Tunnel ceiling at X=%d"), X), HasCell(Dug.Cells, X, 0, Tall - 1));
		TestFalse(*FString::Printf(TEXT("No cell under the feet at X=%d"), X), HasCell(Dug.Cells, X, 0, -1));
		TestFalse(*FString::Printf(TEXT("No cell above the capsule at X=%d"), X), HasCell(Dug.Cells, X, 0, Tall));
		const TMap<int32, FIntPoint> Cols = Columns(Dug.Cells, 0);
		const FIntPoint* Range = Cols.Find(X);
		TestTrue(*FString::Printf(TEXT("Center column X=%d"), X), Range != nullptr);
		if (Range)
		{
			TestEqual(*FString::Printf(TEXT("Column X=%d is the capsule"), X), Range->Y - Range->X + 1, Tall);
		}
	}
	TestFalse(TEXT("Outside the radius stays"), HasCell(Dug.Cells, 5, 5, 19));

	// A known air cell adds no yield; a known solid cell yields its drop. The carve list is unchanged.
	TMap<FIntVector, FName> Blocks;
	Blocks.Add(FIntVector(5, 0, 0), FName(TEXT("Dirt")));
	Blocks.Add(FIntVector(5, 0, 1), NAME_None);
	TMap<FName, FName> Drops;
	Drops.Add(FName(TEXT("Dirt")), FName(TEXT("Dirt")));
	const FGFGrumbleDigResult Mapped = FGFGrumbleDig::Plan(Move, TamedGrumble(), Tuning, &Blocks, &Drops);
	TestEqual(TEXT("Block map does not change the carve"), Mapped.Cells.Num(), Dug.Cells.Num());
	TestEqual(TEXT("Only the solid cell yields"), YieldCount(Mapped.Yields), 1);
	if (Mapped.Yields.Num() == 1)
	{
		TestEqual(TEXT("Drop is the block's item"), Mapped.Yields[0].ItemId, FName(TEXT("Dirt")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGrumbleDigDescentTest, "GFGame.GrumbleDig.DescentSteps", GFGrumbleDigTests::Flags)
bool FGFGrumbleDigDescentTest::RunTest(const FString&)
{
	using namespace GFGrumbleDigTests;
	FGFGrumbleDigTuning Tuning;
	Tuning.CapsuleHeightCm = 3800.f;
	Tuning.CapsuleRadiusCm = 150.f;
	Tuning.MaxPitchDegrees = 45.f;
	Tuning.MaxStepHeightCm = 200.f;
	Tuning.StepWidthCm = 400.f;
	Tuning.CellSizeCm = GFBlockSize;

	FGFGrumbleDigMove Move;
	Move.Position = FVector::ZeroVector;
	Move.Velocity = FVector(600.f, 0.f, 0.f);
	Move.PitchDegrees = -30.f;
	Move.DeltaSeconds = 5.f;

	const FGFGrumbleDigResult Dug = FGFGrumbleDig::Plan(Move, TamedGrumble(), Tuning);
	TestTrue(TEXT("Descent digs"), Dug.bDug);
	TestEqual(TEXT("30 degrees is under the max pitch"), Dug.AppliedPitchDegrees, -30.f, 0.01f);
	TestTrue(TEXT("Riser fits the step limit"), Dug.StepRiserCm > 0.f && Dug.StepRiserCm <= Tuning.MaxStepHeightCm + 0.01f);
	TestTrue(TEXT("Tread is at least one cell"), Dug.StepTreadCm + 0.01f >= GFBlockSize);

	const float Travel = Move.Velocity.Size() * Move.DeltaSeconds;
	const float Horiz = Travel * FMath::Cos(FMath::DegreesToRadians(30.f));
	const int32 X1 = FMath::FloorToInt((Horiz - 1.f) / GFBlockSize);
	const TMap<int32, FIntPoint> Cols = Columns(Dug.Cells, 0);
	const int32 MaxRiserCells = FMath::FloorToInt((Tuning.MaxStepHeightCm + 0.001f) / GFBlockSize);
	// A shaft would stack the whole travel under one cell. Overlapping the 38 m body across a few treads stays well under that.
	const int32 ShaftCells = FMath::RoundToInt(Tuning.CapsuleHeightCm / GFBlockSize) + FMath::FloorToInt(Travel / GFBlockSize);

	int32 Longest = 1;
	int32 Run = 1;
	int32 Levels = 0;
	int32 PrevFloor = 0;
	bool bHavePrev = false;
	for (int32 X = 0; X <= X1; ++X)
	{
		const FIntPoint* Range = Cols.Find(X);
		TestTrue(*FString::Printf(TEXT("Stair column X=%d"), X), Range != nullptr);
		if (!Range)
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("Column X=%d is not a shaft"), X), Range->Y - Range->X + 1 < ShaftCells);
		if (bHavePrev)
		{
			const int32 Drop = PrevFloor - Range->X;
			TestTrue(*FString::Printf(TEXT("Step at X=%d stays a stair"), X), Drop >= 0 && Drop <= MaxRiserCells);
			TestTrue(*FString::Printf(TEXT("Step at X=%d <= max step height"), X), Drop * GFBlockSize <= Tuning.MaxStepHeightCm + 0.01f);
			if (Drop == 0)
			{
				++Run;
				Longest = FMath::Max(Longest, Run);
			}
			else
			{
				Run = 1;
				++Levels;
			}
		}
		PrevFloor = Range->X;
		bHavePrev = true;
	}
	const FIntPoint* First = Cols.Find(0);
	const FIntPoint* Last = Cols.Find(X1);
	TestTrue(TEXT("Ends exist"), First && Last);
	if (First && Last)
	{
		TestTrue(TEXT("The run ends lower than it starts"), Last->X < First->X);
	}
	TestTrue(TEXT("At least one step"), Levels >= 1);
	TestTrue(TEXT("Treads are flat for more than one cell"), Longest >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGrumbleDigVerticalClampTest, "GFGame.GrumbleDig.VerticalClamp", GFGrumbleDigTests::Flags)
bool FGFGrumbleDigVerticalClampTest::RunTest(const FString&)
{
	using namespace GFGrumbleDigTests;
	FGFGrumbleDigTuning Tuning;
	Tuning.CapsuleHeightCm = 3800.f;
	Tuning.CapsuleRadiusCm = 150.f;
	Tuning.MaxPitchDegrees = 40.f;
	Tuning.MaxStepHeightCm = GFBlockSize;
	Tuning.StepWidthCm = 200.f;
	Tuning.CellSizeCm = GFBlockSize;

	FGFGrumbleDigMove Move;
	Move.Position = FVector::ZeroVector;
	Move.Velocity = FVector(0.f, 0.f, -1000.f);
	Move.PitchDegrees = -90.f;
	Move.FacingYawDegrees = 0.f;
	Move.DeltaSeconds = 2.f;

	TestEqual(TEXT("Clamp helper"), FGFGrumbleDig::ClampPitch(-90.f, Tuning), -40.f, 0.01f);

	const FGFGrumbleDigResult Dug = FGFGrumbleDig::Plan(Move, TamedGrumble(), Tuning);
	TestTrue(TEXT("Clamped dig still digs"), Dug.bDug);
	TestEqual(TEXT("Vertical request clamps to the max angle"), Dug.AppliedPitchDegrees, -Tuning.MaxPitchDegrees, 0.01f);
	TestTrue(TEXT("Riser stays walkable"), Dug.StepRiserCm > 0.f && Dug.StepRiserCm <= Tuning.MaxStepHeightCm + 0.01f);
	TestTrue(TEXT("Tread advances"), Dug.StepTreadCm + 0.01f >= GFBlockSize);

	const float Travel = Move.Velocity.Size() * Move.DeltaSeconds;
	const float Horiz = Travel * FMath::Cos(FMath::DegreesToRadians(Tuning.MaxPitchDegrees));
	const int32 X1 = FMath::FloorToInt((Horiz - 1.f) / GFBlockSize);
	const int32 PitSpan = FMath::CeilToInt((Tuning.CapsuleRadiusCm * 2.f) / GFBlockSize);
	TestTrue(TEXT("Clamped dig runs forward, past a body-width pit"), X1 > PitSpan);

	const TMap<int32, FIntPoint> Cols = Columns(Dug.Cells, 0);
	const int32 MaxRiserCells = 1;
	const int32 ShaftCells = FMath::RoundToInt(Tuning.CapsuleHeightCm / GFBlockSize) + FMath::FloorToInt(Travel / GFBlockSize);
	int32 PrevFloor = 0;
	bool bHavePrev = false;
	for (int32 X = 0; X <= X1; ++X)
	{
		const FIntPoint* Range = Cols.Find(X);
		TestTrue(*FString::Printf(TEXT("Forward column X=%d"), X), Range != nullptr);
		if (!Range)
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("Column X=%d is not a shaft"), X), Range->Y - Range->X + 1 < ShaftCells);
		if (bHavePrev)
		{
			const int32 Drop = PrevFloor - Range->X;
			TestTrue(*FString::Printf(TEXT("Drop at X=%d <= one step"), X), Drop >= 0 && Drop <= MaxRiserCells);
		}
		PrevFloor = Range->X;
		bHavePrev = true;
	}
	const FIntPoint* First = Cols.Find(0);
	const FIntPoint* Last = Cols.Find(X1);
	TestTrue(TEXT("Ends exist"), First && Last);
	if (First && Last)
	{
		TestTrue(TEXT("It descends along the stair"), Last->X < First->X);
		TestTrue(TEXT("The drop is spread out, not a pit"), (First->X - Last->X) * GFBlockSize + 0.01f <= Horiz * FMath::Tan(FMath::DegreesToRadians(Tuning.MaxPitchDegrees)) + Tuning.MaxStepHeightCm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGrumbleDigUntamedTest, "GFGame.GrumbleDig.Untamed", GFGrumbleDigTests::Flags)
bool FGFGrumbleDigUntamedTest::RunTest(const FString&)
{
	using namespace GFGrumbleDigTests;
	FGFGrumbleDigMove Move;
	Move.Position = FVector::ZeroVector;
	Move.Velocity = FVector(500.f, 0.f, 0.f);
	Move.PitchDegrees = 0.f;
	Move.DeltaSeconds = 2.f;

	auto Refuses = [&](EGFTameState State, const TCHAR* Why)
	{
		FGFGrumbleDigContext Context;
		Context.TameState = State;
		Context.Species = FGFGrumbleDig::SpeciesId;
		Context.DisplayName = TEXT("Big Grumble");
		const FGFGrumbleDigResult Dug = FGFGrumbleDig::Plan(Move, Context);
		TestFalse(*FString::Printf(TEXT("%s does not dig"), Why), Dug.bDug);
		TestEqual(*FString::Printf(TEXT("%s removes nothing"), Why), Dug.Cells.Num(), 0);
		TestEqual(*FString::Printf(TEXT("%s yields nothing"), Why), Dug.Yields.Num(), 0);
		TestEqual(*FString::Printf(TEXT("%s skip"), Why), Dug.SkipReason, GFGrumbleDigSkip::Untamed);
	};
	Refuses(EGFTameState::Wild, TEXT("Wild"));
	Refuses(EGFTameState::Threat, TEXT("Threat"));
	Refuses(EGFTameState::Hostile, TEXT("Hostile"));
	Refuses(EGFTameState::Feeding, TEXT("Feeding"));

	FGFGrumbleDigContext Other;
	Other.TameState = EGFTameState::Tamed;
	Other.Species = TEXT("Raptor");
	const FGFGrumbleDigResult Raptor = FGFGrumbleDig::Plan(Move, Other);
	TestFalse(TEXT("A tamed raptor is not a Grumble"), Raptor.bDug);
	TestEqual(TEXT("Raptor skip"), Raptor.SkipReason, GFGrumbleDigSkip::NotGrumble);

	const FGFGrumbleDigResult Tamed = FGFGrumbleDig::Plan(Move, TamedGrumble());
	TestTrue(TEXT("The same move digs once tamed"), Tamed.bDug);
	TestTrue(TEXT("Display name Big Grumble counts"), FGFGrumbleDig::IsBigGrumble(NAME_None, TEXT("Big Grumble")));
	return true;
}

#endif
