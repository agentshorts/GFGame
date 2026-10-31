// Course rules (#114): harder layouts, ice skid that is only a velocity nudge, rescue catches, revenge knocks.

#include "Misc/AutomationTest.h"
#include "GFBeatHopCourses.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFBeatHopCourseRules
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopCourseCatalogTest, "GFGame.Rhythm.CourseCatalog", GFBeatHopCourseRules::Flags)
bool FGFBeatHopCourseCatalogTest::RunTest(const FString&)
{
	const TArray<FGFBeatHopCourseDef>& Courses = FGFBeatHopCourses::All();
	if (!TestEqual(TEXT("Three courses"), Courses.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("Default is Fern"), FGFBeatHopCourses::Default().Id, FName(TEXT("Fern")));
	TestTrue(TEXT("Fern is first"), Courses[0].Id == TEXT("Fern") && Courses[0].Order == 0);
	TestTrue(TEXT("Rime is the ice chase"), Courses[1].Id == TEXT("Rime") && Courses[1].Hazard == EGFBeatHopCourseHazard::Ice
		&& Courses[1].Revenge == EGFBeatHopRevengeRole::Chase && Courses[1].RevengeSpecies == TEXT("Raptor"));
	TestTrue(TEXT("Cinder is the lava knock"), Courses[2].Id == TEXT("Cinder") && Courses[2].Hazard == EGFBeatHopCourseHazard::Lava
		&& Courses[2].Revenge == EGFBeatHopRevengeRole::KnockOff && Courses[2].RevengeSpecies == TEXT("Scythepaw"));
	TestTrue(TEXT("Fern has no revenge"), Courses[0].Revenge == EGFBeatHopRevengeRole::None && !Courses[0].IsSkiddy());
	TestTrue(TEXT("Rime skids"), Courses[1].IsSkiddy() && Courses[1].SkidCmPerSec > 0.f);
	TestTrue(TEXT("Cinder stretches gaps"), Courses[2].GapScale > 1.f);
	TestNull(TEXT("Unknown course"), FGFBeatHopCourses::Find(TEXT("NoSuchCourse")));

	for (int32 i = 1; i < Courses.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("%s is later than %s"), *Courses[i].Title, *Courses[i - 1].Title), Courses[i].Order > Courses[i - 1].Order);
		TestTrue(FString::Printf(TEXT("%s is harder than %s"), *Courses[i].Title, *Courses[i - 1].Title),
			Courses[i].DifficultyScore() > Courses[i - 1].DifficultyScore());
	}

	const FVector2D Chart(1200.0, 50.0);
	const FVector2D Fern = FGFBeatHopCourses::Place(Courses[0], 0, Chart.X, Chart.Y, 100.f);
	TestEqual(TEXT("Fern keeps chart X"), Fern.X, Chart.X, 0.01);
	TestEqual(TEXT("Fern keeps chart Y"), Fern.Y, Chart.Y, 0.01);

	const FVector2D Rime0 = FGFBeatHopCourses::Place(Courses[1], 0, Chart.X, Chart.Y, 100.f);
	const FVector2D Rime4 = FGFBeatHopCourses::Place(Courses[1], 4, Chart.X, Chart.Y, 100.f);
	TestEqual(TEXT("Rime keeps X"), Rime0.X, Chart.X, 0.01);
	TestTrue(TEXT("Rime weaves off the chart lane"), !FMath::IsNearlyEqual(Rime0.Y, Chart.Y, 1.0));
	TestTrue(TEXT("Rime weave flips"), (Rime0.Y - Chart.Y) * (Rime4.Y - Chart.Y) < 0.0);

	const FVector2D Cinder = FGFBeatHopCourses::Place(Courses[2], 3, Chart.X, Chart.Y, 100.f);
	TestTrue(TEXT("Cinder is farther along X"), Cinder.X > Fern.X + 100.0);
	TestEqual(TEXT("Cinder keeps the lane"), Cinder.Y, Chart.Y, 0.01);
	const FVector2D CinderNext = FGFBeatHopCourses::Place(Courses[2], 4, Chart.X + 100.0, Chart.Y, 100.f);
	TestTrue(TEXT("Cinder still runs forward"), CinderNext.X > Cinder.X);

	const FVector Forward(600.f, 0.f, 0.f);
	const FVector Right(0.f, 1.f, 0.f);
	const FVector Skidded = FGFBeatHopCourses::ApplySkid(Forward, Right, Courses[1].SkidCmPerSec, 0.5f);
	TestEqual(TEXT("Skid keeps the forward speed"), Skidded.X, Forward.X, 0.01);
	TestTrue(TEXT("Skid adds a sideways drift"), Skidded.Y > 50.f && Skidded.Y <= Courses[1].SkidCmPerSec + 0.01f);
	TestEqual(TEXT("No skid on Fern"), FGFBeatHopCourses::ApplySkid(Forward, Right, 0.f, 0.5f).Y, 0.0, 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopRescueBookTest, "GFGame.Rhythm.RescueBook", GFBeatHopCourseRules::Flags)
