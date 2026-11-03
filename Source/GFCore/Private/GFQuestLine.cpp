#include "GFQuestLine.h"
#include "GFDataTables.h"
#include "Engine/DataTable.h"
#include <initializer_list>

const FName FGFQuestCodes::Ok(TEXT("Ok"));
const FName FGFQuestCodes::OutOfOrder(TEXT("OutOfOrder"));
const FName FGFQuestCodes::Unknown(TEXT("Unknown"));
const FName FGFQuestCodes::Heat(TEXT("Heat"));
const FName FGFQuestCodes::Short(TEXT("Short"));
const FName FGFQuestCodes::WrongItem(TEXT("WrongItem"));
const FName FGFQuestCodes::WrongTown(TEXT("WrongTown"));
const FName FGFQuestCodes::NoFaction(TEXT("NoFaction"));
const FName FGFQuestCodes::LowScore(TEXT("LowScore"));
const FName FGFQuestCodes::Already(TEXT("Already"));

namespace GFQuestLinePrivate
{
	static FString ShortEnumName(const UEnum* Enum, int64 Value)
	{
		FString Name = Enum ? Enum->GetNameStringByValue(Value) : FString();
		int32 Colon = INDEX_NONE;
		if (Name.FindLastChar(TEXT(':'), Colon))
		{
			Name.RightChopInline(Colon + 1);
		}
		return Name;
	}

	static void Promote(FGFQuestProgress& Progress, EGFFactionRank Rank)
	{
		if (static_cast<uint8>(Rank) > static_cast<uint8>(Progress.Rank))
		{
			Progress.Rank = Rank;
		}
	}

	static void AddCharge(TArray<FGFItemStack>& Charge, FName ItemId, int32 Count)
	{
		for (FGFItemStack& Stack : Charge)
		{
			if (Stack.ItemId == ItemId)
			{
				Stack.Count += Count;
				return;
			}
		}
		Charge.Add(FGFItemStack(ItemId, Count));
	}

	/** Fills Charge with exactly Goal of the accepted items, in offer order. False leaves Charge empty. */
	static bool ChargeOffer(const TArray<FGFItemStack>& Offer, const TArray<FName>& Accepted, int32 Goal, TArray<FGFItemStack>& Charge, FName& OutReason)
	{
		Charge.Reset();
		if (Goal <= 0)
		{
			OutReason = FGFQuestCodes::Short;
			return false;
		}
		int32 Have = 0;
		bool bAny = false;
		for (const FGFItemStack& Stack : Offer)
		{
			if (Stack.ItemId.IsNone() || Stack.Count <= 0)
			{
				continue;
			}
			bAny = true;
			if (Accepted.Contains(Stack.ItemId))
			{
				Have += Stack.Count;
			}
		}
		if (!bAny || Have < Goal)
		{
			OutReason = (bAny && Have == 0) ? FGFQuestCodes::WrongItem : FGFQuestCodes::Short;
			return false;
		}
		int32 Need = Goal;
		for (const FGFItemStack& Stack : Offer)
		{
			if (Need <= 0)
			{
				break;
			}
			if (Stack.ItemId.IsNone() || Stack.Count <= 0 || !Accepted.Contains(Stack.ItemId))
			{
				continue;
			}
			const int32 Take = FMath::Min(Need, Stack.Count);
			AddCharge(Charge, Stack.ItemId, Take);
			Need -= Take;
		}
		if (Need > 0)
		{
			Charge.Reset();
			OutReason = FGFQuestCodes::Short;
			return false;
		}
		OutReason = FGFQuestCodes::Ok;
		return true;
	}
}

TArray<FGFItemStack> FGFQuestEvent::Offer() const
{
	if (Kind != EGFQuestEventKind::TurnIn)
	{
		return {};
	}
	if (Stacks.Num() > 0)
	{
		return Stacks;
	}
	if (!ItemId.IsNone() && Count > 0)
	{
		return { FGFItemStack(ItemId, Count) };
	}
	return {};
}

