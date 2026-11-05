// Acceleration Hop tutorial (#348) tests: the side gate and its map name, the hub portal beside Beat Hop, the tutorial
// map's marker and two HUB gates, the lesson order and prompt copy, lesson progress, and lit opt-out tiles.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.AccelHop.Tutorial;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFAccelHop.h"
#include "GFAccelHopTutorial.h"
#include "GFAccelHopTutorialGateSubsystem.h"
#include "GFAccelHopTutorialRules.h"
#include "GFHubGateDirector.h"
#include "GFMiniGameCatalog.h"
#include "GFMiniGameGate.h"
#include "GFMovementPolicy.h"
#include "GFReturnToHub.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFAccelHopTutorialTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FWorldScope
	{
		UWorld* World = nullptr;

		explicit FWorldScope(const TCHAR* Name)
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FWorldScope()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static AGFMiniGameGate* FindGate(UWorld* World, FName Id, EGFMiniGameGateRole Role)
	{
		for (TActorIterator<AGFMiniGameGate> It(World); It; ++It)
		{
			if (It->EntryId == Id && It->GateRole == Role)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static int32 CountReturnGates(UWorld* World, FName Id)
	{
		int32 Count = 0;
		for (TActorIterator<AGFReturnGate> It(World); It; ++It)
		{
			Count += It->GetModeId() == Id ? 1 : 0;
		}
		return Count;
	}

	static FGFAccelHopTutorialLanding Landing(int32 Lesson, EGFJudgement Judgement, int32 Side = 0, bool bLit = false)
	{
		FGFAccelHopTutorialLanding Out;
		Out.LessonIndex = Lesson;
		Out.Judgement = Judgement;
		Out.Side = Side;
		Out.bLit = bLit;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopTutorialGateTest, "GFGame.AccelHop.Tutorial.Gate", GFAccelHopTutorialTests::Flags)
bool FGFAccelHopTutorialGateTest::RunTest(const FString&)
{
	const FName Id = FGFMiniGameCatalog::AccelHopTutorialId();
	TestEqual(TEXT("Gate id"), Id, FName(TEXT("AccelHopTutorial")));
	TestEqual(TEXT("Versioned map"), FGFMiniGameCatalog::AccelHopTutorialMap(), FName(TEXT("/Game/Maps/L_AccelHop_Tutorial_v2")));
	TestTrue(TEXT("Never L_Beat"), FGFMiniGameCatalog::AccelHopTutorialMap() != FName(TEXT("/Game/Maps/L_Beat")));

	const UGFAccelHopTutorialGateSubsystem* Gate = GetDefault<UGFAccelHopTutorialGateSubsystem>();
	TestEqual(TEXT("Subsystem gate id"), Gate->GetGateId(), Id);
	TestEqual(TEXT("Subsystem map"), Gate->GetMapName(), FGFMiniGameCatalog::AccelHopTutorialMap());

	TestEqual(TEXT("Ring stays at 8"), FGFMiniGameCatalog::All().Num(), 8);
	const FGFMiniGameEntry* Entry = FGFMiniGameCatalog::Find(Id);
	if (!TestNotNull(TEXT("Catalog finds the side gate"), Entry))
	{
		return false;
	}
	TestEqual(TEXT("One side gate"), FGFMiniGameCatalog::Extras().Num(), 1);
	TestEqual(TEXT("Catalog map"), Entry->Map, FGFMiniGameCatalog::AccelHopTutorialMap());
	TestEqual(TEXT("Catalog label"), Entry->Label, FString(TEXT("Accel Hop 101")));
	TestEqual(TEXT("Beside Beat Hop"), Entry->BesideId, FName(TEXT("BeatHop")));
	TestEqual(TEXT("Placed class"), Entry->PlacedClass, FString(TEXT("/Script/GFGame.GFAccelHopTutorial")));
	TestFalse(TEXT("Not on the ring"), FGFMiniGameCatalog::All().ContainsByPredicate([Id](const FGFMiniGameEntry& Row) { return Row.Id == Id; }));
	TestTrue(TEXT("Tutorial map is a mini-game map (stamina off unless opted in)"), GFMovementPolicy::IsMiniGameMapName(TEXT("/Game/Maps/L_AccelHop_Tutorial_v2")));

	const FGFMiniGameEntry* Beat = FGFMiniGameCatalog::Find(TEXT("BeatHop"));
	if (!TestNotNull(TEXT("Beat Hop row"), Beat))
	{
		return false;
	}
	const FVector Rime(1000.f, -2000.f, 50.f);
	FVector At;
	FRotator Facing;
	TestTrue(TEXT("Side gate transform"), FGFMiniGameCatalog::ExtraGateTransform(*Entry, Rime, At, Facing));
	const FVector BeatAt = FGFMiniGameCatalog::GateLocation(Rime, Beat->RingIndex, FGFMiniGameCatalog::All().Num());
	TestTrue(TEXT("Side gate faces like Beat Hop"), Facing.Equals(FGFMiniGameCatalog::GateFacing(BeatAt, Rime), 0.01f));
	TestTrue(TEXT("750 cm beside Beat Hop"), FMath::IsNearlyEqual(FVector::Dist(At, BeatAt), FGFMiniGameCatalog::AccelHopTutorialBesideCm, 1.f));
	for (const FGFMiniGameEntry& Ring : FGFMiniGameCatalog::All())
	{
		const FVector RingAt = FGFMiniGameCatalog::GateLocation(Rime, Ring.RingIndex, FGFMiniGameCatalog::All().Num());
		TestTrue(*FString::Printf(TEXT("Clear of %s"), *Ring.Id.ToString()), FVector::Dist(At, RingAt) > 500.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopTutorialHubTest, "GFGame.AccelHop.Tutorial.Hub", GFAccelHopTutorialTests::Flags)
bool FGFAccelHopTutorialHubTest::RunTest(const FString&)
{
	using namespace GFAccelHopTutorialTests;
	const FName Id = FGFMiniGameCatalog::AccelHopTutorialId();
	const FVector Rime = FVector::ZeroVector;
	{
		FWorldScope Hub(TEXT("GFTutorialHub"));
		if (!TestNotNull(TEXT("Hub world"), Hub.World))
		{
			return false;
		}
		AGFHubGateDirector* Director = Hub.World->SpawnActor<AGFHubGateDirector>();
		Director->Place(TEXT("/Game/Maps/L_Hub"), Rime);
		Director->Place(TEXT("/Game/Maps/L_Hub"), Rime);
		AGFMiniGameGate* Portal = FindGate(Hub.World, Id, EGFMiniGameGateRole::Portal);
		if (TestNotNull(TEXT("Hub portal"), Portal))
		{
			FVector At;
			FRotator Facing;
			FGFMiniGameCatalog::ExtraGateTransform(*FGFMiniGameCatalog::Find(Id), Rime, At, Facing);
			TestTrue(TEXT("Portal beside Beat Hop"), Portal->GetActorLocation().Equals(At, 1.f));
			TestEqual(TEXT("Portal label"), Portal->GetLabel(), FString(TEXT("Accel Hop 101")));
		}
		TestNull(TEXT("No tutorial director on the hub"), AGFAccelHopTutorial::Find(Hub.World));
	}
	{
		FWorldScope Map(TEXT("GFTutorialMap"));
		if (!TestNotNull(TEXT("Tutorial world"), Map.World))
		{
			return false;
		}
		// The map script places the director with the finish set; stand in for it.
		AGFAccelHopTutorial* Placed = Map.World->SpawnActor<AGFAccelHopTutorial>();
		Placed->bHasFinishGate = true;
		Placed->FinishGateLocation = FVector(9000.f, 0.f, 0.f);
		AGFHubGateDirector* Director = Map.World->SpawnActor<AGFHubGateDirector>();
		Director->Place(TEXT("/Game/Maps/L_AccelHop_Tutorial_v2"), Rime);
		Director->Place(TEXT("/Game/Maps/UEDPIE_0_L_AccelHop_Tutorial_v2"), Rime);
		TestNotNull(TEXT("Tutorial marker"), FindGate(Map.World, Id, EGFMiniGameGateRole::Marker));
		TestTrue(TEXT("Director reused, not doubled"), AGFAccelHopTutorial::Find(Map.World) == Placed);
		int32 Directors = 0;
		for (TActorIterator<AGFAccelHopTutorial> It(Map.World); It; ++It)
		{
			++Directors;
		}
		TestEqual(TEXT("One director"), Directors, 1);
		TestEqual(TEXT("Start and finish HUB gates"), CountReturnGates(Map.World, Id), 2);
		AGFReturnGate* Home = AGFReturnGate::Find(Map.World, Id);
		if (TestNotNull(TEXT("Canonical return gate"), Home))
		{
			TestFalse(TEXT("Canonical gate is the untagged one"), Home->ActorHasTag(AGFAccelHopTutorial::FinishGateTag()));
			TestTrue(TEXT("Start gate at MapLocation"), Home->GetActorLocation().Equals(AGFReturnGate::MapLocation(), 1.f));
			TestTrue(TEXT("Map travel"), Home->IsMapTravel());
		}
		bool bFinish = false;
		for (TActorIterator<AGFReturnGate> It(Map.World); It; ++It)
		{
			if (It->ActorHasTag(AGFAccelHopTutorial::FinishGateTag()))
			{
				bFinish = true;
				TestTrue(TEXT("Finish gate where the script put it"), It->GetActorLocation().Equals(FVector(9000.f, 0.f, 0.f), 1.f));
				TestTrue(TEXT("Finish gate travels home"), It->IsMapTravel());
			}
		}
		TestTrue(TEXT("Finish gate"), bFinish);
	}
	{
		FWorldScope Beat(TEXT("GFTutorialBeat"));
		AGFHubGateDirector* Director = Beat.World ? Beat.World->SpawnActor<AGFHubGateDirector>() : nullptr;
		if (TestNotNull(TEXT("Beat director"), Director))
		{
			Director->Place(TEXT("/Game/Maps/L_Beat"), Rime);
			TestNull(TEXT("L_Beat gets no tutorial"), AGFAccelHopTutorial::Find(Beat.World));
			TestEqual(TEXT("L_Beat gets no tutorial gate"), CountReturnGates(Beat.World, Id), 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopTutorialPromptTest, "GFGame.AccelHop.Tutorial.Prompts", GFAccelHopTutorialTests::Flags)
bool FGFAccelHopTutorialPromptTest::RunTest(const FString&)
{
	const TArray<FGFAccelHopLessonSpec>& Lessons = FGFAccelHopTutorialRules::Lessons();
	if (!TestEqual(TEXT("Four lessons"), Lessons.Num(), FGFAccelHopTutorialRules::LessonCount))
	{
		return false;
	}
	const EGFAccelHopLesson Order[] = {EGFAccelHopLesson::Beat, EGFAccelHopLesson::Steer, EGFAccelHopLesson::Lit, EGFAccelHopLesson::Chain};
	const TCHAR* Ids[] = {TEXT("Beat"), TEXT("Steer"), TEXT("Lit"), TEXT("Chain")};
	const TCHAR* Titles[] = {TEXT("Hop on the beat"), TEXT("Steer in the air"), TEXT("Hit the lit tile"), TEXT("Put it all together")};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestTrue(*FString::Printf(TEXT("Lesson %d order"), Index), Lessons[Index].Lesson == Order[Index]);
		TestEqual(*FString::Printf(TEXT("Lesson %d id"), Index), Lessons[Index].Id, FName(Ids[Index]));
		TestEqual(*FString::Printf(TEXT("Lesson %d title"), Index), Lessons[Index].Title, FString(Titles[Index]));
		TestEqual(*FString::Printf(TEXT("Lesson %d tag"), Index), FGFAccelHopTutorialRules::LessonTag(Index), FName(*FString::Printf(TEXT("GFAccelHopTutorial.Lesson.%s"), Ids[Index])));
		TestEqual(*FString::Printf(TEXT("Lesson %d from tag"), Index), FGFAccelHopTutorialRules::LessonFromTags({TEXT("GFAccelHopTutorial"), FGFAccelHopTutorialRules::LessonTag(Index)}), Index);
		TestFalse(*FString::Printf(TEXT("Lesson %d body has no em dash"), Index), Lessons[Index].Body.Contains(TEXT("\u2014")));
	}
	TestEqual(TEXT("No tag no lesson"), FGFAccelHopTutorialRules::LessonFromTags({TEXT("GFAccelHopTutorial")}), INDEX_NONE);
	TestTrue(TEXT("Bad index tag"), FGFAccelHopTutorialRules::LessonTag(4).IsNone());

	FGFAccelHopTutorialProgress Progress;
	FGFAccelHopTutorialPrompt Prompt = FGFAccelHopTutorialRules::PromptFor(Progress);
	TestEqual(TEXT("Step 1"), Prompt.Step, FString(TEXT("1/4")));
	TestEqual(TEXT("Goal 1"), Prompt.Goal, FString(TEXT("On-beat landings 0/4")));
	TestFalse(TEXT("Not done"), Prompt.bDone);
	Progress.Lesson = 1;
	TestEqual(TEXT("Goal 2"), FGFAccelHopTutorialRules::PromptFor(Progress).Goal, FString(TEXT("Zigzag hops 0/3")));
	Progress.Lesson = 2;
	TestEqual(TEXT("Goal 3"), FGFAccelHopTutorialRules::PromptFor(Progress).Goal, FString(TEXT("Lit landings 0/2")));
	Progress.Lesson = 3;
	Progress.BestChain = 9;
	TestEqual(TEXT("Goal 4 clamps"), FGFAccelHopTutorialRules::PromptFor(Progress).Goal, FString(TEXT("Chain 6/6   Zigzag 0/1   Lit 0/1")));
	Progress.Lesson = 4;
	Prompt = FGFAccelHopTutorialRules::PromptFor(Progress);
	TestTrue(TEXT("Done"), Prompt.bDone);
	TestEqual(TEXT("Done title"), Prompt.Title, FString(TEXT("Hop certified")));

	const TArray<FString> Lines = FGFAccelHopTutorialRules::PromptLines(FGFAccelHopTutorialRules::PromptFor(FGFAccelHopTutorialProgress()), false);
	if (TestEqual(TEXT("Three lines"), Lines.Num(), 3))
	{
		TestEqual(TEXT("Header"), Lines[0], FString(TEXT("1/4  Hop on the beat")));
	}
	const TArray<FString> Flash = FGFAccelHopTutorialRules::PromptLines(FGFAccelHopTutorialRules::PromptFor(FGFAccelHopTutorialProgress()), true);
	TestTrue(TEXT("Flash says Nice!"), Flash[0].StartsWith(TEXT("Nice!  ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopTutorialProgressTest, "GFGame.AccelHop.Tutorial.Progress", GFAccelHopTutorialTests::Flags)
bool FGFAccelHopTutorialProgressTest::RunTest(const FString&)
{
	using namespace GFAccelHopTutorialTests;
	using R = FGFAccelHopTutorialRules;
	FGFAccelHopTutorialProgress P;

	// 1: four in-time landings, misses do not count, not required in a row.
	TestFalse(TEXT("Miss"), R::OnLanding(P, Landing(0, EGFJudgement::Miss)));
	R::OnLanding(P, Landing(0, EGFJudgement::Perfect));
	R::OnLanding(P, Landing(0, EGFJudgement::Miss));
	R::OnLanding(P, Landing(0, EGFJudgement::Good));
	R::OnLanding(P, Landing(0, EGFJudgement::Good));
	TestEqual(TEXT("Three counted"), P.Count, 3);
	TestFalse(TEXT("Non-tutorial tile"), R::OnLanding(P, Landing(INDEX_NONE, EGFJudgement::Perfect)));
	TestTrue(TEXT("Beat done"), R::OnLanding(P, Landing(0, EGFJudgement::Perfect)));
	TestEqual(TEXT("On to Steer"), P.Lesson, 1);
	TestEqual(TEXT("Counter reset"), P.Count, 0);
	TestEqual(TEXT("Serial bumped"), P.Serial, 1);

	// Walking back onto lesson 1 tiles does nothing.
	TestFalse(TEXT("No rewind"), R::OnLanding(P, Landing(0, EGFJudgement::Perfect)));
	TestEqual(TEXT("Still Steer"), P.Lesson, 1);

	// 2: three side switches, any judgement; same side or centre does not count.
	R::OnLanding(P, Landing(1, EGFJudgement::Miss, -1));
	R::OnLanding(P, Landing(1, EGFJudgement::Good, -1));
	R::OnLanding(P, Landing(1, EGFJudgement::Good, 0));
	TestEqual(TEXT("No switch yet"), P.Count, 0);
	R::OnLanding(P, Landing(1, EGFJudgement::Miss, 1));
	R::OnLanding(P, Landing(1, EGFJudgement::Good, -1));
	TestTrue(TEXT("Steer done"), R::OnLanding(P, Landing(1, EGFJudgement::Perfect, 1)));
	TestEqual(TEXT("On to Lit"), P.Lesson, 2);

	// 3: two lit landings.
	R::OnLanding(P, Landing(2, EGFJudgement::Perfect));
	R::OnLanding(P, Landing(2, EGFJudgement::Good, 0, true));
	TestEqual(TEXT("One lit"), P.Count, 1);
	TestTrue(TEXT("Lit done"), R::OnLanding(P, Landing(2, EGFJudgement::Good, 0, true)));
	TestEqual(TEXT("On to Chain"), P.Lesson, 3);

	// 4: best chain 6 in time (a miss resets), plus a zigzag and a lit landing.
	for (int32 Index = 0; Index < 5; ++Index)
	{
		R::OnLanding(P, Landing(3, EGFJudgement::Good));
	}
	R::OnLanding(P, Landing(3, EGFJudgement::Miss));
	TestEqual(TEXT("Chain reset"), P.Chain, 0);
	TestEqual(TEXT("Best kept"), P.BestChain, 5);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		R::OnLanding(P, Landing(3, EGFJudgement::Perfect));
	}
	TestEqual(TEXT("Chain 6"), P.BestChain, 6);
	TestEqual(TEXT("Still needs zig and lit"), P.Lesson, 3);
	R::OnLanding(P, Landing(3, EGFJudgement::Good, -1));
	R::OnLanding(P, Landing(3, EGFJudgement::Good, 1));
	TestEqual(TEXT("One zig"), P.Zigs, 1);
	TestTrue(TEXT("Chain done"), R::OnLanding(P, Landing(3, EGFJudgement::Good, 0, true)));
	TestTrue(TEXT("Done"), R::IsDone(P));
	TestFalse(TEXT("Nothing after done"), R::OnLanding(P, Landing(3, EGFJudgement::Perfect)));

	// Skipping ahead: landing on a later lesson's tile, or walking onto its stretch.
	FGFAccelHopTutorialProgress Skip;
	R::OnLanding(Skip, Landing(2, EGFJudgement::Good, 0, true));
	TestEqual(TEXT("Skipped to Lit"), Skip.Lesson, 2);
	TestEqual(TEXT("The skipping landing counts"), Skip.Count, 1);
	TestFalse(TEXT("Reach back does nothing"), R::OnReachLesson(Skip, 1));
	TestTrue(TEXT("Reach ahead"), R::OnReachLesson(Skip, 3));

	const TArray<float> Starts = {600.f, 3800.f, MAX_flt, 9000.f};
	TestEqual(TEXT("Before the course"), R::LessonAtAlong(Starts, 0.f), INDEX_NONE);
	TestEqual(TEXT("On 1"), R::LessonAtAlong(Starts, 1000.f), 0);
	TestEqual(TEXT("On 2"), R::LessonAtAlong(Starts, 5000.f), 1);
	TestEqual(TEXT("Missing lesson skipped"), R::LessonAtAlong(Starts, 9500.f), 3);

	TestEqual(TEXT("Left"), R::SideOf(-250.f), -1);
	TestEqual(TEXT("Centre"), R::SideOf(30.f), 0);
	TestEqual(TEXT("Right"), R::SideOf(250.f), 1);

	// The director keeps one progress and stamps the change time.
	FWorldScope Scope(TEXT("GFTutorialDirector"));
	if (Scope.World)
	{
		AGFAccelHopTutorial* Tutorial = Scope.World->SpawnActor<AGFAccelHopTutorial>();
		if (TestNotNull(TEXT("Director"), Tutorial))
		{
			TestTrue(TEXT("Director skip"), Tutorial->ApplyLanding(Landing(1, EGFJudgement::Good, -1)));
			TestEqual(TEXT("Director lesson"), Tutorial->GetProgress().Lesson, 1);
			TestTrue(TEXT("Flash time set"), Tutorial->GetLastAdvanceTime() > -1.0);
			TestEqual(TEXT("Director prompt"), Tutorial->GetPrompt().Step, FString(TEXT("2/4")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopTutorialLitOptOutTest, "GFGame.AccelHop.Tutorial.LitOptOut", GFAccelHopTutorialTests::Flags)
bool FGFAccelHopTutorialLitOptOutTest::RunTest(const FString&)
{
	// Lessons 1 and 2 tiles opt out of the lit pick so the magenta diamond first shows up in lesson 3.
	TArray<FGFHopTileSlot> Slots;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FGFHopTileSlot Slot;
		Slot.Along = 450.f * static_cast<float>(Index);
		Slot.Kind = EGFHopTileKind::Standard;
		Slot.bCanLight = Index >= 6;
		Slots.Add(Slot);
	}
	const FGFAccelHopLitTuning Tuning;
	for (int32 Seed = 0; Seed < 16; ++Seed)
	{
		const int32 Early = FGFAccelHopLitRules::PickLitIndex(Slots, 0.f, 1.f, INDEX_NONE, Seed, Tuning);
		TestEqual(*FString::Printf(TEXT("Seed %d: nothing in reach can light"), Seed), Early, INDEX_NONE);
		const int32 Late = FGFAccelHopLitRules::PickLitIndex(Slots, 450.f * 4.f, 1.f, INDEX_NONE, Seed, Tuning);
		TestTrue(*FString::Printf(TEXT("Seed %d: picks a tile that can light"), Seed), Slots.IsValidIndex(Late) && Slots[Late].bCanLight);
	}
	return true;
}

#endif
