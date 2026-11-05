#include "GFSideQuestLine.h"
#include "GFSideQuestRows.h"
#include "GFQuestLine.h"
#include "GFDataTables.h"
#include "Engine/DataTable.h"

const FName FGFSideQuestCodes::RankLow(TEXT("RankLow"));
const FName FGFSideQuestCodes::OtherActive(TEXT("OtherActive"));
const FName FGFSideQuestCodes::Already(TEXT("Already"));
const FName FGFSideQuestCodes::NotReady(TEXT("NotReady"));

namespace GFSideQuestLinePrivate
{
	static FName CanonTown(FName Id)
	{
		static const FName Cinder(TEXT("Cinder"));
		static const FName Cinderwright(TEXT("Cinderwright"));
		static const FName Cinderwrights(TEXT("Cinderwrights"));
		if (Id == Cinder || Id == Cinderwrights)
		{
			return Cinderwright;
		}
		return Id;
	}

	static FGFSideQuestApplyResult Make(EGFQuestResult Result, FName Reason, FName LineId = NAME_None)
	{
		FGFSideQuestApplyResult Out;
		Out.Result = Result;
		Out.Reason = Reason;
		Out.LineId = LineId;
		return Out;
	}

	static FGFSideQuestStepRule MakeStep(const TCHAR* StepId, int32 Order, const TCHAR* Title, const TCHAR* Hint, int32 Goal,
		EGFSideQuestEventKind Kind, TArray<FName> Items = {})
	{
		FGFSideQuestStepRule Step;
		Step.StepId = StepId;
		Step.Order = Order;
		Step.Title = Title;
		Step.Hint = Hint;
		Step.Goal = Goal;
		Step.Items = MoveTemp(Items);
		Step.AdvanceKind = Kind;
		return Step;
	}

	static FGFSideQuestStepRule Accept()
	{
		return MakeStep(TEXT("Accept"), 1, TEXT("Town task"), TEXT("The elder has a task for you."), 1, EGFSideQuestEventKind::TalkAccepted);
	}

	/**
	 * Verdant town task (QL-c, #363). PLACEHOLDER title. Generic strings only (canon #368).
	 * Six Apply steps for the five plan beats: the hand-in is followed by a short escort hire, and HireEscort
	 * finishes when that hire spawns (UGFSideQuestEscorts raises HireSpawned).
	 */
	static TArray<FGFSideQuestStepRule> VerdantSteps()
	{
		using K = EGFSideQuestEventKind;
		return {
			Accept(),
			MakeStep(TEXT("GatherWood"), 2, TEXT("Gather wood"), TEXT("Break trees in town."), 3, K::TreeBroken),
			MakeStep(TEXT("RoadHops"), 3, TEXT("Road hops"), TEXT("Land good hops on the road."), 5, K::HopLanded),
			MakeStep(TEXT("ScoutClimb"), 4, TEXT("Scout climb"), TEXT("Climb the lookout in town."), 1, K::ClimbMantled),
			MakeStep(TEXT("HandIn"), 5, TEXT("Hand in"), TEXT("Bring planks to the elder."), 6, K::TurnIn, { FName(TEXT("WoodPlank")) }),
			MakeStep(TEXT("HireEscort"), 6, TEXT("Escort"), TEXT("A hire will walk with you."), 1, K::HireSpawned),
		};
	}
}

int32 FGFSideQuestLineRule::IndexOf(FName StepId) const
{
	return Steps.IndexOfByPredicate([StepId](const FGFSideQuestStepRule& Step) { return Step.StepId == StepId; });
}

const FGFSideQuestStepRule* FGFSideQuestLineRule::FindStep(FName StepId) const
{
	const int32 Index = IndexOf(StepId);
	return Steps.IsValidIndex(Index) ? &Steps[Index] : nullptr;
}

