#include "GFHUD.h"

namespace
{
	/** Menus that hold the pawn and can sit under chat. */
	bool IsMenu(EGFHudModal Modal)
	{
		return Modal == EGFHudModal::Pause || Modal == EGFHudModal::PauseConfirm || Modal == EGFHudModal::Elder
			|| Modal == EGFHudModal::Inventory;
	}
}

bool FGFHudModalRules::IsPause(EGFHudModal Modal)
{
	return Modal == EGFHudModal::Pause || Modal == EGFHudModal::PauseConfirm;
}

bool FGFHudModalRules::HoldsPawn(EGFHudModal Modal, EGFHudModal Restore)
{
	if (IsMenu(Modal))
	{
		return true;
	}
	if (Modal == EGFHudModal::Chat)
	{
		return IsMenu(Restore);
	}
	return false;
}

bool FGFHudModalRules::FreesMouse(EGFHudModal Modal)
{
	return IsMenu(Modal);
}

bool FGFHudModalRules::ShowsFieldHud(EGFHudModal Modal, EGFHudModal Restore)
{
	auto Blocks = [](EGFHudModal Value)
	{
		return Value == EGFHudModal::Pause || Value == EGFHudModal::PauseConfirm || Value == EGFHudModal::Elder;
	};
	return !Blocks(Modal) && !Blocks(Restore);
}

bool FGFHudModalRules::ConfirmsReturn(EGFHudModal Modal)
{
	return Modal == EGFHudModal::PauseConfirm;
}

bool FGFHudModalRules::HasElder(EGFHudModal Modal, EGFHudModal Restore)
{
	return Modal == EGFHudModal::Elder || (Modal == EGFHudModal::Chat && Restore == EGFHudModal::Elder);
}

bool FGFHudModalRules::EndsElderSession(const FState& Before, const FState& After)
{
	return HasElder(Before.Modal, Before.Restore) && !HasElder(After.Modal, After.Restore);
}

FGFHudModalRules::FState FGFHudModalRules::Transition(EGFHudModal Modal, EGFHudModal Restore, EGFHudModalEvent Event)
{
	FState Out;
	Out.Modal = Modal;
	Out.Restore = Restore;

	auto Go = [&Out](EGFHudModal Next)
	{
		Out.Modal = Next;
		Out.Restore = EGFHudModal::Game;
	};

	const bool bMenuUnderChat = IsMenu(Restore);

	switch (Event)
	{
	case EGFHudModalEvent::Reset:
		Go(EGFHudModal::Game);
		break;
	case EGFHudModalEvent::TogglePause:
		if (Modal == EGFHudModal::Game)
		{
			Go(EGFHudModal::Pause);
		}
		else if (Modal == EGFHudModal::Pause || Modal == EGFHudModal::PauseConfirm)
		{
			Go(EGFHudModal::Game);
		}
		break;
	case EGFHudModalEvent::OpenPause:
		if (Modal == EGFHudModal::Game)
		{
			Go(EGFHudModal::Pause);
		}
		else if (Modal == EGFHudModal::Chat && Restore != EGFHudModal::Elder)
		{
			Out.Restore = EGFHudModal::Pause;
		}
		break;
	case EGFHudModalEvent::ClosePause:
		if (Modal == EGFHudModal::Pause || Modal == EGFHudModal::PauseConfirm)
		{
			Go(EGFHudModal::Game);
		}
		else if (Modal == EGFHudModal::Chat && (Restore == EGFHudModal::Pause || Restore == EGFHudModal::PauseConfirm))
		{
			Out.Restore = EGFHudModal::Game;
		}
		break;
	case EGFHudModalEvent::OpenConfirm:
		if (Modal == EGFHudModal::Game || Modal == EGFHudModal::Pause || Modal == EGFHudModal::Elder || Modal == EGFHudModal::Aim
			|| Modal == EGFHudModal::Inventory)
		{
			Go(EGFHudModal::PauseConfirm);
		}
		else if (Modal == EGFHudModal::Chat && Restore != EGFHudModal::Elder)
		{
			Out.Restore = EGFHudModal::PauseConfirm;
		}
		break;
	case EGFHudModalEvent::CancelConfirm:
		if (Modal == EGFHudModal::PauseConfirm)
		{
			Go(EGFHudModal::Pause);
		}
		break;
	case EGFHudModalEvent::OpenChat:
		if (Modal != EGFHudModal::Chat && Modal != EGFHudModal::Aim)
		{
			Out.Restore = IsMenu(Modal) ? Modal : EGFHudModal::Game;
			Out.Modal = EGFHudModal::Chat;
		}
		break;
	case EGFHudModalEvent::CloseChat:
		if (Modal == EGFHudModal::Chat)
		{
			Out.Modal = bMenuUnderChat ? Restore : EGFHudModal::Game;
			Out.Restore = EGFHudModal::Game;
		}
		break;
	case EGFHudModalEvent::OpenAim:
		if (Modal == EGFHudModal::Game)
		{
			Go(EGFHudModal::Aim);
		}
		break;
	case EGFHudModalEvent::CloseAim:
		if (Modal == EGFHudModal::Aim)
		{
			Go(EGFHudModal::Game);
		}
		break;
	case EGFHudModalEvent::OpenElder:
		// The open is a server reply, so it can land after the player opened something else. It never replaces
		// pause, the confirm, the bag or a menu under chat; the HUD then closes the server session instead.
		if (Modal == EGFHudModal::Chat && Restore == EGFHudModal::Game)
		{
			Out.Restore = EGFHudModal::Elder;
		}
		else if (Modal == EGFHudModal::Game)
		{
			Go(EGFHudModal::Elder);
		}
		break;
	case EGFHudModalEvent::CloseElder:
		if (Modal == EGFHudModal::Elder)
		{
			Go(EGFHudModal::Game);
		}
		else if (Modal == EGFHudModal::Chat && Restore == EGFHudModal::Elder)
		{
			Out.Restore = EGFHudModal::Game;
		}
		break;
	case EGFHudModalEvent::OpenInventory:
		if (Modal == EGFHudModal::Game)
		{
			Go(EGFHudModal::Inventory);
		}
		break;
	case EGFHudModalEvent::CloseInventory:
		if (Modal == EGFHudModal::Inventory)
		{
			Go(EGFHudModal::Game);
		}
		else if (Modal == EGFHudModal::Chat && Restore == EGFHudModal::Inventory)
		{
			Out.Restore = EGFHudModal::Game;
		}
		break;
	case EGFHudModalEvent::ToggleInventory:
		if (Modal == EGFHudModal::Game)
		{
			Go(EGFHudModal::Inventory);
		}
		else if (Modal == EGFHudModal::Inventory)
		{
			Go(EGFHudModal::Game);
		}
		break;
	case EGFHudModalEvent::Escape:
		if (Modal == EGFHudModal::Chat)
		{
			Out.Modal = bMenuUnderChat ? Restore : EGFHudModal::Game;
			Out.Restore = EGFHudModal::Game;
		}
		else if (Modal == EGFHudModal::Game)
		{
			Go(EGFHudModal::Pause);
		}
		else
		{
			Go(EGFHudModal::Game);
		}
		break;
	default:
		break;
	}
	return Out;
}
