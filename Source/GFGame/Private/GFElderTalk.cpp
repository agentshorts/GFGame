#include "GFElderTalk.h"

EGFElderSession FGFElderSessionRules::Transition(EGFElderSession From, EGFElderSessionEvent Event)
{
	switch (Event)
	{
	case EGFElderSessionEvent::Opened:
		return EGFElderSession::Open;
	case EGFElderSessionEvent::Closed:
		return EGFElderSession::Closed;
	default:
		return From;
	}
}

FGFElderTalk::EAction FGFElderTalk::ActionFor(EGFQuestStep Step)
{
	switch (Step)
	{
	case EGFQuestStep::TakeCrystal:
	case EGFQuestStep::Escape:
	case EGFQuestStep::TalkToElder:
		return EAction::Accept;
	case EGFQuestStep::HuntHide:
	case EGFQuestStep::HuntSkins:
		return EAction::HandIn;
	default:
		return EAction::None;
	}
}

FGFQuestApplyResult FGFElderTalk::Accept(FGFQuestLine& Line, const FString& PlayerKey, FName TownId)
{
	return Line.Apply(FGFQuestEvents::ElderAccepted(PlayerKey, TownId));
}

TArray<FGFItemStack> FGFElderTalk::OfferFromInventory(const FGFQuestProgress& Progress, const FGFQuestCatalog& Catalog, const FGFInventory& Inventory)
{
	TArray<FName> Accepted;
	if (Progress.Step == EGFQuestStep::HuntHide)
	{
		Accepted = Catalog.ItemsFor(EGFQuestStep::HuntHide);
	}
	else if (Progress.Step == EGFQuestStep::HuntSkins)
	{
		Accepted = Catalog.SkinsFor(Progress.FactionId);
	}
	TArray<FGFItemStack> Offer;
	for (const FName ItemId : Accepted)
	{
		const int32 Count = Inventory.GetCount(ItemId);
		if (Count > 0)
		{
			Offer.Add(FGFItemStack(ItemId, Count));
		}
	}
	return Offer;
}

FString FGFElderTalk::RefusalText(FName Reason, EGFQuestStep Step)
{
	if (Reason == FGFQuestCodes::Heat)
	{
		return TEXT("This elder will not talk while the dinos are still after you.");
	}
	if (Reason == FGFQuestCodes::NoFaction)
	{
		return TEXT("There is no town to join.");
	}
	if (Reason == FGFQuestCodes::Short)
	{
		if (Step == EGFQuestStep::HuntSkins)
		{
			return TEXT("Not enough of this town's skins.");
		}
		return TEXT("Not enough Hide or Scales.");
	}
	if (Reason == FGFQuestCodes::WrongItem)
	{
		return TEXT("Those are not the items this step wants.");
	}
	if (Reason == FGFQuestCodes::WrongTown)
	{
		return TEXT("That is the wrong town.");
	}
	if (!Reason.IsNone())
	{
		return FString::Printf(TEXT("The elder refused (%s)."), *Reason.ToString());
	}
	return TEXT("The elder refused.");
}

FGFElderTalk::ESideAction FGFElderTalk::SideActionFor(const FGFSideQuestLineRule& Line, const FGFSideQuestStepRule& Step)
{
	switch (Step.AdvanceKind)
	{
	case EGFSideQuestEventKind::TurnIn:
	case EGFSideQuestEventKind::HeldDelivered:
		return ESideAction::HandIn;
	case EGFSideQuestEventKind::TalkAccepted:
		return Line.IndexOf(Step.StepId) > 0 ? ESideAction::HandIn : ESideAction::None;
	default:
		return ESideAction::None;
	}
}

FGFElderTalk::FSideOffer FGFElderTalk::SideOfferFor(const FGFSideQuestCatalog& Catalog, FName ActiveLineId, FName ActiveStepId, int32 ActiveCount,
	const TArray<FName>& DoneLineIds, FName FactionId, EGFFactionRank Rank, FName ElderTownId)
{
	FSideOffer Offer;
	if (ElderTownId.IsNone())
	{
		return Offer;
	}
	if (!ActiveLineId.IsNone())
	{
		const FGFSideQuestLineRule* Line = Catalog.FindLine(ActiveLineId);
		if (!Line || !FGFSideQuestCatalog::SameTown(Line->TownId, ElderTownId))
		{
			return Offer;
		}
		Offer.LineId = Line->LineId;
		Offer.Line = Line;
		Offer.Step = Line->FindStep(ActiveStepId);
		Offer.Count = ActiveCount;
		Offer.Action = Offer.Step ? SideActionFor(*Line, *Offer.Step) : ESideAction::None;
		return Offer;
	}
	for (const FGFSideQuestLineRule& Line : Catalog.Lines)
	{
		if (DoneLineIds.Contains(Line.LineId) || !FGFSideQuestCatalog::SameTown(Line.TownId, ElderTownId)
			|| !FGFSideQuestLine::IsUnlocked(Line, FactionId, Rank))
		{
			continue;
		}
		Offer.Action = ESideAction::Accept;
		Offer.LineId = Line.LineId;
		Offer.Line = &Line;
		return Offer;
	}
	return Offer;
}