const FGFSideQuestCatalog& FGFSideQuestCatalog::Builtin()
{
	// PLACEHOLDER ids and titles (canon Q1). Verdant ships in full (QL-c). Rime and Cinderwright still ship only
	// step 1 (Accept), so they stay Locked (MinSteps) until QL-d and QL-e append the rest to the CSV and here.
	static const FGFSideQuestCatalog Catalog = []()
	{
		FGFSideQuestCatalog Built;
		auto Line = [&Built](const TCHAR* LineId, const TCHAR* TownId, const TCHAR* Title, int32 Sort, TArray<FGFSideQuestStepRule> Steps)
		{
			FGFSideQuestLineRule Rule;
			Rule.LineId = LineId;
			Rule.TownId = TownId;
			Rule.Title = Title;
			Rule.UnlockRank = EGFFactionRank::Member;
			Rule.Sort = Sort;
			Rule.Steps = MoveTemp(Steps);
			Built.Lines.Add(Rule);
		};
		Line(TEXT("VerdantRootward"), TEXT("Verdant"), TEXT("Rootward Run"), 1, GFSideQuestLinePrivate::VerdantSteps());
		Line(TEXT("RimeIceSpan"), TEXT("Rime"), TEXT("Ice Span"), 2, { GFSideQuestLinePrivate::Accept() });
		Line(TEXT("CinderEmberAscent"), TEXT("Cinderwright"), TEXT("Ember Ascent"), 3, { GFSideQuestLinePrivate::Accept() });
		Built.Normalize();
		return Built;
	}();
	return Catalog;
}

FGFSideQuestCatalog FGFSideQuestCatalog::FromTables(const UDataTable* LinesTable, const UDataTable* StepsTable)
{
	FGFSideQuestCatalog Out = Builtin();
	if (LinesTable)
	{
		Out.Lines.Reset();
		LinesTable->ForeachRow<FGFSideQuestLineRow>(TEXT("GFSideQuest"), [&Out](const FName& RowName, const FGFSideQuestLineRow& Row)
		{
			if (RowName.IsNone() || Out.FindLine(RowName))
			{
				return;
			}
			FGFSideQuestLineRule Rule;
			Rule.LineId = RowName;
			Rule.TownId = Row.TownId;
			Rule.Title = Row.Title;
			Rule.UnlockRank = Row.UnlockRank;
			Rule.Sort = Row.Sort;
			Out.Lines.Add(Rule);
		});
		if (StepsTable)
		{
			StepsTable->ForeachRow<FGFSideQuestStepRow>(TEXT("GFSideQuest"), [&Out](const FName& RowName, const FGFSideQuestStepRow& Row)
			{
				(void)RowName;
				FGFSideQuestLineRule* Line = Out.Lines.FindByPredicate([&Row](const FGFSideQuestLineRule& L) { return L.LineId == Row.LineId; });
				if (!Line || Row.StepId.IsNone() || Line->FindStep(Row.StepId))
				{
					return;
				}
				FGFSideQuestStepRule Step;
				Step.StepId = Row.StepId;
				Step.Order = Row.Order;
				Step.Title = Row.Title;
				Step.Hint = Row.Hint;
				Step.Goal = FMath::Max(1, Row.Goal);
				Step.Items = Row.Items;
				Step.AdvanceKind = Row.AdvanceKind;
				Line->Steps.Add(Step);
			});
		}
		else
		{
			// Lines without a steps table keep Builtin steps for the same id.
			for (FGFSideQuestLineRule& Line : Out.Lines)
			{
				if (const FGFSideQuestLineRule* Known = Builtin().FindLine(Line.LineId))
				{
					Line.Steps = Known->Steps;
				}
			}
		}
	}
	Out.Normalize();
	return Out;
}

FGFSideQuestCatalog FGFSideQuestCatalog::Load()
{
	return FromTables(FGFDataTables::Get<FGFSideQuestLineRow>(TEXT("DT_QuestLines")), FGFDataTables::Get<FGFSideQuestStepRow>(TEXT("DT_QuestSteps")));
}

bool FGFSideQuestCatalog::SameTown(FName A, FName B)
{
	if (A.IsNone() || B.IsNone())
	{
		return false;
	}
	return GFSideQuestLinePrivate::CanonTown(A) == GFSideQuestLinePrivate::CanonTown(B);
}

bool FGFSideQuestCatalog::TryKind(FName Id, EGFSideQuestEventKind& Out)
{
	const UEnum* Enum = StaticEnum<EGFSideQuestEventKind>();
	if (!Enum || Id.IsNone())
	{
		return false;
	}
	int64 Value = Enum->GetValueByNameString(FString::Printf(TEXT("EGFSideQuestEventKind::%s"), *Id.ToString()));
	if (Value == INDEX_NONE)
	{
		Value = Enum->GetValueByName(Id);
	}
	if (Value == INDEX_NONE || Value > static_cast<int64>(GFSideQuestLastKind))
	{
		return false;
	}
	Out = static_cast<EGFSideQuestEventKind>(Value);
	return true;
}