bool FGFQuestCatalog::TryStep(FName Id, EGFQuestStep& Out)
{
	const UEnum* Enum = StaticEnum<EGFQuestStep>();
	if (!Enum || Id.IsNone())
	{
		return false;
	}
	int64 Value = Enum->GetValueByName(Id);
	if (Value == INDEX_NONE)
	{
		Value = Enum->GetValueByNameString(FString::Printf(TEXT("EGFQuestStep::%s"), *Id.ToString()));
	}
	if (Value == INDEX_NONE)
	{
		return false;
	}
	const FString Name = GFQuestLinePrivate::ShortEnumName(Enum, Value);
	if (Name.EndsWith(TEXT("_MAX")))
	{
		return false;
	}
	Out = static_cast<EGFQuestStep>(Value);
	return true;
}

FName FGFQuestCatalog::StepId(EGFQuestStep Step)
{
	const UEnum* Enum = StaticEnum<EGFQuestStep>();
	return FName(*GFQuestLinePrivate::ShortEnumName(Enum, static_cast<int64>(Step)));
}

const FGFQuestCatalog& FGFQuestCatalog::Builtin()
{
	static const FGFQuestCatalog Catalog = []()
	{
		FGFQuestCatalog Built;
		auto Add = [&Built](EGFQuestStep Step, int32 Goal, EGFFactionRank Rank, std::initializer_list<const TCHAR*> Items)
		{
			FGFQuestStepRule Rule;
			Rule.Step = Step;
			Rule.Goal = Goal;
			Rule.Rank = Rank;
			for (const TCHAR* Item : Items)
			{
				Rule.Items.Add(Item);
			}
			Built.Steps.Add(Rule);
		};
		Add(EGFQuestStep::TakeCrystal, 1, EGFFactionRank::Outsider, {});
		Add(EGFQuestStep::Escape, 1, EGFFactionRank::Outsider, {});
		Add(EGFQuestStep::TalkToElder, 1, EGFFactionRank::Initiate, {});
		Add(EGFQuestStep::HuntHide, 6, EGFFactionRank::Member, { TEXT("Hide"), TEXT("Scales") });
		Add(EGFQuestStep::HuntSkins, 2, EGFFactionRank::Champion, {});
		Add(EGFQuestStep::CraftLook, 1, EGFFactionRank::Champion, {});
		Add(EGFQuestStep::WinShow, 1, EGFFactionRank::Leader, {});
		Add(EGFQuestStep::Done, 0, EGFFactionRank::Leader, {});

		auto Town = [&Built](const TCHAR* TownId, std::initializer_list<const TCHAR*> Skins)
		{
			FGFQuestTownSkins Row;
			Row.TownId = TownId;
			Row.Goal = 2;
			for (const TCHAR* Skin : Skins)
			{
				Row.Items.Add(Skin);
			}
			Built.Towns.Add(Row);
		};
		Town(TEXT("Rime"), { TEXT("Skin_Nipper"), TEXT("Skin_Raptor") });
		Town(TEXT("Verdant"), { TEXT("Skin_Tusksaw"), TEXT("Skin_Lanternmaw") });
		Town(TEXT("Cinderwrights"), { TEXT("Skin_Scythepaw"), TEXT("Skin_Hornback") });
		return Built;
	}();
	return Catalog;
}

