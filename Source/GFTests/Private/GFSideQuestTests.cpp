// Side quest board (QL-a): pure Apply table, unlock gates, one Active line, CSV / Builtin match, and the
// "Quest" blob with errand records and side boards together (old blobs still load the errand).
#include "Misc/AutomationTest.h"
#include "Engine/DataTable.h"
#include "GFSideQuestLine.h"
#include "GFSideQuestRows.h"
#include "GFQuestLine.h"
#include "GFDataTables.h"
#include "GFModeTypes.h"
#include "GFSideQuestTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSideQuestTest
{
	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));
	static const FName Cinderwright(TEXT("Cinderwright"));
	static const FName Cinderwrights(TEXT("Cinderwrights"));
	static const FName Plank(TEXT("WoodPlank"));
	static const FName Crystal(TEXT("Crystal"));
	static const FName Hide(TEXT("Hide"));
	static const FName LineA(TEXT("TestVerdantA"));
	static const FName LineB(TEXT("TestVerdantB"));
	static const FName LineRime(TEXT("TestRime"));
	static const FName LineCinder(TEXT("TestCinder"));

	static FGFSideQuestStepRule Step(const TCHAR* Id, int32 Order, EGFSideQuestEventKind Kind, int32 Goal, TArray<FName> Items = {})
	{
		FGFSideQuestStepRule Rule;
		Rule.StepId = Id;
		Rule.Order = Order;
		Rule.Title = Id;
		Rule.Hint = TEXT("The elder has a task for you.");
		Rule.Goal = Goal;
		Rule.Items = MoveTemp(Items);
		Rule.AdvanceKind = Kind;
		return Rule;
	}

	/** Four test lines that between them use every event kind. Two share Verdant so OneActive can be reached. */
	static const FGFSideQuestCatalog& Catalog()
	{
		static const FGFSideQuestCatalog Built = []()
		{
			FGFSideQuestCatalog Out;
			auto Line = [&Out](FName Id, FName Town, EGFFactionRank Rank, int32 Sort, TArray<FGFSideQuestStepRule> Steps)
			{
				FGFSideQuestLineRule Rule;
				Rule.LineId = Id;
				Rule.TownId = Town;
				Rule.Title = Id.ToString();
				Rule.UnlockRank = Rank;
				Rule.Sort = Sort;
				Rule.Steps = MoveTemp(Steps);
				Out.Lines.Add(Rule);
			};
			Line(LineA, Verdant, EGFFactionRank::Member, 1, {
				Step(TEXT("Accept"), 1, EGFSideQuestEventKind::TalkAccepted, 1),
				Step(TEXT("Wood"), 2, EGFSideQuestEventKind::TreeBroken, 3),
				Step(TEXT("Hops"), 3, EGFSideQuestEventKind::HopLanded, 2),
				Step(TEXT("Climb"), 4, EGFSideQuestEventKind::ClimbMantled, 1),
				Step(TEXT("HandIn"), 5, EGFSideQuestEventKind::TurnIn, 6, { Plank }) });
			Line(LineB, Verdant, EGFFactionRank::Member, 2, {
				Step(TEXT("Accept"), 1, EGFSideQuestEventKind::TalkAccepted, 1),
				Step(TEXT("Ferry"), 2, EGFSideQuestEventKind::FerryArrived, 1),
				Step(TEXT("Held"), 3, EGFSideQuestEventKind::HeldDelivered, 1, { Crystal }) });
			Line(LineRime, Rime, EGFFactionRank::Member, 3, {
				Step(TEXT("Accept"), 1, EGFSideQuestEventKind::TalkAccepted, 1),
				Step(TEXT("Gather"), 2, EGFSideQuestEventKind::CrystalGathered, 2, { Crystal }),
				Step(TEXT("Hire"), 3, EGFSideQuestEventKind::HireSpawned, 1) });
			Line(LineCinder, Cinderwright, EGFFactionRank::Champion, 4, {
				Step(TEXT("Accept"), 1, EGFSideQuestEventKind::TalkAccepted, 1),
				Step(TEXT("Hop"), 2, EGFSideQuestEventKind::HopLanded, 1),
				Step(TEXT("Give"), 3, EGFSideQuestEventKind::TurnIn, 1, { Crystal }) });
			Out.Normalize();
			return Out;
		}();
		return Built;
	}

	static FGFSideQuestLine Player(FName Faction, EGFFactionRank Rank, const FGFSideQuestCatalog* Rules = &Catalog())
	{
		FGFSideQuestLine Line;
		Line.Catalog = Rules;
		Line.FactionId = Faction;
		Line.Rank = Rank;
		Line.Refresh();
		return Line;
	}

	static EGFSideQuestStatus StatusOf(const FGFSideQuestLine& Line, FName LineId)
	{
		const FGFSideQuestProgress* P = Line.Board.Find(LineId);
		return P ? P->Status : EGFSideQuestStatus::Locked;
	}

	static TArray<EGFSideQuestEventKind> Kinds()
	{
		TArray<EGFSideQuestEventKind> Out;
		for (uint8 Value = 0; Value <= static_cast<uint8>(EGFSideQuestEventKind::HireSpawned); ++Value)
		{
			Out.Add(static_cast<EGFSideQuestEventKind>(Value));
		}
		return Out;
	}

	/** A payload that is valid for Kind on Rule's town, so only the step decides the outcome. */
	static FGFSideQuestEvent Valid(EGFSideQuestEventKind Kind, const FGFSideQuestLineRule& Rule, const FGFSideQuestStepRule* Current)
	{
		const FName Item = (Current && Current->Items.Num() > 0) ? Current->Items[0] : Crystal;
		const int32 Goal = Current ? Current->Goal : 1;
		switch (Kind)
		{
		case EGFSideQuestEventKind::TalkAccepted:
			return FGFSideQuestEvents::TalkAccepted(TEXT("P"), Rule.LineId, Rule.TownId);
		case EGFSideQuestEventKind::TurnIn:
			return FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Item, FMath::Max(1, Goal)) }, Rule.TownId);
		case EGFSideQuestEventKind::HeldDelivered:
			return FGFSideQuestEvents::HeldDelivered(TEXT("P"), Item, Rule.TownId);
		default:
		{
			FGFSideQuestEvent Event = FGFSideQuestEvents::Progress(TEXT("P"), Kind, Rule.TownId, 1);
			Event.ItemId = Item;
			return Event;
		}
		}
	}

	/** Board with Rule Active at step Index, count 0. */
	static FGFSideQuestLine ActiveAt(const FGFSideQuestLineRule& Rule, int32 Index, EGFFactionRank Rank = EGFFactionRank::Champion)
	{
		FGFSideQuestLine Line = Player(Rule.TownId, Rank);
		FGFSideQuestProgress* P = Line.Board.Find(Rule.LineId);
		check(P);
		P->Status = EGFSideQuestStatus::Active;
		P->StepId = Rule.Steps[Index].StepId;
		P->Count = 0;
		Line.Board.ActiveLineId = Rule.LineId;
		return Line;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestBoardUnlockTest, "GFGame.SideQuest.Board.Unlock", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestBoardUnlockTest::RunTest(const FString&)
{
	using namespace GFSideQuestTest;
	TestTrue(TEXT("Cinderwright is Cinderwrights"), FGFSideQuestCatalog::SameTown(Cinderwright, Cinderwrights));
	TestTrue(TEXT("Cinder is Cinderwright"), FGFSideQuestCatalog::SameTown(FName(TEXT("Cinder")), Cinderwright));
	TestFalse(TEXT("Rime is not Verdant"), FGFSideQuestCatalog::SameTown(Rime, Verdant));
	TestFalse(TEXT("None never matches"), FGFSideQuestCatalog::SameTown(NAME_None, NAME_None));

	const FName Factions[] = { NAME_None, Verdant, Rime, Cinderwrights, Cinderwright };
	const EGFFactionRank Ranks[] = { EGFFactionRank::Outsider, EGFFactionRank::Initiate, EGFFactionRank::Member, EGFFactionRank::Champion, EGFFactionRank::Leader };
	for (const FName Faction : Factions)
	{
		for (const EGFFactionRank Rank : Ranks)
		{
			const FGFSideQuestLine Line = Player(Faction, Rank);
			const FString What = FString::Printf(TEXT("%s rank %d"), *Faction.ToString(), static_cast<int32>(Rank));
			TestEqual(*FString::Printf(TEXT("%s board has every line"), *What), Line.Board.Lines.Num(), Catalog().Lines.Num());
			TestTrue(*FString::Printf(TEXT("%s nothing Active"), *What), Line.Board.ActiveLineId.IsNone());
			for (const FGFSideQuestLineRule& Rule : Catalog().Lines)
			{
				const bool bTown = FGFSideQuestCatalog::SameTown(Faction, Rule.TownId);
				const bool bRank = static_cast<uint8>(Rank) >= static_cast<uint8>(Rule.UnlockRank);
				const EGFSideQuestStatus Want = (bTown && bRank) ? EGFSideQuestStatus::Available : EGFSideQuestStatus::Locked;
				TestTrue(*FString::Printf(TEXT("%s %s status"), *What, *Rule.LineId.ToString()), StatusOf(Line, Rule.LineId) == Want);

				FGFSideQuestLine Try = Line;
				const FGFSideQuestBoard Before = Try.Board;
				const FGFSideQuestApplyResult Got = Try.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), Rule.LineId, Rule.TownId));
				if (bTown && bRank)
				{
					TestTrue(*FString::Printf(TEXT("%s %s accept"), *What, *Rule.LineId.ToString()), Got.Result == EGFQuestResult::Ok && Got.bAccepted);
					TestEqual(*FString::Printf(TEXT("%s %s active"), *What, *Rule.LineId.ToString()), Try.Board.ActiveLineId, Rule.LineId);
				}
				else
				{
					const FName Why = bTown ? FGFSideQuestCodes::RankLow : FGFQuestCodes::WrongTown;
					TestTrue(*FString::Printf(TEXT("%s %s refused"), *What, *Rule.LineId.ToString()), Got.Result == EGFQuestResult::Refused);
					TestEqual(*FString::Printf(TEXT("%s %s reason"), *What, *Rule.LineId.ToString()), Got.Reason, Why);
					TestTrue(*FString::Printf(TEXT("%s %s board kept"), *What, *Rule.LineId.ToString()), Try.Board == Before);
				}
			}
		}
	}

	// The elder's town must be the line's town, even for a member of that town.
	FGFSideQuestLine Member = Player(Verdant, EGFFactionRank::Member);
	const FGFSideQuestBoard Kept = Member.Board;
	TestEqual(TEXT("Rime elder cannot hand out a Verdant line"), Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineA, Rime)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("No elder town"), Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineA, NAME_None)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("Unknown line"), Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), FName(TEXT("NoSuchLine")), Verdant)).Reason, FGFQuestCodes::Unknown);
	TestEqual(TEXT("No line id"), Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), NAME_None, Verdant)).Reason, FGFQuestCodes::Unknown);
	TestTrue(TEXT("Refusals keep the board"), Member.Board == Kept);

	// Rank moves the gate on Refresh. Member unlocks the Cinder test line only at Champion.
	FGFSideQuestLine Climber = Player(Cinderwrights, EGFFactionRank::Member);
	TestTrue(TEXT("Member: Cinder line locked"), StatusOf(Climber, LineCinder) == EGFSideQuestStatus::Locked);
	Climber.Rank = EGFFactionRank::Champion;
	Climber.Refresh();
	TestTrue(TEXT("Champion: Cinder line available"), StatusOf(Climber, LineCinder) == EGFSideQuestStatus::Available);

	// Shipped stub lines (Accept only) are never offered until their steps land.
	const FGFSideQuestCatalog& Stubs = FGFSideQuestCatalog::Builtin();
	for (const FGFSideQuestLineRule& Rule : Stubs.Lines)
	{
		FGFSideQuestLine Line = Player(Rule.TownId, EGFFactionRank::Leader, &Stubs);
		TestTrue(*FString::Printf(TEXT("Stub %s locked"), *Rule.LineId.ToString()), StatusOf(Line, Rule.LineId) == EGFSideQuestStatus::Locked);
		TestEqual(*FString::Printf(TEXT("Stub %s not ready"), *Rule.LineId.ToString()), Line.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), Rule.LineId, Rule.TownId)).Reason, FGFSideQuestCodes::NotReady);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestBoardOneActiveTest, "GFGame.SideQuest.Board.OneActive", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestBoardOneActiveTest::RunTest(const FString&)
{
	using namespace GFSideQuestTest;
	FGFSideQuestLine A = Player(Verdant, EGFFactionRank::Member);
	FGFSideQuestLine B = Player(Verdant, EGFFactionRank::Member);

	const FGFSideQuestApplyResult Took = A.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("A"), LineA, Verdant));
	TestTrue(TEXT("A accepts line A"), Took.Result == EGFQuestResult::Ok && Took.bAccepted && !Took.bCompleted);
	TestEqual(TEXT("Accept step done on accept"), A.Board.Find(LineA)->StepId, FName(TEXT("Wood")));
	TestTrue(TEXT("B board untouched"), StatusOf(B, LineA) == EGFSideQuestStatus::Available && B.Board.ActiveLineId.IsNone());

	const FGFSideQuestBoard Before = A.Board;
	const FGFSideQuestApplyResult Second = A.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("A"), LineB, Verdant));
	TestTrue(TEXT("Second line refused"), Second.Result == EGFQuestResult::Refused);
	TestEqual(TEXT("Reason OtherActive"), Second.Reason, FGFSideQuestCodes::OtherActive);
	TestTrue(TEXT("Board kept"), A.Board == Before);
	TestTrue(TEXT("Line B still Available"), StatusOf(A, LineB) == EGFSideQuestStatus::Available);
	const FGFSideQuestApplyResult Again = A.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("A"), LineA, Verdant));
	TestTrue(TEXT("Same line again is ignored"), Again.Result == EGFQuestResult::Ignored && Again.Reason == FGFSideQuestCodes::Already);

	// Run line A to Done.
	TestTrue(TEXT("Tree 1"), A.Apply(FGFSideQuestEvents::Progress(TEXT("A"), EGFSideQuestEventKind::TreeBroken, Verdant)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Count 1"), A.Board.Find(LineA)->Count, 1);
	TestTrue(TEXT("Tree 2 and 3 at once"), A.Apply(FGFSideQuestEvents::Progress(TEXT("A"), EGFSideQuestEventKind::TreeBroken, Verdant, 5)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("On to hops"), A.Board.Find(LineA)->StepId, FName(TEXT("Hops")));
	TestEqual(TEXT("Count reset"), A.Board.Find(LineA)->Count, 0);
	TestTrue(TEXT("Hop 1"), A.Apply(FGFSideQuestEvents::Progress(TEXT("A"), EGFSideQuestEventKind::HopLanded)).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Hop 2"), A.Apply(FGFSideQuestEvents::Progress(TEXT("A"), EGFSideQuestEventKind::HopLanded)).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Climb"), A.Apply(FGFSideQuestEvents::Progress(TEXT("A"), EGFSideQuestEventKind::ClimbMantled, Verdant)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Short hand in"), A.Apply(FGFSideQuestEvents::TurnIn(TEXT("A"), { FGFItemStack(Plank, 5) }, Verdant)).Reason, FGFQuestCodes::Short);
	TestEqual(TEXT("Wrong item"), A.Apply(FGFSideQuestEvents::TurnIn(TEXT("A"), { FGFItemStack(Hide, 9) }, Verdant)).Reason, FGFQuestCodes::WrongItem);
	TestEqual(TEXT("Hand in at the wrong town"), A.Apply(FGFSideQuestEvents::TurnIn(TEXT("A"), { FGFItemStack(Plank, 6) }, Rime)).Reason, FGFQuestCodes::WrongTown);
	const FGFSideQuestApplyResult Done = A.Apply(FGFSideQuestEvents::TurnIn(TEXT("A"), { FGFItemStack(Hide, 2), FGFItemStack(Plank, 8) }, Verdant));
	TestTrue(TEXT("Hand in completes"), Done.Result == EGFQuestResult::Ok && Done.bCompleted && !Done.bAccepted);
	TestEqual(TEXT("Charge one stack"), Done.Charge.Num(), 1);
	TestTrue(TEXT("Charge exactly 6 planks"), Done.Charge.Num() == 1 && Done.Charge[0].ItemId == Plank && Done.Charge[0].Count == 6);
	TestTrue(TEXT("Line A Done"), StatusOf(A, LineA) == EGFSideQuestStatus::Done);
	TestTrue(TEXT("Nothing Active"), A.Board.ActiveLineId.IsNone());
	TestEqual(TEXT("Done stays on the board"), A.Board.Lines.Num(), Catalog().Lines.Num());
	TestTrue(TEXT("Events after Done are ignored"), A.Apply(FGFSideQuestEvents::Progress(TEXT("A"), EGFSideQuestEventKind::TreeBroken, Verdant)).Result == EGFQuestResult::Ignored);
	TestEqual(TEXT("Done line cannot be taken again"), A.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("A"), LineA, Verdant)).Reason, FGFSideQuestCodes::Already);

	// Now the other Verdant line can go.
	TestTrue(TEXT("Line B accepted after A is done"), A.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("A"), LineB, Verdant)).bAccepted);
	TestTrue(TEXT("Line A still Done"), StatusOf(A, LineA) == EGFSideQuestStatus::Done);
	TestEqual(TEXT("Line B is the Active one"), A.Board.ActiveLineId, LineB);

	// B was never touched by any of it.
	TestTrue(TEXT("B still clean"), B.Board.IsDefault());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestLineApplyTableTest, "GFGame.SideQuest.Line.ApplyTable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestLineApplyTableTest::RunTest(const FString&)
{
	using namespace GFSideQuestTest;
	// Every line x every step x every kind: only the step's own kind moves; everything else leaves the board as it was.
	for (const FGFSideQuestLineRule& Rule : Catalog().Lines)
	{
		for (int32 Index = 0; Index < Rule.Steps.Num(); ++Index)
		{
			const FGFSideQuestStepRule& Current = Rule.Steps[Index];
			for (const EGFSideQuestEventKind Kind : Kinds())
			{
				FGFSideQuestLine Line = ActiveAt(Rule, Index);
				const FGFSideQuestBoard Before = Line.Board;
				const FGFSideQuestApplyResult Got = Line.Apply(Valid(Kind, Rule, &Current));
				const FString What = FString::Printf(TEXT("%s %s %s"), *Rule.LineId.ToString(), *Current.StepId.ToString(), *FGFSideQuestCatalog::KindId(Kind).ToString());
				if (Kind == EGFSideQuestEventKind::TalkAccepted)
				{
					TestTrue(*(What + TEXT(" ignored")), Got.Result == EGFQuestResult::Ignored && Got.Reason == FGFSideQuestCodes::Already);
					TestTrue(*(What + TEXT(" kept")), Line.Board == Before);
					continue;
				}
				if (Kind != Current.AdvanceKind)
				{
					TestTrue(*(What + TEXT(" ignored")), Got.Result == EGFQuestResult::Ignored && Got.Reason == FGFQuestCodes::OutOfOrder);
					TestTrue(*(What + TEXT(" kept")), Line.Board == Before);
					continue;
				}
				TestTrue(*(What + TEXT(" ok")), Got.Result == EGFQuestResult::Ok);
				const FGFSideQuestProgress* P = Line.Board.Find(Rule.LineId);
				const bool bStepDone = Kind == EGFSideQuestEventKind::TurnIn || Current.Goal <= 1;
				if (!bStepDone)
				{
					TestTrue(*(What + TEXT(" counts")), P && P->StepId == Current.StepId && P->Count == 1);
				}
				else if (Rule.Steps.IsValidIndex(Index + 1))
				{
					TestTrue(*(What + TEXT(" next step")), P && P->StepId == Rule.Steps[Index + 1].StepId && P->Count == 0 && P->Status == EGFSideQuestStatus::Active);
				}
				else
				{
					TestTrue(*(What + TEXT(" done")), Got.bCompleted && P && P->Status == EGFSideQuestStatus::Done && Line.Board.ActiveLineId.IsNone());
				}
			}
		}
	}

	const FGFSideQuestLineRule* B = Catalog().FindLine(LineB);
	const FGFSideQuestLineRule* R = Catalog().FindLine(LineRime);
	const FGFSideQuestLineRule* A = Catalog().FindLine(LineA);
	if (!TestNotNull(TEXT("Line B"), B) || !TestNotNull(TEXT("Rime line"), R) || !TestNotNull(TEXT("Line A"), A))
	{
		return false;
	}
	struct FBad
	{
		const TCHAR* What;
		const FGFSideQuestLineRule* Rule;
		int32 Index;
		FGFSideQuestEvent Event;
		EGFQuestResult Result;
		FName Reason;
	};
	FGFSideQuestEvent Unknown = FGFSideQuestEvents::Progress(TEXT("P"), EGFSideQuestEventKind::TreeBroken, Verdant);
	Unknown.Kind = static_cast<EGFSideQuestEventKind>(200);
	FGFSideQuestEvent OtherLine = FGFSideQuestEvents::Progress(TEXT("P"), EGFSideQuestEventKind::TreeBroken, Verdant);
	OtherLine.LineId = LineB;
	FGFSideQuestEvent WrongCrystal = FGFSideQuestEvents::Progress(TEXT("P"), EGFSideQuestEventKind::CrystalGathered, Rime);
	WrongCrystal.ItemId = Hide;
	const FBad Bad[] = {
		{ TEXT("Unknown kind"), A, 1, Unknown, EGFQuestResult::Ignored, FGFQuestCodes::Unknown },
		{ TEXT("Event for another line"), A, 1, OtherLine, EGFQuestResult::Ignored, FGFQuestCodes::OutOfOrder },
		{ TEXT("Tree in another town"), A, 1, FGFSideQuestEvents::Progress(TEXT("P"), EGFSideQuestEventKind::TreeBroken, Rime), EGFQuestResult::Refused, FGFQuestCodes::WrongTown },
		{ TEXT("Held with empty hands"), B, 2, FGFSideQuestEvents::HeldDelivered(TEXT("P"), NAME_None, Verdant), EGFQuestResult::Refused, FGFQuestCodes::WrongItem },
		{ TEXT("Held the wrong item"), B, 2, FGFSideQuestEvents::HeldDelivered(TEXT("P"), Hide, Verdant), EGFQuestResult::Refused, FGFQuestCodes::WrongItem },
		{ TEXT("Held at the wrong elder"), B, 2, FGFSideQuestEvents::HeldDelivered(TEXT("P"), Crystal, Rime), EGFQuestResult::Refused, FGFQuestCodes::WrongTown },
		{ TEXT("Gathered the wrong item"), R, 1, WrongCrystal, EGFQuestResult::Refused, FGFQuestCodes::WrongItem },
		{ TEXT("Empty turn in"), A, 4, FGFSideQuestEvents::TurnIn(TEXT("P"), {}, Verdant), EGFQuestResult::Refused, FGFQuestCodes::Short },
	};
	for (const FBad& Case : Bad)
	{
		FGFSideQuestLine Line = ActiveAt(*Case.Rule, Case.Index);
		const FGFSideQuestBoard Before = Line.Board;
		const FGFSideQuestApplyResult Got = Line.Apply(Case.Event);
		TestTrue(*FString::Printf(TEXT("%s result"), Case.What), Got.Result == Case.Result);
		TestEqual(*FString::Printf(TEXT("%s reason"), Case.What), Got.Reason, Case.Reason);
		TestTrue(*FString::Printf(TEXT("%s kept"), Case.What), Line.Board == Before);
		TestEqual(*FString::Printf(TEXT("%s no charge"), Case.What), Got.Charge.Num(), 0);
	}

	// No Active line: every non-accept kind is out of order.
	FGFSideQuestLine Idle = Player(Verdant, EGFFactionRank::Member);
	const FGFSideQuestBoard IdleBefore = Idle.Board;
	for (const EGFSideQuestEventKind Kind : Kinds())
	{
		if (Kind == EGFSideQuestEventKind::TalkAccepted)
		{
			continue;
		}
		const FGFSideQuestApplyResult Got = Idle.Apply(Valid(Kind, *A, nullptr));
		TestTrue(*FString::Printf(TEXT("Idle %s"), *FGFSideQuestCatalog::KindId(Kind).ToString()), Got.Result == EGFQuestResult::Ignored && Got.Reason == FGFQuestCodes::OutOfOrder);
	}
	TestTrue(TEXT("Idle board kept"), Idle.Board == IdleBefore);

	// Kind names parse both ways (the CSV AdvanceKind column).
	for (const EGFSideQuestEventKind Kind : Kinds())
	{
		EGFSideQuestEventKind Parsed;
		TestTrue(*FString::Printf(TEXT("Parse %s"), *FGFSideQuestCatalog::KindId(Kind).ToString()), FGFSideQuestCatalog::TryKind(FGFSideQuestCatalog::KindId(Kind), Parsed) && Parsed == Kind);
	}
	EGFSideQuestEventKind Ignored = EGFSideQuestEventKind::TalkAccepted;
	TestFalse(TEXT("Bad kind name"), FGFSideQuestCatalog::TryKind(FName(TEXT("Dance")), Ignored));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestDataRowsTest, "GFGame.SideQuest.Data.Rows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestDataRowsTest::RunTest(const FString&)
{
	using namespace GFSideQuestTest;
	const UDataTable* LinesTable = FGFDataTables::Get<FGFSideQuestLineRow>(TEXT("DT_QuestLines"));
	const UDataTable* StepsTable = FGFDataTables::Get<FGFSideQuestStepRow>(TEXT("DT_QuestSteps"));
	if (!TestNotNull(TEXT("DT_QuestLines"), LinesTable) || !TestNotNull(TEXT("DT_QuestSteps"), StepsTable))
	{
		return false;
	}
	const FGFSideQuestCatalog& Builtin = FGFSideQuestCatalog::Builtin();
	const FGFSideQuestCatalog Loaded = FGFSideQuestCatalog::Load();
	TestEqual(TEXT("Line rows match Builtin"), LinesTable->GetRowMap().Num(), Builtin.Lines.Num());
	int32 BuiltinSteps = 0;
	for (const FGFSideQuestLineRule& Rule : Builtin.Lines)
	{
		BuiltinSteps += Rule.Steps.Num();
	}
	TestEqual(TEXT("Step rows match Builtin"), StepsTable->GetRowMap().Num(), BuiltinSteps);
	TestEqual(TEXT("Loaded line count"), Loaded.Lines.Num(), Builtin.Lines.Num());

	for (int32 L = 0; L < Builtin.Lines.Num(); ++L)
	{
		const FGFSideQuestLineRule& Want = Builtin.Lines[L];
		const FString Name = Want.LineId.ToString();
		const FGFSideQuestLineRow* Row = LinesTable->FindRow<FGFSideQuestLineRow>(Want.LineId, TEXT("SideQuest"), false);
		if (TestNotNull(*FString::Printf(TEXT("Row %s"), *Name), Row))
		{
			TestEqual(*FString::Printf(TEXT("%s town"), *Name), Row->TownId, Want.TownId);
			TestEqual(*FString::Printf(TEXT("%s title"), *Name), Row->Title, Want.Title);
			TestTrue(*FString::Printf(TEXT("%s unlock"), *Name), Row->UnlockRank == Want.UnlockRank);
			TestEqual(*FString::Printf(TEXT("%s sort"), *Name), Row->Sort, Want.Sort);
		}
		const FGFSideQuestLineRule* Got = Loaded.FindLine(Want.LineId);
		if (!TestNotNull(*FString::Printf(TEXT("Loaded %s"), *Name), Got))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("Loaded %s order"), *Name), Loaded.Lines[L].LineId, Want.LineId);
		TestEqual(*FString::Printf(TEXT("Loaded %s town"), *Name), Got->TownId, Want.TownId);
		TestEqual(*FString::Printf(TEXT("Loaded %s steps"), *Name), Got->Steps.Num(), Want.Steps.Num());
		for (int32 S = 0; S < Want.Steps.Num() && S < Got->Steps.Num(); ++S)
		{
			const FGFSideQuestStepRule& WS = Want.Steps[S];
			const FGFSideQuestStepRule& GS = Got->Steps[S];
			const FString Step = Name + TEXT(".") + WS.StepId.ToString();
			TestEqual(*(Step + TEXT(" id")), GS.StepId, WS.StepId);
			TestEqual(*(Step + TEXT(" order")), GS.Order, WS.Order);
			TestEqual(*(Step + TEXT(" title")), GS.Title, WS.Title);
			TestEqual(*(Step + TEXT(" hint")), GS.Hint, WS.Hint);
			TestEqual(*(Step + TEXT(" goal")), GS.Goal, WS.Goal);
			TestTrue(*(Step + TEXT(" items")), GS.Items == WS.Items);
			TestTrue(*(Step + TEXT(" kind")), GS.AdvanceKind == WS.AdvanceKind);
		}
	}

	// Canon (#368): new side data names the town Cinderwright, and it still belongs to the Cinderwrights faction.
	const FGFSideQuestLineRule* Cinder = Builtin.FindLine(FName(TEXT("CinderEmberAscent")));
	if (TestNotNull(TEXT("Cinder line"), Cinder))
	{
		TestEqual(TEXT("Cinder town id"), Cinder->TownId, Cinderwright);
		TestTrue(TEXT("Cinder town matches the faction"), FGFSideQuestCatalog::SameTown(Cinder->TownId, Cinderwrights));
	}
	for (const FGFSideQuestLineRule& Rule : Builtin.Lines)
	{
		TestTrue(*FString::Printf(TEXT("%s unlocks at Member"), *Rule.LineId.ToString()), Rule.UnlockRank == EGFFactionRank::Member);
		TestTrue(*FString::Printf(TEXT("%s starts with Accept"), *Rule.LineId.ToString()), Rule.Steps.Num() > 0 && Rule.Steps[0].AdvanceKind == EGFSideQuestEventKind::TalkAccepted);
	}

	// A table with no step rows keeps Builtin steps; no tables at all is Builtin.
	const FGFSideQuestCatalog NoSteps = FGFSideQuestCatalog::FromTables(LinesTable, nullptr);
	TestEqual(TEXT("No steps table keeps Builtin steps"), NoSteps.Lines.Num() > 0 ? NoSteps.Lines[0].Steps.Num() : -1, Builtin.Lines.Num() > 0 ? Builtin.Lines[0].Steps.Num() : -1);
	TestEqual(TEXT("No tables is Builtin"), FGFSideQuestCatalog::FromTables(nullptr, nullptr).Lines.Num(), Builtin.Lines.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestSaveRoundTripTest, "GFGame.SideQuest.Save.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFSideQuestSaveRoundTripTest::RunTest(const FString&)
{
	using namespace GFSideQuestTest;
	FGFQuestRecord Errand;
	Errand.PlayerKey = TEXT("A");
	Errand.Progress.Step = EGFQuestStep::HuntSkins;
	Errand.Progress.FactionId = Verdant;
	Errand.Progress.Rank = EGFFactionRank::Member;
	Errand.Progress.HeatFactions.Add(Rime);

	// Errand and side board together.
	{
		FGFSideQuestLine Board = Player(Verdant, EGFFactionRank::Member);
		TestTrue(TEXT("Accept"), Board.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("A"), LineA, Verdant)).bAccepted);
		TestTrue(TEXT("One tree"), Board.Apply(FGFSideQuestEvents::Progress(TEXT("A"), EGFSideQuestEventKind::TreeBroken, Verdant)).Result == EGFQuestResult::Ok);
		FGFSideQuestBoardRecord SideA;
		SideA.PlayerKey = TEXT("A");
		SideA.Board = Board.Board;
		TestFalse(TEXT("Active board is worth saving"), SideA.Board.IsDefault());

		FGFSideQuestBoardRecord SideB;
		SideB.PlayerKey = TEXT("B");
		FGFSideQuestProgress DoneLine;
		DoneLine.LineId = LineRime;
		DoneLine.Status = EGFSideQuestStatus::Done;
		SideB.Board.Lines.Add(DoneLine);
		TestFalse(TEXT("Done board is worth saving"), SideB.Board.IsDefault());

		FGFQuestSave Save;
		Save.Records.Add(Errand);
		Save.SideBoards = { SideA, SideB };
		TArray<uint8> Bytes;
		FGFModeBytes::Write(Save, Bytes);
		FGFQuestSave Back;
		TestTrue(TEXT("Bytes read"), FGFModeBytes::Read(Back, Bytes));
		TestEqual(TEXT("One errand record"), Back.Records.Num(), 1);
		TestTrue(TEXT("Errand kept"), Back.Records.Num() == 1 && Back.Records[0].PlayerKey == Errand.PlayerKey && Back.Records[0].Progress == Errand.Progress);
		TestEqual(TEXT("Two boards"), Back.SideBoards.Num(), 2);
		TestTrue(TEXT("Board A kept"), Back.SideBoards.Num() == 2 && Back.SideBoards[0].PlayerKey == TEXT("A") && Back.SideBoards[0].Board == SideA.Board);
		TestTrue(TEXT("Board B kept"), Back.SideBoards.Num() == 2 && Back.SideBoards[1].PlayerKey == TEXT("B") && Back.SideBoards[1].Board == SideB.Board);
		const FGFSideQuestProgress* Active = Back.SideBoards.Num() > 0 ? Back.SideBoards[0].Board.Active() : nullptr;
		TestTrue(TEXT("Active step and count kept"), Active && Active->StepId == FName(TEXT("Wood")) && Active->Count == 1 && Active->Status == EGFSideQuestStatus::Active);
	}

	// An old blob (errand records only) still loads the errand and comes back with no side boards.
	{
		FGFQuestSaveBeforeSideBoard Old;
		Old.Records.Add(Errand);
		TArray<uint8> Bytes;
		FGFModeBytes::Write(Old, Bytes);
		FGFQuestSave Back;
		TestTrue(TEXT("Old bytes read"), FGFModeBytes::Read(Back, Bytes));
		TestEqual(TEXT("Old errand record"), Back.Records.Num(), 1);
		TestTrue(TEXT("Old errand kept"), Back.Records.Num() == 1 && Back.Records[0].PlayerKey == Errand.PlayerKey && Back.Records[0].Progress == Errand.Progress);
		TestEqual(TEXT("Old blob has no boards"), Back.SideBoards.Num(), 0);
	}

	// A fresh board (only Locked / Available) is not saved; it is rebuilt from the errand rank.
	{
		const FGFSideQuestLine Fresh = Player(Verdant, EGFFactionRank::Member);
		TestTrue(TEXT("Available only board is default"), Fresh.Board.IsDefault());
	}
	return true;
}

#endif