FName FGFSideQuestCatalog::KindId(EGFSideQuestEventKind Kind)
{
	const UEnum* Enum = StaticEnum<EGFSideQuestEventKind>();
	FString Name = Enum ? Enum->GetNameStringByValue(static_cast<int64>(Kind)) : FString();
	int32 Colon = INDEX_NONE;
	if (Name.FindLastChar(TEXT(':'), Colon))
	{
		Name.RightChopInline(Colon + 1);
	}
	return FName(*Name);
}

const FGFSideQuestLineRule* FGFSideQuestCatalog::FindLine(FName LineId) const
{
	if (LineId.IsNone())
	{
		return nullptr;
	}
	return Lines.FindByPredicate([LineId](const FGFSideQuestLineRule& Line) { return Line.LineId == LineId; });
}

void FGFSideQuestCatalog::Normalize()
{
	Lines.StableSort([](const FGFSideQuestLineRule& A, const FGFSideQuestLineRule& B) { return A.Sort < B.Sort; });
	for (FGFSideQuestLineRule& Line : Lines)
	{
		Line.Steps.StableSort([](const FGFSideQuestStepRule& A, const FGFSideQuestStepRule& B) { return A.Order < B.Order; });
	}
}

const FGFSideQuestCatalog& FGFSideQuestLine::ActiveRules() const
{
	return Catalog ? *Catalog : FGFSideQuestCatalog::Builtin();
}

const FGFSideQuestStepRule* FGFSideQuestLine::ActiveStep() const
{
	const FGFSideQuestProgress* Progress = Board.Active();
	const FGFSideQuestLineRule* Rule = Progress && Progress->Status == EGFSideQuestStatus::Active ? ActiveRules().FindLine(Progress->LineId) : nullptr;
	return Rule ? Rule->FindStep(Progress->StepId) : nullptr;
}

bool FGFSideQuestLine::IsUnlocked(const FGFSideQuestLineRule& Line, FName InFactionId, EGFFactionRank InRank)
{
	return Line.IsPlayable()
		&& FGFSideQuestCatalog::SameTown(InFactionId, Line.TownId)
		&& static_cast<uint8>(InRank) >= static_cast<uint8>(Line.UnlockRank);
}

void FGFSideQuestLine::Refresh()
{
	for (const FGFSideQuestLineRule& Rule : ActiveRules().Lines)
	{
		FGFSideQuestProgress* Progress = Board.Find(Rule.LineId);
		if (!Progress)
		{
			FGFSideQuestProgress Added;
			Added.LineId = Rule.LineId;
			Progress = &Board.Lines.Add_GetRef(Added);
		}
		if (Progress->Status == EGFSideQuestStatus::Active || Progress->Status == EGFSideQuestStatus::Done)
		{
			continue;
		}
		Progress->Status = IsUnlocked(Rule, FactionId, Rank) ? EGFSideQuestStatus::Available : EGFSideQuestStatus::Locked;
		Progress->StepId = NAME_None;
		Progress->Count = 0;
	}
}

FGFSideQuestApplyResult FGFSideQuestLine::Apply(const FGFSideQuestEvent& Event)
{
	if (static_cast<uint8>(Event.Kind) > static_cast<uint8>(GFSideQuestLastKind))
	{
		return GFSideQuestLinePrivate::Make(EGFQuestResult::Ignored, FGFQuestCodes::Unknown);
	}
	FGFSideQuestApplyResult Result;
	if (Event.Kind == EGFSideQuestEventKind::TalkAccepted && (Board.ActiveLineId.IsNone() || Event.LineId != Board.ActiveLineId))
	{
		Result = ApplyAccept(Event);
	}
	else
	{
		Result = ApplyStep(Event);
	}
	// Ignored and Refused leave the board exactly as it was.
	if (Result.Result == EGFQuestResult::Ok)
	{
		Refresh();
	}
	return Result;
}