FGFElderTalk::FSideOffer FGFElderTalk::SideOfferFor(const FGFSideQuestCatalog& Catalog, const FGFSideQuestBoard& Board, FName FactionId, EGFFactionRank Rank, FName ElderTownId)
{
	TArray<FName> Done;
	for (const FGFSideQuestProgress& Line : Board.Lines)
	{
		if (Line.Status == EGFSideQuestStatus::Done)
		{
			Done.Add(Line.LineId);
		}
	}
	// Load repairs the board. This guard only skips a view whose pointer and status still disagree.
	const FGFSideQuestProgress* Active = Board.Active();
	const bool bActive = Active && Active->Status == EGFSideQuestStatus::Active;
	return SideOfferFor(Catalog, bActive ? Active->LineId : NAME_None, bActive ? Active->StepId : NAME_None, bActive ? Active->Count : 0,
		Done, FactionId, Rank, ElderTownId);
}

TArray<FGFItemStack> FGFElderTalk::SideOfferFromInventory(const FGFSideQuestStepRule& Step, const FGFInventory& Inventory)
{
	TArray<FGFItemStack> Offer;
	for (const FName ItemId : Step.Items)
	{
		const int32 Count = Inventory.GetCount(ItemId);
		if (Count > 0)
		{
			Offer.Add(FGFItemStack(ItemId, Count));
		}
	}
	return Offer;
}

bool FGFElderTalk::SideHandInEvent(const FSideOffer& Offer, const FString& PlayerKey, FName ElderTownId, FName HeldItem, const FGFInventory& Inventory, FGFSideQuestEvent& Out)
{
	if (Offer.Action != ESideAction::HandIn || !Offer.Step)
	{
		return false;
	}
	switch (Offer.Step->AdvanceKind)
	{
	case EGFSideQuestEventKind::TurnIn:
		Out = FGFSideQuestEvents::TurnIn(PlayerKey, SideOfferFromInventory(*Offer.Step, Inventory), ElderTownId);
		break;
	case EGFSideQuestEventKind::HeldDelivered:
	{
		const FName Held = (!HeldItem.IsNone() && Inventory.GetCount(HeldItem) > 0) ? HeldItem : NAME_None;
		Out = FGFSideQuestEvents::HeldDelivered(PlayerKey, Held, ElderTownId);
		break;
	}
	case EGFSideQuestEventKind::TalkAccepted:
		Out = FGFSideQuestEvents::TalkAccepted(PlayerKey, Offer.LineId, ElderTownId);
		break;
	default:
		return false;
	}
	Out.LineId = Offer.LineId;
	return true;
}

FString FGFElderTalk::SideRefusalText(FName Reason)
{
	if (Reason == FGFSideQuestCodes::RankLow)
	{
		return TEXT("The elder wants you to rise in the town first.");
	}
	if (Reason == FGFSideQuestCodes::OtherActive)
	{
		return TEXT("Finish the town task you already have first.");
	}
	if (Reason == FGFSideQuestCodes::Already)
	{
		return TEXT("You have already taken this town task.");
	}
	if (Reason == FGFSideQuestCodes::NotReady)
	{
		return TEXT("This town task is not ready yet.");
	}
	if (Reason == FGFQuestCodes::WrongTown)
	{
		return TEXT("This task belongs to another town.");
	}
	if (Reason == FGFQuestCodes::Short)
	{
		return TEXT("Not enough of what the task asks for.");
	}
	if (Reason == FGFQuestCodes::WrongItem)
	{
		return TEXT("That is not what the task asks for.");
	}
	if (Reason == FGFQuestCodes::OutOfOrder)
	{
		return TEXT("That is not the next part of the task.");
	}
	if (!Reason.IsNone())
	{
		return FString::Printf(TEXT("The elder refused (%s)."), *Reason.ToString());
	}
	return TEXT("The elder refused.");
}