bool FGFBeatHopRescueBookTest::RunTest(const FString&)
{
	TestEqual(TEXT("Rescue is a Ripwing"), FGFBeatHopRescueRules::Species(), FName(TEXT("Ripwing")));
	TestEqual(TEXT("Rescue costs GlowDust"), FGFBeatHopRescueRules::CostItem(), FName(TEXT("GlowDust")));
	TestTrue(TEXT("Rescue costs something"), FGFBeatHopRescueRules::Cost > 0 && FGFBeatHopRescueRules::Catches > 0);

	FGFBeatHopRescueBook Book;
	TestFalse(TEXT("Empty key refused"), Book.Hire(TEXT(""), 3));
	TestFalse(TEXT("No catches refused"), Book.Hire(TEXT("Solo"), 0));
	TestTrue(TEXT("Hire"), Book.Hire(TEXT("Solo"), FGFBeatHopRescueRules::Catches));
	TestFalse(TEXT("Second hire refused"), Book.Hire(TEXT("Solo"), 3));
	TestEqual(TEXT("Three catches"), Book.Find(TEXT("Solo"))->CatchesLeft, 3);
	TestTrue(TEXT("Catch"), Book.SpendCatch(TEXT("Solo")));
	TestEqual(TEXT("Two left"), Book.Find(TEXT("Solo"))->CatchesLeft, 2);
	TestFalse(TEXT("Stranger has no catch"), Book.SpendCatch(TEXT("Other")));
	TestTrue(TEXT("Catch"), Book.SpendCatch(TEXT("Solo")));
	TestTrue(TEXT("Catch"), Book.SpendCatch(TEXT("Solo")));
	TestFalse(TEXT("Spent"), Book.SpendCatch(TEXT("Solo")));
	TestEqual(TEXT("Zero"), Book.Find(TEXT("Solo"))->CatchesLeft, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopRevengeSimTest, "GFGame.Rhythm.RevengeSim", GFBeatHopCourseRules::Flags)
bool FGFBeatHopRevengeSimTest::RunTest(const FString&)
{
	const FGFBeatHopCourseDef* Fern = FGFBeatHopCourses::Find(TEXT("Fern"));
	const FGFBeatHopCourseDef* Rime = FGFBeatHopCourses::Find(TEXT("Rime"));
	const FGFBeatHopCourseDef* Cinder = FGFBeatHopCourses::Find(TEXT("Cinder"));
	if (!TestNotNull(TEXT("Fern"), Fern) || !TestNotNull(TEXT("Rime"), Rime) || !TestNotNull(TEXT("Cinder"), Cinder))
	{
		return false;
	}

	FGFBeatHopRevengeState None;
	None.Reset(*Fern, 1000.f, 0.f);
	TestFalse(TEXT("Fern spawns no revenge dino"), None.bSpawned);
	TestFalse(TEXT("Fern does not knock"), None.Step(TEXT("Solo"), FVector(1000.f, 0.f, 0.f), 1.f, *Fern).bKnocked);

	FGFBeatHopRevengeState Lava;
	Lava.Reset(*Cinder, 1000.f, 0.f);
	TestTrue(TEXT("Lava dino is posted"), Lava.bSpawned);
	TestFalse(TEXT("Far from the post, no knock"), Lava.Step(TEXT("Solo"), FVector(0.f, 0.f, 100.f), 1.f, *Cinder).bKnocked);
	const FGFBeatHopRevengeState::FStep Swat = Lava.Step(TEXT("Solo"), FVector(1000.f, 0.f, 100.f), 1.f, *Cinder);
	TestTrue(TEXT("Passing the post is a knock"), Swat.bKnocked);
	TestTrue(TEXT("The knock shoves"), !Swat.Push.IsNearlyZero());
	TestEqual(TEXT("One knock"), Lava.Knocks, 1);
	TestFalse(TEXT("The same runner is not swatted every step"), Lava.Step(TEXT("Solo"), FVector(1000.f, 0.f, 100.f), 1.f, *Cinder).bKnocked);
	TestEqual(TEXT("Still one knock"), Lava.Knocks, 1);

	FGFBeatHopRevengeState Ice;
	Ice.Reset(*Rime, 2000.f, 400.f);
	TestTrue(TEXT("Ice dino starts behind"), Ice.Local.X < 400.f);
	const float Before = FVector::Dist(Ice.Local, FVector(400.f, 0.f, 100.f));
	const FGFBeatHopRevengeState::FStep Chase = Ice.Step(TEXT("Solo"), FVector(400.f, 0.f, 100.f), 0.5f, *Rime);
	TestTrue(TEXT("Ice dino is chasing"), Chase.bChasing);
	TestFalse(TEXT("A chase from behind is not a lava knock"), Chase.bKnocked);
	TestTrue(TEXT("Chase closes"), FVector::Dist(Ice.Local, FVector(400.f, 0.f, 100.f)) < Before - 100.f);
	return true;
}

#endif