FGFSideQuestApplyResult FGFSideQuestLine::ApplyAccept(const FGFSideQuestEvent& Event)
{
	using namespace GFSideQuestLinePrivate;
	const FGFSideQuestLineRule* Rule = ActiveRules().FindLine(Event.LineId);
	if (!Rule)
	{
		return Make(EGFQuestResult::Ignored, FGFQuestCodes::Unknown);
	}
	const FGFSideQuestProgress* Existing = Board.Find(Rule->LineId);
	if (Existing && Existing->Status == EGFSideQuestStatus::Done)
	{
		return Make(EGFQuestResult::Ignored, FGFSideQuestCodes::Already, Rule->LineId);
	}
	if (!Board.ActiveLineId.IsNone())
	{
		return Make(EGFQuestResult::Refused, FGFSideQuestCodes::OtherActive, Rule->LineId);
	}
	if (!Rule->IsPlayable())
	{
		return Make(EGFQuestResult::Refused, FGFSideQuestCodes::NotReady, Rule->LineId);
	}
	if (!FGFSideQuestCatalog::SameTown(Event.TownId, Rule->TownId) || !FGFSideQuestCatalog::SameTown(FactionId, Rule->TownId))
	{
		return Make(EGFQuestResult::Refused, FGFQuestCodes::WrongTown, Rule->LineId);
	}
	if (static_cast<uint8>(Rank) < static_cast<uint8>(Rule->UnlockRank))
	{
		return Make(EGFQuestResult::Refused, FGFSideQuestCodes::RankLow, Rule->LineId);
	}
	FGFSideQuestProgress* Progress = Board.Find(Rule->LineId);
	if (!Progress)
	{
		FGFSideQuestProgress Added;
		Added.LineId = Rule->LineId;
		Progress = &Board.Lines.Add_GetRef(Added);
	}
	Progress->Status = EGFSideQuestStatus::Active;
	Progress->StepId = Rule->Steps[0].StepId;
	Progress->Count = 0;
	Board.ActiveLineId = Rule->LineId;

	FGFSideQuestApplyResult Result = Make(EGFQuestResult::Ok, FGFQuestCodes::Ok, Rule->LineId);
	Result.bAccepted = true;
	if (Rule->Steps[0].AdvanceKind == EGFSideQuestEventKind::TalkAccepted)
	{
		Advance(*Rule, *Progress, Rule->Steps[0].Goal, Result);
	}
	return Result;
}

FGFSideQuestApplyResult FGFSideQuestLine::ApplyStep(const FGFSideQuestEvent& Event)
{
	using namespace GFSideQuestLinePrivate;
	const FGFSideQuestLineRule* Rule = ActiveRules().FindLine(Board.ActiveLineId);
	FGFSideQuestProgress* Progress = Rule ? Board.Find(Rule->LineId) : nullptr;
	if (!Rule || !Progress || Progress->Status != EGFSideQuestStatus::Active)
	{
		return Make(EGFQuestResult::Ignored, FGFQuestCodes::OutOfOrder);
	}
	if (!Event.LineId.IsNone() && Event.LineId != Rule->LineId)
	{
		return Make(EGFQuestResult::Ignored, FGFQuestCodes::OutOfOrder, Rule->LineId);
	}
	const FGFSideQuestStepRule* Step = Rule->FindStep(Progress->StepId);
	if (Event.Kind == EGFSideQuestEventKind::TalkAccepted)
	{
		// A check-in with the elder moves a later TalkAccepted step. The first step is only ever done by the accept.
		const bool bCheckIn = Step && Step->AdvanceKind == EGFSideQuestEventKind::TalkAccepted && Rule->IndexOf(Step->StepId) > 0;
		if (!bCheckIn)
		{
			return Make(EGFQuestResult::Ignored, FGFSideQuestCodes::Already, Rule->LineId);
		}
		if (!FGFSideQuestCatalog::SameTown(Event.TownId, Rule->TownId))
		{
			return Make(EGFQuestResult::Refused, FGFQuestCodes::WrongTown, Rule->LineId);
		}
		FGFSideQuestApplyResult CheckIn = Make(EGFQuestResult::Ok, FGFQuestCodes::Ok, Rule->LineId);
		Advance(*Rule, *Progress, FMath::Max(1, Step->Goal), CheckIn);
		return CheckIn;
	}
	if (!Step || Event.Kind != Step->AdvanceKind)
	{
		return Make(EGFQuestResult::Ignored, FGFQuestCodes::OutOfOrder, Rule->LineId);
	}
	if (!Event.TownId.IsNone() && !FGFSideQuestCatalog::SameTown(Event.TownId, Rule->TownId))
	{
		return Make(EGFQuestResult::Refused, FGFQuestCodes::WrongTown, Rule->LineId);
	}

	FGFSideQuestApplyResult Result = Make(EGFQuestResult::Ok, FGFQuestCodes::Ok, Rule->LineId);
	if (Event.Kind == EGFSideQuestEventKind::TurnIn)
	{
		TArray<FGFItemStack> Charge;
		FName Why = FGFQuestCodes::Short;
		const int32 Need = FMath::Max(1, Step->Goal - Progress->Count);
		if (!FGFQuestLine::ChargeOffer(Event.Offer(), Step->Items, Need, Charge, Why))
		{
			return Make(EGFQuestResult::Refused, Why, Rule->LineId);
		}
		Result.Charge = MoveTemp(Charge);
		Advance(*Rule, *Progress, Need, Result);
		return Result;
	}
	if ((Event.Kind == EGFSideQuestEventKind::HeldDelivered && Event.ItemId.IsNone())
		|| (Step->Items.Num() > 0 && !Step->Items.Contains(Event.ItemId)))
	{
		return Make(EGFQuestResult::Refused, FGFQuestCodes::WrongItem, Rule->LineId);
	}
	const int32 Add = Event.Kind == EGFSideQuestEventKind::HeldDelivered ? 1 : FMath::Max(1, Event.Count);
	Advance(*Rule, *Progress, Add, Result);
	return Result;
}