FGFQuestCatalog FGFQuestCatalog::FromTable(const UDataTable* Table)
{
	FGFQuestCatalog Out = Builtin();
	if (!Table)
	{
		return Out;
	}
	Table->ForeachRow<FGFQuestRow>(TEXT("GFQuest"), [&Out](const FName& RowName, const FGFQuestRow& Row)
	{
		EGFQuestStep Step;
		if (TryStep(RowName, Step))
		{
			for (FGFQuestStepRule& Rule : Out.Steps)
			{
				if (Rule.Step == Step)
				{
					Rule.Goal = Row.Goal;
					Rule.Items = Row.Items;
					Rule.Rank = Row.RankOnComplete;
					break;
				}
			}
			return;
		}
		if (Row.TownId.IsNone())
		{
			return;
		}
		for (FGFQuestTownSkins& Town : Out.Towns)
		{
			if (Town.TownId == Row.TownId)
			{
				Town.Items = Row.Items;
				Town.Goal = Row.Goal;
				return;
			}
		}
		FGFQuestTownSkins Town;
		Town.TownId = Row.TownId;
		Town.Items = Row.Items;
		Town.Goal = Row.Goal;
		Out.Towns.Add(Town);
	});
	return Out;
}

FGFQuestCatalog FGFQuestCatalog::Load()
{
	return FromTable(FGFDataTables::Get<FGFQuestRow>(TEXT("DT_Quests")));
}

const FGFQuestStepRule* FGFQuestCatalog::FindStep(EGFQuestStep Step) const
{
	for (const FGFQuestStepRule& Rule : Steps)
	{
		if (Rule.Step == Step)
		{
			return &Rule;
		}
	}
	return nullptr;
}

const FGFQuestTownSkins* FGFQuestCatalog::FindTown(FName TownId) const
{
	for (const FGFQuestTownSkins& Town : Towns)
	{
		if (Town.TownId == TownId)
		{
			return &Town;
		}
	}
	return nullptr;
}

int32 FGFQuestCatalog::GoalFor(EGFQuestStep Step) const
{
	const FGFQuestStepRule* Rule = FindStep(Step);
	return Rule ? Rule->Goal : 0;
}

TArray<FName> FGFQuestCatalog::ItemsFor(EGFQuestStep Step) const
{
	const FGFQuestStepRule* Rule = FindStep(Step);
	return Rule ? Rule->Items : TArray<FName>();
}

EGFFactionRank FGFQuestCatalog::RankAfter(EGFQuestStep Step) const
{
	const FGFQuestStepRule* Rule = FindStep(Step);
	return Rule ? Rule->Rank : EGFFactionRank::Outsider;
}

TArray<FName> FGFQuestCatalog::SkinsFor(FName TownId) const
{
	const FGFQuestTownSkins* Town = FindTown(TownId);
	return Town ? Town->Items : TArray<FName>();
}

const FGFQuestCatalog& FGFQuestLine::ActiveRules() const
{
	return Catalog ? *Catalog : FGFQuestCatalog::Builtin();
}

FGFQuestApplyResult FGFQuestLine::Apply(const FGFQuestEvent& Event)
{
	FGFQuestApplyResult Result;
	Result.Reason = FGFQuestCodes::OutOfOrder;
	if (static_cast<uint8>(Event.Kind) > static_cast<uint8>(EGFQuestEventKind::ShowPlaced))
	{
		Result.Reason = FGFQuestCodes::Unknown;
		return Result;
	}
	switch (Progress.Step)
	{
	case EGFQuestStep::TakeCrystal:
		if (Event.Kind == EGFQuestEventKind::CrystalTaken) return ApplyCrystal(Event);
		if (Event.Kind == EGFQuestEventKind::ElderAccepted) return ApplyAccept(Event);
		return Result;
	case EGFQuestStep::Escape:
		if (Event.Kind == EGFQuestEventKind::HeatCleared) return ApplyHeatCleared(Event);
		if (Event.Kind == EGFQuestEventKind::ElderAccepted) return ApplyAccept(Event);
		return Result;
	case EGFQuestStep::TalkToElder:
		if (Event.Kind == EGFQuestEventKind::ElderAccepted) return ApplyAccept(Event);
		return Result;
	case EGFQuestStep::HuntHide:
	case EGFQuestStep::HuntSkins:
		if (Event.Kind == EGFQuestEventKind::TurnIn) return ApplyTurnIn(Event);
		return Result;
	case EGFQuestStep::CraftLook:
		if (Event.Kind == EGFQuestEventKind::OutfitCrafted || Event.Kind == EGFQuestEventKind::OutfitWorn) return ApplyCraft(Event);
		return Result;
	case EGFQuestStep::WinShow:
		if (Event.Kind == EGFQuestEventKind::ShowEntered || Event.Kind == EGFQuestEventKind::ShowPlaced) return ApplyShow(Event);
		return Result;
	case EGFQuestStep::Done:
		return Result;
	default:
		Result.Reason = FGFQuestCodes::Unknown;
		return Result;
	}
}

