#include "GFHUD.h"

bool FGFHudModalRules::IsPause(EGFHudModal Modal)
{
	return Modal == EGFHudModal::Pause || Modal == EGFHudModal::PauseConfirm;
}

bool FGFHudModalRules::HoldsPawn(EGFHudModal Modal, EGFHudModal Restore)
{
	if (Modal == EGFHudModal::Pause || Modal == EGFHudModal::PauseConfirm || Modal == EGFHudModal::Elder)
	{
		return true;
	}
	if (Modal == EGFHudModal::Chat)
	{
		return Restore == EGFHudModal::Pause || Restore == EGFHudModal::PauseConfirm || Restore == EGFHudModal::Elder;
	}
	return false;
}

bool FGFHudModalRules::FreesMouse(EGFHudModal Modal)
{
	return Modal == EGFHudModal::Pause || Modal == EGFHudModal::PauseConfirm || Modal == EGFHudModal::Elder;
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

	const bool bMenuUnderChat = Restore == EGFHudModal::Pause || Restore == EGFHudModal::PauseConfirm || Restore == EGFHudModal::Elder;

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
		if (Modal == EGFHudModal::Game || Modal == EGFHudModal::Pause || Modal == EGFHudModal::Elder || Modal == EGFHudModal::Aim)
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
			Out.Restore = (Modal == EGFHudModal::Pause || Modal == EGFHudModal::PauseConfirm || Modal == EGFHudModal::Elder)
				? Modal
				: EGFHudModal::Game;
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
		if (Modal == EGFHudModal::Chat)
		{
			Out.Restore = EGFHudModal::Elder;
		}
		else if (Modal != EGFHudModal::Aim && Modal != EGFHudModal::Elder)
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