void FGFSideQuestLine::Advance(const FGFSideQuestLineRule& Line, FGFSideQuestProgress& Progress, int32 Add, FGFSideQuestApplyResult& Result)
{
	const int32 Index = Line.IndexOf(Progress.StepId);
	if (!Line.Steps.IsValidIndex(Index))
	{
		return;
	}
	const int32 Goal = FMath::Max(1, Line.Steps[Index].Goal);
	Progress.Count = FMath::Min(Goal, Progress.Count + FMath::Max(0, Add));
	if (Progress.Count < Goal)
	{
		return;
	}
	if (Line.Steps.IsValidIndex(Index + 1))
	{
		Progress.StepId = Line.Steps[Index + 1].StepId;
		Progress.Count = 0;
		return;
	}
	Progress.Status = EGFSideQuestStatus::Done;
	Progress.StepId = NAME_None;
	Progress.Count = 0;
	Board.ActiveLineId = NAME_None;
	Result.bCompleted = true;
}

FGFOnSideQuestRaised& FGFSideQuestEvents::OnRaised()
{
	static FGFOnSideQuestRaised Delegate;
	return Delegate;
}

void FGFSideQuestEvents::Raise(UWorld* World, const FGFSideQuestEvent& Event)
{
	OnRaised().Broadcast(World, Event);
}

static FGFSideQuestEvent GFSideQuestEventBase(EGFSideQuestEventKind Kind, const FString& PlayerKey, FName TownId)
{
	FGFSideQuestEvent Event;
	Event.Kind = Kind;
	Event.PlayerKey = PlayerKey;
	Event.TownId = TownId;
	return Event;
}

FGFSideQuestEvent FGFSideQuestEvents::TalkAccepted(const FString& PlayerKey, FName LineId, FName TownId)
{
	FGFSideQuestEvent Event = GFSideQuestEventBase(EGFSideQuestEventKind::TalkAccepted, PlayerKey, TownId);
	Event.LineId = LineId;
	return Event;
}

FGFSideQuestEvent FGFSideQuestEvents::Progress(const FString& PlayerKey, EGFSideQuestEventKind Kind, FName TownId, int32 Count)
{
	FGFSideQuestEvent Event = GFSideQuestEventBase(Kind, PlayerKey, TownId);
	Event.Count = Count;
	return Event;
}

FGFSideQuestEvent FGFSideQuestEvents::TurnIn(const FString& PlayerKey, const TArray<FGFItemStack>& Offer, FName TownId)
{
	FGFSideQuestEvent Event = GFSideQuestEventBase(EGFSideQuestEventKind::TurnIn, PlayerKey, TownId);
	Event.Stacks = Offer;
	return Event;
}

FGFSideQuestEvent FGFSideQuestEvents::HeldDelivered(const FString& PlayerKey, FName ItemId, FName TownId)
{
	FGFSideQuestEvent Event = GFSideQuestEventBase(EGFSideQuestEventKind::HeldDelivered, PlayerKey, TownId);
	Event.ItemId = ItemId;
	Event.Count = 1;
	return Event;
}