FGFQuestApplyResult FGFQuestLine::ApplyCrystal(const FGFQuestEvent& Event)
{
	if (!Event.FactionId.IsNone() && !Progress.HeatFactions.Contains(Event.FactionId))
	{
		Progress.HeatFactions.Add(Event.FactionId);
	}
	Progress.Step = EGFQuestStep::Escape;
	Progress.Count = 0;
	GFQuestLinePrivate::Promote(Progress, ActiveRules().RankAfter(EGFQuestStep::TakeCrystal));
	FGFQuestApplyResult Result;
	Result.Result = EGFQuestResult::Ok;
	Result.Reason = FGFQuestCodes::Ok;
	return Result;
}

FGFQuestApplyResult FGFQuestLine::ApplyHeatCleared(const FGFQuestEvent& Event)
{
	FGFQuestApplyResult Result;
	Result.Reason = FGFQuestCodes::OutOfOrder;
	if (!Event.FactionId.IsNone())
	{
		if (!Progress.HeatFactions.Contains(Event.FactionId))
		{
			if (Progress.HeatFactions.Num() > 0)
			{
				Result.Result = EGFQuestResult::Refused;
				Result.Reason = FGFQuestCodes::WrongTown;
				return Result;
			}
		}
		else
		{
			Progress.HeatFactions.Remove(Event.FactionId);
		}
	}
	else
	{
		Progress.HeatFactions.Reset();
	}
	if (Progress.HeatFactions.Num() > 0)
	{
		Result.Result = EGFQuestResult::Ok;
		Result.Reason = FGFQuestCodes::Ok;
		return Result;
	}
	Progress.Step = EGFQuestStep::TalkToElder;
	Progress.Count = 0;
	GFQuestLinePrivate::Promote(Progress, ActiveRules().RankAfter(EGFQuestStep::Escape));
	Result.Result = EGFQuestResult::Ok;
	Result.Reason = FGFQuestCodes::Ok;
	return Result;
}

FGFQuestApplyResult FGFQuestLine::ApplyAccept(const FGFQuestEvent& Event)
{
	FGFQuestApplyResult Result;
	if (Event.FactionId.IsNone())
	{
		Result.Result = EGFQuestResult::Refused;
		Result.Reason = FGFQuestCodes::NoFaction;
		return Result;
	}
	if (Progress.HeatFactions.Contains(Event.FactionId))
	{
		Result.Result = EGFQuestResult::Refused;
		Result.Reason = FGFQuestCodes::Heat;
		return Result;
	}
	Progress.FactionId = Event.FactionId;
	Progress.Step = EGFQuestStep::HuntHide;
	Progress.Count = 0;
	Progress.CraftedItem = NAME_None;
	GFQuestLinePrivate::Promote(Progress, ActiveRules().RankAfter(EGFQuestStep::TalkToElder));
	Result.Result = EGFQuestResult::Ok;
	Result.Reason = FGFQuestCodes::Ok;
	Result.bClaimFaction = true;
	return Result;
}

