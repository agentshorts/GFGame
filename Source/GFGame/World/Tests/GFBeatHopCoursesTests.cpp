// Beat Hop courses on the course the hub gate already places (#114). No new map.
// A win is the finish. A hired Ripwing catches a fall. Revenge dinos chase on ice and knock on lava.
// Walk speed, gravity and air control are left on the run's existing tuning.

#include "Misc/AutomationTest.h"
#include "GFBeatHopCourse.h"
#include "GFBeatHopCourses.h"
#include "GFEconomy.h"
#include "GFHubGateDirector.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFBeatHopCoursePlay
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FWorld
	{
		UWorld* World = nullptr;

		explicit FWorld(const TCHAR* Name = TEXT("GFBeatHopCoursesWorld"))
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

		~FWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static AGFBeatHopCourse* Place(UWorld* World)
	{
		AGFHubGateDirector* Director = World ? World->SpawnActor<AGFHubGateDirector>() : nullptr;
		if (!Director)
		{
			return nullptr;
		}
		Director->Place(TEXT("/Game/Maps/L_Beat"), FVector::ZeroVector);
		for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	static ACharacter* SpawnRunner(UWorld* World, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(At), Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopCoursesOnGateTest, "GFGame.BeatHop.Courses", GFBeatHopCoursePlay::Flags)
bool FGFBeatHopCoursesOnGateTest::RunTest(const FString&)
{
	using namespace GFBeatHopCoursePlay;
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = Place(W.World);
	if (!TestNotNull(TEXT("Gate director still places one course"), Course) || !TestTrue(TEXT("Session"), Course->IsSession()))
	{
		return false;
	}
	TestEqual(TEXT("Three course tiles"), Course->NumCourseTiles(), 3);
	TestEqual(TEXT("Default course is Fern"), Course->GetSelectedCourseId(), FName(TEXT("Fern")));
	TestFalse(TEXT("Unknown course refused"), Course->SelectCourse(TEXT("Nope")));
	if (!TestTrue(TEXT("Bricks"), Course->NumBricks() > 4))
	{
		return false;
	}

	const double FernX = Course->GetBrickTop(3).X;
	const double FernY = Course->GetBrickTop(0).Y;
	TestTrue(TEXT("Rime"), Course->SelectCourse(TEXT("Rime")));
	TestEqual(TEXT("Selected Rime"), Course->GetSelectedCourseId(), FName(TEXT("Rime")));
	TestTrue(TEXT("Rime weaves the first brick"), !FMath::IsNearlyEqual(Course->GetBrickTop(0).Y, FernY, 1.0));
	TestEqual(TEXT("Rime does not stretch X"), Course->GetBrickTop(3).X, FernX, 1.0);

	TestTrue(TEXT("Cinder"), Course->SelectCourse(TEXT("Cinder")));
	TestTrue(TEXT("Cinder stretches the course"), Course->GetBrickTop(3).X > FernX + 50.0);
	UBoxComponent* Finish = Course->GetFinishTrigger();
	if (TestNotNull(TEXT("Finish still exists"), Finish))
	{
		TestTrue(TEXT("Finish stays past the last brick"), Finish->GetComponentLocation().X > Course->GetBrickTop(Course->NumBricks() - 1).X);
	}
	TestTrue(TEXT("Back to Fern"), Course->SelectCourse(TEXT("Fern")));
	TestEqual(TEXT("Fern X restored"), Course->GetBrickTop(3).X, FernX, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopWinIsFinishTest, "GFGame.BeatHop.WinIsFinish", GFBeatHopCoursePlay::Flags)
bool FGFBeatHopWinIsFinishTest::RunTest(const FString&)
{
	using namespace GFBeatHopCoursePlay;
	FWorld W;
	AGFBeatHopCourse* Course = W.World ? Place(W.World) : nullptr;
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	FGFTransferLedger Ledger;
	FGFMapLedgerAccounts Accounts;
	Accounts.Accounts.Add(TEXT("Solo"));
	Course->SetLedgerOverride(&Ledger, &Accounts);
	Course->SetDebugServerTime(1000.0);
	ACharacter* Runner = SpawnRunner(W.World, Course->GetStartTrigger() ? Course->GetStartTrigger()->GetComponentLocation() : Course->GetLobbySpawn());
	FString Why;
	if (!TestTrue(TEXT("Run starts"), Course->TryStartFromGate(Runner, Why)))
	{
		AddError(Why);
		return false;
	}
	const double T0 = Course->GetBrick(0).Time;
	Course->SetDebugRunTime(T0);
	Course->UpdateRun();
	TestTrue(TEXT("One hop"), Course->HandleLanding(TEXT("Solo"), 0, T0) == EGFJudgement::Perfect);
	Course->SetDebugRunTime(Course->GetRunEndTime() + 1.0);
	Course->UpdateRun();
	TestFalse(TEXT("Song ran out"), Course->IsRunning());
	TestFalse(TEXT("Running out the song is not a pass"), Course->DidPass());
	if (TestTrue(TEXT("Results"), Course->GetResults().Num() > 0))
	{
		TestFalse(TEXT("Result row did not pass"), Course->GetResults()[0].bPassed);
	}
	TestFalse(TEXT("No win payout"), Ledger.WasApplied(AGFBeatHopCourse::MakeGrantTxId(Course->GetRunId(), TEXT("Solo"), TEXT("SongEnd"))));
	TestFalse(TEXT("Results stay up, so the course tiles wait"), Course->SelectCourse(TEXT("Cinder")));

	FWorld Passed(TEXT("GFBeatHopPassWorld"));
	AGFBeatHopCourse* Finish = Passed.World ? Place(Passed.World) : nullptr;
	if (!TestNotNull(TEXT("Second course"), Finish))
	{
		return false;
	}
	FGFTransferLedger Ledger2;
	FGFMapLedgerAccounts Accounts2;
	Accounts2.Accounts.Add(TEXT("Solo"));
	Finish->SetLedgerOverride(&Ledger2, &Accounts2);
	Finish->SetDebugServerTime(2000.0);
	ACharacter* Finisher = SpawnRunner(Passed.World, Finish->GetStartTrigger() ? Finish->GetStartTrigger()->GetComponentLocation() : Finish->GetLobbySpawn());
	if (!TestTrue(TEXT("Second run starts"), Finish->TryStartFromGate(Finisher, Why)))
	{
		return false;
	}
	const double Hop = Finish->GetBrick(0).Time;
	Finish->SetDebugRunTime(Hop);
	Finish->UpdateRun();
	TestTrue(TEXT("A hop so the runner is on the board"), Finish->HandleLanding(TEXT("Solo"), 0, Hop) == EGFJudgement::Perfect);
	TestTrue(TEXT("Crossing the finish passes the course"), Finish->ReachFinish(Finisher));
	TestTrue(TEXT("Passed"), Finish->DidPass());
	if (TestTrue(TEXT("A result row"), Finish->GetResults().Num() > 0))
	{
		TestTrue(TEXT("The row passed"), Finish->GetResults()[0].bPassed);
	}
	TestTrue(TEXT("Win payout"), Ledger2.WasApplied(AGFBeatHopCourse::MakeGrantTxId(Finish->GetRunId(), TEXT("Solo"), TEXT("SongEnd"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopRescueDinoTest, "GFGame.BeatHop.RescueDino", GFBeatHopCoursePlay::Flags)
bool FGFBeatHopRescueDinoTest::RunTest(const FString&)
{
	using namespace GFBeatHopCoursePlay;
	FWorld W;
	AGFBeatHopCourse* Course = W.World ? Place(W.World) : nullptr;
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	FGFTransferLedger Ledger;
	FGFMapLedgerAccounts Accounts;
	FGFInventory Poor;
	Poor.Add(TEXT("GlowDust"), FGFBeatHopRescueRules::Cost - 1);
	Accounts.Accounts.Add(TEXT("Poor"), Poor);
	FGFInventory Rich;
	Rich.Add(TEXT("GlowDust"), FGFBeatHopRescueRules::Cost);
	Accounts.Accounts.Add(TEXT("Solo"), Rich);
	Course->SetLedgerOverride(&Ledger, &Accounts);

	FString Why;
	TestFalse(TEXT("Short on GlowDust"), Course->TryHireRescue(TEXT("Poor"), Why));
	TestEqual(TEXT("Poor still holds their dust"), Accounts.Accounts[TEXT("Poor")].GetCount(TEXT("GlowDust")), FGFBeatHopRescueRules::Cost - 1);
	TestTrue(TEXT("Hire"), Course->TryHireRescue(TEXT("Solo"), Why));
	TestEqual(TEXT("Dust burned"), Accounts.Accounts[TEXT("Solo")].GetCount(TEXT("GlowDust")), 0);
	TestEqual(TEXT("Three catches"), Course->GetRescueCatches(TEXT("Solo")), FGFBeatHopRescueRules::Catches);
	TestFalse(TEXT("Already hired"), Course->TryHireRescue(TEXT("Solo"), Why));
	TestEqual(TEXT("Dust stays burned"), Accounts.Accounts[TEXT("Solo")].GetCount(TEXT("GlowDust")), 0);

	Course->SetDebugServerTime(1000.0);
	ACharacter* Runner = SpawnRunner(W.World, Course->GetStartTrigger() ? Course->GetStartTrigger()->GetComponentLocation() : Course->GetLobbySpawn());
	if (!TestNotNull(TEXT("Runner"), Runner) || !TestTrue(TEXT("Run"), Course->TryStartFromGate(Runner, Why)))
	{
		AddError(Why);
		return false;
	}
	TestFalse(TEXT("No fall before the run has a dino to spend"), Course->NotifyFall(nullptr, TEXT("Solo")));
	TestTrue(TEXT("Ripwing catches the fall"), Course->NotifyFall(Runner, TEXT("Solo")));
	TestEqual(TEXT("One catch spent"), Course->GetRescueCatches(TEXT("Solo")), FGFBeatHopRescueRules::Catches - 1);
	TestTrue(TEXT("Put back on a brick"), Course->IsValidBrick(Course->GetHoldBrick(Runner)));
	if (TestNotNull(TEXT("Rescue greybox"), Course->GetRescueDino()))
	{
		TestFalse(TEXT("Rescue dino is showing"), Course->GetRescueDino()->bHiddenInGame);
	}
	TestFalse(TEXT("A stranger is not caught"), Course->NotifyFall(Runner, TEXT("Poor")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopRevengeDinoTest, "GFGame.BeatHop.RevengeDinos", GFBeatHopCoursePlay::Flags)
bool FGFBeatHopRevengeDinoTest::RunTest(const FString&)
{
	using namespace GFBeatHopCoursePlay;
	FWorld FernWorld(TEXT("GFBeatHopFernWorld"));
	AGFBeatHopCourse* Fern = FernWorld.World ? Place(FernWorld.World) : nullptr;
	if (!TestNotNull(TEXT("Fern course"), Fern))
	{
		return false;
	}
	Fern->SetDebugServerTime(1000.0);
	ACharacter* FernRunner = SpawnRunner(FernWorld.World, Fern->GetStartTrigger() ? Fern->GetStartTrigger()->GetComponentLocation() : Fern->GetLobbySpawn());
	FString Why;
	if (!TestTrue(TEXT("Fern run"), Fern->TryStartFromGate(FernRunner, Why)))
	{
		AddError(Why);
		return false;
	}
	TestFalse(TEXT("Fern has no revenge dino"), Fern->HasRevengeDino());

	// Ice: the chase closes in, and the skid is a sideways nudge. The run's feel knobs stay put.
	FWorld W(TEXT("GFBeatHopRimeWorld"));
	AGFBeatHopCourse* Course = W.World ? Place(W.World) : nullptr;
	if (!TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	Course->SetDebugServerTime(1000.0);
	TestTrue(TEXT("Rime"), Course->SelectCourse(TEXT("Rime")));
	ACharacter* Runner = SpawnRunner(W.World, Course->GetStartTrigger() ? Course->GetStartTrigger()->GetComponentLocation() : Course->GetLobbySpawn());
	if (!TestNotNull(TEXT("Runner"), Runner) || !TestTrue(TEXT("Ice run"), Course->TryStartFromGate(Runner, Why)))
	{
		AddError(Why);
		return false;
	}
	UCharacterMovementComponent* Move = Runner->GetCharacterMovement();
	if (!TestNotNull(TEXT("Movement"), Move) || !TestTrue(TEXT("Ice dino is out"), Course->HasRevengeDino()))
	{
		return false;
	}
	const float Walk = Move->MaxWalkSpeed;
	const float Gravity = Move->GravityScale;
	const float Air = Move->AirControl;
	const FVector RunnerLocal = Course->GetActorTransform().InverseTransformPosition(Runner->GetActorLocation());
	const float Before = FVector::Dist(Course->GetRevengeLocal(), RunnerLocal);
	const FVector VelBefore = Move->Velocity;
	Course->TickHazards(0.5f);
	TestTrue(TEXT("Chase closes"), FVector::Dist(Course->GetRevengeLocal(), RunnerLocal) < Before - 100.f);
	TestEqual(TEXT("Walk speed untouched"), Move->MaxWalkSpeed, Walk, 0.01f);
	TestEqual(TEXT("Gravity untouched"), Move->GravityScale, Gravity, 0.001f);
	TestEqual(TEXT("Air control untouched"), Move->AirControl, Air, 0.001f);
	const float Side = FVector::DotProduct(Move->Velocity - VelBefore, Course->GetActorRightVector());
	TestTrue(TEXT("Ice skids sideways"), FMath::Abs(Side) > 10.f);
	TestEqual(TEXT("Ice does not knock"), Course->GetRevengeKnocks(), 0);
	if (TestNotNull(TEXT("Revenge greybox"), Course->GetRevengeDino()))
	{
		TestFalse(TEXT("Revenge dino is showing"), Course->GetRevengeDino()->bHiddenInGame);
	}

	// Lava: stand on the dino's post and it swats once. Feel knobs still stay put.
	FWorld LavaWorld(TEXT("GFBeatHopCinderWorld"));
	AGFBeatHopCourse* Lava = LavaWorld.World ? Place(LavaWorld.World) : nullptr;
	if (!TestNotNull(TEXT("Lava course"), Lava) || !TestTrue(TEXT("Cinder"), Lava->SelectCourse(TEXT("Cinder"))))
	{
		return false;
	}
	Lava->SetDebugServerTime(3000.0);
	ACharacter* Swatted = SpawnRunner(LavaWorld.World, Lava->GetStartTrigger() ? Lava->GetStartTrigger()->GetComponentLocation() : Lava->GetLobbySpawn());
	if (!TestNotNull(TEXT("Lava runner"), Swatted) || !TestTrue(TEXT("Lava run"), Lava->TryStartFromGate(Swatted, Why)))
	{
		AddError(Why);
		return false;
	}
	UCharacterMovementComponent* LavaMove = Swatted->GetCharacterMovement();
	if (!TestNotNull(TEXT("Lava movement"), LavaMove) || !TestTrue(TEXT("Lava dino is posted"), Lava->HasRevengeDino()))
	{
		return false;
	}
	const float LavaWalk = LavaMove->MaxWalkSpeed;
	const float LavaGravity = LavaMove->GravityScale;
	const float LavaAir = LavaMove->AirControl;
	FVector Post = Lava->GetRevengeLocal();
	Post.Y = 0.f;
	Post.Z = 120.f;
	Swatted->SetActorLocation(Lava->GetActorTransform().TransformPosition(Post));
	Lava->TickHazards(1.f);
	TestEqual(TEXT("One knock"), Lava->GetRevengeKnocks(), 1);
	TestTrue(TEXT("The knock shoves"), LavaMove->Velocity.SizeSquared() > 100.f);
	Lava->TickHazards(1.f);
	TestEqual(TEXT("Not swatted again"), Lava->GetRevengeKnocks(), 1);
	TestEqual(TEXT("Lava walk speed untouched"), LavaMove->MaxWalkSpeed, LavaWalk, 0.01f);
	TestEqual(TEXT("Lava gravity untouched"), LavaMove->GravityScale, LavaGravity, 0.001f);
	TestEqual(TEXT("Lava air control untouched"), LavaMove->AirControl, LavaAir, 0.001f);
	return true;
}

#endif
