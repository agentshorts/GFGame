// Load repairs a side quest board whose Active pointer and line statuses disagree.
// One test per illegal shape, plus a consistent save that must load unchanged.
// After each illegal load, accept and step progress both still run.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFGameState.h"
#include "GFModeTypes.h"
#include "GFPlayerState.h"
#include "GFQuestSubsystem.h"
#include "GFSideQuestLine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSideQuestBoardRepairTest
{
	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));
	static const FName LineA(TEXT("BoardLineA"));
	static const FName LineB(TEXT("BoardLineB"));
	static const FName LineDone(TEXT("BoardLineDone"));
	static const FName WorkStep(TEXT("Work"));
	static const FName ReportStep(TEXT("Report"));
	static const FString Key(TEXT("SideP"));

	static FGFSideQuestStepRule Step(FName Id, int32 Order, EGFSideQuestEventKind Kind, int32 Goal)
	{
		FGFSideQuestStepRule Rule;
		Rule.StepId = Id;
		Rule.Order = Order;
		Rule.Title = Id.ToString();
		Rule.Hint = TEXT("The elder has a task for you.");
		Rule.Goal = Goal;
		Rule.AdvanceKind = Kind;
		return Rule;
	}

	/** Accept, a Work step of goal 2, then a Report check-in. Three steps so the line is playable. */
	static FGFSideQuestLineRule MakeLine(FName LineId, FName TownId, int32 Sort, EGFSideQuestEventKind WorkKind)
	{
		FGFSideQuestLineRule Rule;
		Rule.LineId = LineId;
		Rule.TownId = TownId;
		Rule.Title = TEXT("Town task");
		Rule.UnlockRank = EGFFactionRank::Member;
		Rule.Sort = Sort;
		Rule.Steps.Add(Step(TEXT("Accept"), 1, EGFSideQuestEventKind::TalkAccepted, 1));
		Rule.Steps.Add(Step(WorkStep, 2, WorkKind, 2));
		Rule.Steps.Add(Step(ReportStep, 3, EGFSideQuestEventKind::TalkAccepted, 1));
		return Rule;
	}

	static FGFSideQuestCatalog TestCatalog()
	{
		FGFSideQuestCatalog Out;
		Out.Lines.Add(MakeLine(LineA, Verdant, 1, EGFSideQuestEventKind::TreeBroken));
		Out.Lines.Add(MakeLine(LineB, Verdant, 2, EGFSideQuestEventKind::FerryArrived));
		Out.Lines.Add(MakeLine(LineDone, Rime, 3, EGFSideQuestEventKind::ClimbMantled));
		Out.Normalize();
		return Out;
	}

	static FGFSideQuestProgress Entry(FName LineId, EGFSideQuestStatus Status, FName StepId = NAME_None, int32 Count = 0)
	{
		FGFSideQuestProgress Progress;
		Progress.LineId = LineId;
		Progress.Status = Status;
		Progress.StepId = StepId;
		Progress.Count = Count;
		return Progress;
	}

	/** Exactly one Active line if and only if ActiveLineId names it. */
	static bool PointerAgrees(const FGFSideQuestBoard& Board)
	{
		int32 ActiveCount = 0;
		bool bNamed = false;
		for (const FGFSideQuestProgress& Line : Board.Lines)
		{
			if (Line.Status != EGFSideQuestStatus::Active)
			{
				continue;
			}
			++ActiveCount;
			if (!Line.LineId.IsNone() && Line.LineId == Board.ActiveLineId)
			{
				bNamed = true;
			}
		}
		if (ActiveCount == 1)
		{
			return bNamed;
		}
		return ActiveCount == 0 && Board.ActiveLineId.IsNone();
	}

	static bool ReplicaAgrees(const AGFPlayerState& PS, const FGFSideQuestBoard& Board)
	{
		const FGFSideQuestProgress* Active = Board.Active();
		if (Active && Active->Status == EGFSideQuestStatus::Active)
		{
			return PS.ActiveSideLineId == Active->LineId
				&& PS.ActiveSideStepId == Active->StepId
				&& PS.ActiveSideCount == Active->Count
				&& PS.ActiveSideStatus == EGFSideQuestStatus::Active;
		}
		return PS.ActiveSideLineId.IsNone()
			&& PS.ActiveSideStepId.IsNone()
			&& PS.ActiveSideCount == 0
			&& PS.ActiveSideStatus == EGFSideQuestStatus::Locked;
	}

	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFQuestSubsystem* Quest = nullptr;
		int32 NextId = 800;

		bool Init(FAutomationTestBase& T, const TCHAR* Name)
		{
			if (!GEngine)
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World)
			{
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			GS = World->SpawnActor<AGFGameState>();
			if (GS)
			{
				World->SetGameState(GS);
			}
			Quest = World->GetSubsystem<UGFQuestSubsystem>();
			if (Quest)
			{
				Quest->EnsureRegistered();
			}
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Quest subsystem"), Quest);
		}

		AGFPlayerState* Add(const FString& PlayerKey)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
			if (PS)
			{
				PS->PlayerKey = PlayerKey;
				PS->SetPlayerId(++NextId);
				PS->SetPlayerName(PlayerKey);
				if (GS->PlayerArray.Find(PS) == INDEX_NONE)
				{
					GS->AddPlayerState(PS);
				}
			}
			return PS;
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

	static bool Open(FAutomationTestBase& T, FWorld& World, const TCHAR* Name, AGFPlayerState*& OutPS)
	{
		if (!World.Init(T, Name))
		{
			return false;
		}
		OutPS = World.Add(Key);
		if (!T.TestNotNull(TEXT("Player"), OutPS))
		{
			return false;
		}
		World.Quest->SetSideCatalogForTests(TestCatalog());
		return true;
	}

	static void Load(UGFQuestSubsystem& Quest, const FGFSideQuestBoard& Board)
	{
		FGFQuestRecord Errand;
		Errand.PlayerKey = Key;
		Errand.Progress.Step = EGFQuestStep::HuntSkins;
		Errand.Progress.FactionId = Verdant;
		Errand.Progress.Rank = EGFFactionRank::Member;

		FGFSideQuestBoardRecord Side;
		Side.PlayerKey = Key;
		Side.Board = Board;

		FGFQuestSave Save;
		Save.Records.Add(Errand);
		Save.SideBoards.Add(Side);
		TArray<uint8> Bytes;
		FGFModeBytes::Write(Save, Bytes);
		Quest.RestoreModeState(Bytes);
	}

	static bool Agreed(FAutomationTestBase& T, const TCHAR* What, const FGFSideQuestBoard* Board, const AGFPlayerState* PS)
	{
		if (!T.TestNotNull(What, Board) || !Board || !T.TestNotNull(TEXT("Player still present"), PS) || !PS)
		{
			return false;
		}
		if (!T.TestTrue(*FString::Printf(TEXT("%s: one Active line if and only if the pointer names it"), What), PointerAgrees(*Board)))
		{
			return false;
		}
		return T.TestTrue(*FString::Printf(TEXT("%s: player state matches the repaired board"), What), ReplicaAgrees(*PS, *Board));
	}

	static const FGFSideQuestProgress* FindLine(const FGFSideQuestBoard* Board, FName LineId)
	{
		return Board ? Board->Find(LineId) : nullptr;
	}

	/** Work events until Report, then the elder check-in. Proves the kept line can still finish. */
	static bool FinishWork(FAutomationTestBase& T, const TCHAR* What, UGFQuestSubsystem& Quest, FName LineId, EGFSideQuestEventKind Kind)
	{
		for (int32 Guard = 0; Guard < 4; ++Guard)
		{
			const FGFSideQuestProgress* Progress = FindLine(Quest.FindSideBoard(Key), LineId);
			if (!Progress || Progress->Status != EGFSideQuestStatus::Active)
			{
				return T.TestTrue(*FString::Printf(TEXT("%s stayed Active"), What), false);
			}
			if (Progress->StepId == ReportStep)
			{
				break;
			}
			const FGFSideQuestApplyResult Step = Quest.ApplySideEvent(FGFSideQuestEvents::Progress(Key, Kind, Verdant, 1));
			if (!T.TestTrue(*FString::Printf(TEXT("%s step progress"), What), Step.Result == EGFQuestResult::Ok))
			{
				return false;
			}
		}
		const FGFSideQuestApplyResult CheckIn = Quest.ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineId, Verdant));
		const FGFSideQuestBoard* Board = Quest.FindSideBoard(Key);
		const FGFSideQuestProgress* Progress = FindLine(Board, LineId);
		return T.TestTrue(*FString::Printf(TEXT("%s check-in completes the line"), What),
			CheckIn.Result == EGFQuestResult::Ok && CheckIn.bCompleted
			&& Progress && Progress->Status == EGFSideQuestStatus::Done
			&& Board && Board->ActiveLineId.IsNone());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestLoadPointerNotActiveTest, "GFGame.SideQuest.Board.LoadPointerNotActive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestLoadPointerNotActiveTest::RunTest(const FString&)
{
	// The pointer names a line that is not Active, and no other line is Active. Accept and step progress used to stick.
	using namespace GFSideQuestBoardRepairTest;
	AddExpectedMessage(TEXT("SideQuest: repaired board"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);

	FWorld World;
	AGFPlayerState* PS = nullptr;
	if (!Open(*this, World, TEXT("GFSideRepairPointer"), PS))
	{
		return false;
	}

	FGFSideQuestBoard Saved;
	Saved.Lines.Add(Entry(LineA, EGFSideQuestStatus::Available));
	Saved.Lines.Add(Entry(LineB, EGFSideQuestStatus::Available));
	Saved.ActiveLineId = LineA;
	Load(*World.Quest, Saved);

	const FGFSideQuestBoard* Board = World.Quest->FindSideBoard(Key);
	if (!Agreed(*this, TEXT("After load"), Board, PS))
	{
		return false;
	}
	const FGFSideQuestProgress* Named = FindLine(Board, LineA);
	TestTrue(TEXT("Pointer cleared"), Board->ActiveLineId.IsNone());
	TestTrue(TEXT("Named line is not left Active"), Named && Named->Status == EGFSideQuestStatus::Available);

	const FGFSideQuestApplyResult Accept = World.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineA, Verdant));
	TestTrue(TEXT("Accept works"), Accept.Result == EGFQuestResult::Ok && Accept.bAccepted);
	Board = World.Quest->FindSideBoard(Key);
	const FGFSideQuestProgress* Active = FindLine(Board, LineA);
	TestTrue(TEXT("Accepted onto Work"), Active && Active->Status == EGFSideQuestStatus::Active && Active->StepId == WorkStep && Active->Count == 0);
	TestEqual(TEXT("Pointer follows the accept"), Board->ActiveLineId, LineA);

	const FGFSideQuestApplyResult Step = World.Quest->ApplySideEvent(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::TreeBroken, Verdant, 1));
	TestTrue(TEXT("Step progress works"), Step.Result == EGFQuestResult::Ok);
	Board = World.Quest->FindSideBoard(Key);
	Active = FindLine(Board, LineA);
	TestTrue(TEXT("Count advanced on the Active line"), Active && Active->Status == EGFSideQuestStatus::Active && Active->StepId == WorkStep && Active->Count == 1);
	TestTrue(TEXT("Other line stayed available"), FindLine(Board, LineB) && FindLine(Board, LineB)->Status == EGFSideQuestStatus::Available);
	return Agreed(*this, TEXT("After play"), Board, PS);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestLoadTwoActiveTest, "GFGame.SideQuest.Board.LoadTwoActive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestLoadTwoActiveTest::RunTest(const FString&)
{
	// Two Active lines. The pointer names the later one, so that line stays and the earlier one drops to Available.
	using namespace GFSideQuestBoardRepairTest;
	AddExpectedMessage(TEXT("SideQuest: repaired board"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);

	FWorld World;
	AGFPlayerState* PS = nullptr;
	if (!Open(*this, World, TEXT("GFSideRepairTwo"), PS))
	{
		return false;
	}

	FGFSideQuestBoard Saved;
	Saved.Lines.Add(Entry(LineA, EGFSideQuestStatus::Active, WorkStep, 0));
	Saved.Lines.Add(Entry(LineDone, EGFSideQuestStatus::Done));
	Saved.Lines.Add(Entry(LineB, EGFSideQuestStatus::Active, WorkStep, 0));
	Saved.ActiveLineId = LineB;
	Load(*World.Quest, Saved);

	const FGFSideQuestBoard* Board = World.Quest->FindSideBoard(Key);
	if (!Agreed(*this, TEXT("After load"), Board, PS))
	{
		return false;
	}
	const FGFSideQuestProgress* Kept = FindLine(Board, LineB);
	const FGFSideQuestProgress* Demoted = FindLine(Board, LineA);
	const FGFSideQuestProgress* Done = FindLine(Board, LineDone);
	TestEqual(TEXT("Pointer kept the named Active line"), Board->ActiveLineId, LineB);
	TestTrue(TEXT("Named line still on Work"), Kept && Kept->Status == EGFSideQuestStatus::Active && Kept->StepId == WorkStep && Kept->Count == 0);
	TestTrue(TEXT("Extra Active line is Available with no step"), Demoted && Demoted->Status == EGFSideQuestStatus::Available && Demoted->StepId.IsNone() && Demoted->Count == 0);
	TestTrue(TEXT("Done line stayed Done"), Done && Done->Status == EGFSideQuestStatus::Done);

	const FGFSideQuestApplyResult Refused = World.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineA, Verdant));
	TestEqual(TEXT("Second accept waits"), Refused.Reason, FGFSideQuestCodes::OtherActive);

	const FGFSideQuestApplyResult Step = World.Quest->ApplySideEvent(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::FerryArrived, Verdant, 1));
	TestTrue(TEXT("Step progress works on the kept line"), Step.Result == EGFQuestResult::Ok);
	Board = World.Quest->FindSideBoard(Key);
	Kept = FindLine(Board, LineB);
	TestTrue(TEXT("Kept line count is 1"), Kept && Kept->Status == EGFSideQuestStatus::Active && Kept->Count == 1 && Board->ActiveLineId == LineB);

	if (!FinishWork(*this, TEXT("Kept line"), *World.Quest, LineB, EGFSideQuestEventKind::FerryArrived))
	{
		return false;
	}
	const FGFSideQuestApplyResult Accept = World.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineA, Verdant));
	TestTrue(TEXT("Accept works once the kept line is Done"), Accept.Result == EGFQuestResult::Ok && Accept.bAccepted);
	const FGFSideQuestApplyResult Other = World.Quest->ApplySideEvent(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::TreeBroken, Verdant, 1));
	TestTrue(TEXT("Step progress works on the newly accepted line"), Other.Result == EGFQuestResult::Ok);
	Board = World.Quest->FindSideBoard(Key);
	const FGFSideQuestProgress* Now = FindLine(Board, LineA);
	TestTrue(TEXT("Accepted line advanced"), Now && Now->Status == EGFSideQuestStatus::Active && Now->StepId == WorkStep && Now->Count == 1);
	return Agreed(*this, TEXT("After play"), Board, PS);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestLoadPointerEmptyTest, "GFGame.SideQuest.Board.LoadPointerEmpty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestLoadPointerEmptyTest::RunTest(const FString&)
{
	// The pointer is empty while lines are Active. Repair points at the first Active line and demotes the rest.
	using namespace GFSideQuestBoardRepairTest;
	AddExpectedMessage(TEXT("SideQuest: repaired board"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);

	FWorld World;
	AGFPlayerState* PS = nullptr;
	if (!Open(*this, World, TEXT("GFSideRepairEmpty"), PS))
	{
		return false;
	}

	FGFSideQuestBoard Saved;
	Saved.Lines.Add(Entry(LineA, EGFSideQuestStatus::Active, WorkStep, 0));
	Saved.Lines.Add(Entry(LineDone, EGFSideQuestStatus::Done));
	Saved.Lines.Add(Entry(LineB, EGFSideQuestStatus::Active, WorkStep, 0));
	Saved.ActiveLineId = NAME_None;
	Load(*World.Quest, Saved);

	const FGFSideQuestBoard* Board = World.Quest->FindSideBoard(Key);
	if (!Agreed(*this, TEXT("After load"), Board, PS))
	{
		return false;
	}
	const FGFSideQuestProgress* Kept = FindLine(Board, LineA);
	const FGFSideQuestProgress* Demoted = FindLine(Board, LineB);
	TestEqual(TEXT("Pointer names the first Active line"), Board->ActiveLineId, LineA);
	TestTrue(TEXT("First Active line kept its step"), Kept && Kept->Status == EGFSideQuestStatus::Active && Kept->StepId == WorkStep && Kept->Count == 0);
	TestTrue(TEXT("Later Active line is Available"), Demoted && Demoted->Status == EGFSideQuestStatus::Available && Demoted->StepId.IsNone() && Demoted->Count == 0);
	TestTrue(TEXT("Done line stayed Done"), FindLine(Board, LineDone) && FindLine(Board, LineDone)->Status == EGFSideQuestStatus::Done);

	const FGFSideQuestApplyResult Step = World.Quest->ApplySideEvent(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::TreeBroken, Verdant, 1));
	TestTrue(TEXT("Step progress works"), Step.Result == EGFQuestResult::Ok);
	Board = World.Quest->FindSideBoard(Key);
	Kept = FindLine(Board, LineA);
	TestTrue(TEXT("First line count is 1"), Kept && Kept->Count == 1 && Board->ActiveLineId == LineA);

	const FGFSideQuestApplyResult Refused = World.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineB, Verdant));
	TestEqual(TEXT("Second accept waits"), Refused.Reason, FGFSideQuestCodes::OtherActive);

	if (!FinishWork(*this, TEXT("First line"), *World.Quest, LineA, EGFSideQuestEventKind::TreeBroken))
	{
		return false;
	}
	const FGFSideQuestApplyResult Accept = World.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineB, Verdant));
	TestTrue(TEXT("Accept works once the first line is Done"), Accept.Result == EGFQuestResult::Ok && Accept.bAccepted);
	const FGFSideQuestApplyResult Other = World.Quest->ApplySideEvent(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::FerryArrived, Verdant, 1));
	TestTrue(TEXT("Step progress works on the accepted line"), Other.Result == EGFQuestResult::Ok);
	Board = World.Quest->FindSideBoard(Key);
	const FGFSideQuestProgress* Now = FindLine(Board, LineB);
	TestTrue(TEXT("Accepted line advanced"), Now && Now->Status == EGFSideQuestStatus::Active && Now->StepId == WorkStep && Now->Count == 1);
	return Agreed(*this, TEXT("After play"), Board, PS);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestLoadConsistentTest, "GFGame.SideQuest.Board.LoadConsistent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestLoadConsistentTest::RunTest(const FString&)
{
	// A board the game itself saved loads with the same lines, pointer, step, and count.
	using namespace GFSideQuestBoardRepairTest;
	TArray<uint8> Bytes;
	FGFSideQuestBoard Captured;
	{
		FWorld First;
		AGFPlayerState* PS = nullptr;
		if (!Open(*this, First, TEXT("GFSideRepairSameA"), PS))
		{
			return false;
		}
		FGFQuestRecord Errand;
		Errand.PlayerKey = Key;
		Errand.Progress.Step = EGFQuestStep::HuntSkins;
		Errand.Progress.FactionId = Verdant;
		Errand.Progress.Rank = EGFFactionRank::Member;
		FGFQuestSave Standing;
		Standing.Records.Add(Errand);
		TArray<uint8> StandingBytes;
		FGFModeBytes::Write(Standing, StandingBytes);
		First.Quest->RestoreModeState(StandingBytes);

		const FGFSideQuestApplyResult Accept = First.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineA, Verdant));
		if (!TestTrue(TEXT("Setup accept"), Accept.Result == EGFQuestResult::Ok && Accept.bAccepted))
		{
			return false;
		}
		const FGFSideQuestApplyResult Step = First.Quest->ApplySideEvent(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::TreeBroken, Verdant, 1));
		if (!TestTrue(TEXT("Setup step"), Step.Result == EGFQuestResult::Ok))
		{
			return false;
		}
		const FGFSideQuestBoard* Live = First.Quest->FindSideBoard(Key);
		if (!TestNotNull(TEXT("Setup board"), Live))
		{
			return false;
		}
		Captured = *Live;
		First.Quest->CaptureModeState(Bytes);
	}

	FWorld Second;
	AGFPlayerState* PS = nullptr;
	if (!Open(*this, Second, TEXT("GFSideRepairSameB"), PS))
	{
		return false;
	}
	Second.Quest->RestoreModeState(Bytes);
	const FGFSideQuestBoard* Loaded = Second.Quest->FindSideBoard(Key);
	if (!TestNotNull(TEXT("Loaded board"), Loaded))
	{
		return false;
	}
	TestTrue(TEXT("Consistent board loads unchanged"), *Loaded == Captured);
	TestEqual(TEXT("Pointer unchanged"), Loaded->ActiveLineId, LineA);
	const FGFSideQuestProgress* Active = FindLine(Loaded, LineA);
	TestTrue(TEXT("Step and count unchanged"), Active && Active->Status == EGFSideQuestStatus::Active && Active->StepId == WorkStep && Active->Count == 1);
	if (!Agreed(*this, TEXT("After load"), Loaded, PS))
	{
		return false;
	}

	const FGFSideQuestApplyResult More = Second.Quest->ApplySideEvent(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::TreeBroken, Verdant, 1));
	TestTrue(TEXT("Step progress still works"), More.Result == EGFQuestResult::Ok);
	Loaded = Second.Quest->FindSideBoard(Key);
	Active = FindLine(Loaded, LineA);
	TestTrue(TEXT("Work step completed into Report"), Active && Active->Status == EGFSideQuestStatus::Active && Active->StepId == ReportStep && Active->Count == 0 && Loaded->ActiveLineId == LineA);
	return Agreed(*this, TEXT("After another step"), Loaded, PS);
}

#endif