FGFQuestApplyResult FGFQuestLine::ApplyTurnIn(const FGFQuestEvent& Event)
{
	FGFQuestApplyResult Result;
	Result.Reason = FGFQuestCodes::OutOfOrder;
	const FGFQuestCatalog& Rules = ActiveRules();
	TArray<FName> Accepted;
	int32 Goal = 0;
	EGFQuestStep Next = Progress.Step;
	EGFFactionRank Rank = Progress.Rank;
	if (Progress.Step == EGFQuestStep::HuntHide)
	{
		Accepted = Rules.ItemsFor(EGFQuestStep::HuntHide);
		Goal = Rules.GoalFor(EGFQuestStep::HuntHide);
		Next = EGFQuestStep::HuntSkins;
		Rank = Rules.RankAfter(EGFQuestStep::HuntHide);
	}
	else if (Progress.Step == EGFQuestStep::HuntSkins)
	{
		if (Progress.FactionId.IsNone())
		{
			Result.Result = EGFQuestResult::Refused;
			Result.Reason = FGFQuestCodes::NoFaction;
			return Result;
		}
		Accepted = Rules.SkinsFor(Progress.FactionId);
		if (Accepted.Num() == 0)
		{
			Result.Result = EGFQuestResult::Refused;
			Result.Reason = FGFQuestCodes::WrongItem;
			return Result;
		}
		Goal = Rules.GoalFor(EGFQuestStep::HuntSkins);
		Next = EGFQuestStep::CraftLook;
		Rank = Rules.RankAfter(EGFQuestStep::HuntSkins);
	}
	else
	{
		return Result;
	}
	TArray<FGFItemStack> Charge;
	FName Why = FGFQuestCodes::Short;
	if (!GFQuestLinePrivate::ChargeOffer(Event.Offer(), Accepted, Goal, Charge, Why))
	{
		Result.Result = EGFQuestResult::Refused;
		Result.Reason = Why;
		return Result;
	}
	Progress.Step = Next;
	Progress.Count = 0;
	Progress.CraftedItem = NAME_None;
	GFQuestLinePrivate::Promote(Progress, Rank);
	Result.Result = EGFQuestResult::Ok;
	Result.Reason = FGFQuestCodes::Ok;
	Result.Charge = MoveTemp(Charge);
	return Result;
}

FGFQuestApplyResult FGFQuestLine::ApplyCraft(const FGFQuestEvent& Event)
{
	FGFQuestApplyResult Result;
	Result.Reason = FGFQuestCodes::OutOfOrder;
	if (Event.Kind == EGFQuestEventKind::OutfitCrafted)
	{
		if (Progress.Count != 0)
		{
			return Result;
		}
		Progress.Count = 1;
		Progress.CraftedItem = Event.ItemId;
		Result.Result = EGFQuestResult::Ok;
		Result.Reason = FGFQuestCodes::Ok;
		return Result;
	}
	if (Event.Kind != EGFQuestEventKind::OutfitWorn || Progress.Count != 1)
	{
		return Result;
	}
	if (!Progress.CraftedItem.IsNone() && !Event.ItemId.IsNone() && Event.ItemId != Progress.CraftedItem)
	{
		Result.Result = EGFQuestResult::Refused;
		Result.Reason = FGFQuestCodes::WrongItem;
		return Result;
	}
	Progress.CraftedItem = NAME_None;
	Progress.Step = EGFQuestStep::WinShow;
	Progress.Count = 0;
	GFQuestLinePrivate::Promote(Progress, ActiveRules().RankAfter(EGFQuestStep::CraftLook));
	Result.Result = EGFQuestResult::Ok;
	Result.Reason = FGFQuestCodes::Ok;
	return Result;
}

FGFQuestApplyResult FGFQuestLine::ApplyShow(const FGFQuestEvent& Event)
{
	FGFQuestApplyResult Result;
	Result.Reason = FGFQuestCodes::OutOfOrder;
	if (Event.Kind == EGFQuestEventKind::ShowEntered)
	{
		if (Progress.Count != 0)
		{
			Result.Reason = FGFQuestCodes::Already;
			return Result;
		}
		Progress.Count = 1;
		Result.Result = EGFQuestResult::Ok;
		Result.Reason = FGFQuestCodes::Ok;
		return Result;
	}
	if (Event.Kind != EGFQuestEventKind::ShowPlaced || Progress.Count != 1)
	{
		return Result;
	}
	if (Event.Count != FGFQuestShowRule::WinPlace || Event.Score < FGFQuestShowRule::WinTier)
	{
		Result.Result = EGFQuestResult::Refused;
		Result.Reason = FGFQuestCodes::LowScore;
		return Result;
	}
	Progress.Step = EGFQuestStep::Done;
	Progress.Count = 0;
	Progress.CraftedItem = NAME_None;
	GFQuestLinePrivate::Promote(Progress, ActiveRules().RankAfter(EGFQuestStep::WinShow));
	Result.Result = EGFQuestResult::Ok;
	Result.Reason = FGFQuestCodes::Ok;
	Result.bBecameLeader = true;
	return Result;
}

