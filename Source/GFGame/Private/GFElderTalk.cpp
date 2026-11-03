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