FGFOnQuestRaised& FGFQuestEvents::OnRaised()
{
	static FGFOnQuestRaised Delegate;
	return Delegate;
}

void FGFQuestEvents::Raise(UWorld* World, const FGFQuestEvent& Event)
{
	OnRaised().Broadcast(World, Event);
}

static FGFQuestEvent GFQuestEventBase(EGFQuestEventKind Kind, const FString& PlayerKey)
{
	FGFQuestEvent Event;
	Event.Kind = Kind;
	Event.PlayerKey = PlayerKey;
	return Event;
}

FGFQuestEvent FGFQuestEvents::CrystalTaken(const FString& PlayerKey, FName TownId)
{
	FGFQuestEvent Event = GFQuestEventBase(EGFQuestEventKind::CrystalTaken, PlayerKey);
	Event.FactionId = TownId;
	return Event;
}

FGFQuestEvent FGFQuestEvents::HeatCleared(const FString& PlayerKey, FName TownId)
{
	FGFQuestEvent Event = GFQuestEventBase(EGFQuestEventKind::HeatCleared, PlayerKey);
	Event.FactionId = TownId;
	return Event;
}

FGFQuestEvent FGFQuestEvents::ElderAccepted(const FString& PlayerKey, FName TownId)
{
	FGFQuestEvent Event = GFQuestEventBase(EGFQuestEventKind::ElderAccepted, PlayerKey);
	Event.FactionId = TownId;
	return Event;
}

FGFQuestEvent FGFQuestEvents::TurnIn(const FString& PlayerKey, const TArray<FGFItemStack>& Offer)
{
	FGFQuestEvent Event = GFQuestEventBase(EGFQuestEventKind::TurnIn, PlayerKey);
	Event.Stacks = Offer;
	return Event;
}

FGFQuestEvent FGFQuestEvents::TurnIn(const FString& PlayerKey, FName ItemId, int32 Count)
{
	FGFQuestEvent Event = GFQuestEventBase(EGFQuestEventKind::TurnIn, PlayerKey);
	Event.ItemId = ItemId;
	Event.Count = Count;
	return Event;
}

FGFQuestEvent FGFQuestEvents::OutfitCrafted(const FString& PlayerKey, FName ItemId)
{
	FGFQuestEvent Event = GFQuestEventBase(EGFQuestEventKind::OutfitCrafted, PlayerKey);
	Event.ItemId = ItemId;
	return Event;
}

FGFQuestEvent FGFQuestEvents::OutfitWorn(const FString& PlayerKey, FName ItemId)
{
	FGFQuestEvent Event = GFQuestEventBase(EGFQuestEventKind::OutfitWorn, PlayerKey);
	Event.ItemId = ItemId;
	return Event;
}

FGFQuestEvent FGFQuestEvents::ShowEntered(const FString& PlayerKey)
{
	return GFQuestEventBase(EGFQuestEventKind::ShowEntered, PlayerKey);
}

FGFQuestEvent FGFQuestEvents::ShowPlaced(const FString& PlayerKey, int32 Place, int32 Tier)
{
	FGFQuestEvent Event = GFQuestEventBase(EGFQuestEventKind::ShowPlaced, PlayerKey);
	Event.Count = Place;
	Event.Score = Tier;
	return Event;
}
