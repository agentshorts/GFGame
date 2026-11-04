#include "GFDefaultInputKeys.h"

FKey FGFDefaultInputKeys::InteractKey()
{
	return EKeys::X;
}

void FGFDefaultInputKeys::CharacterKeys(TArray<FGFMappedKey>& Out)
{
	Out.Reset();
	auto Add = [&Out](const TCHAR* Action, FKey Key, bool bSwizzle, bool bNegate)
	{
		FGFMappedKey Row;
		Row.Action = Action;
		Row.Key = Key;
		Row.bSwizzle = bSwizzle;
		Row.bNegate = bNegate;
		Out.Add(Row);
	};
	Add(TEXT("Move"), EKeys::W, true, false);
	Add(TEXT("Move"), EKeys::S, true, true);
	Add(TEXT("Move"), EKeys::D, false, false);
	Add(TEXT("Move"), EKeys::A, false, true);
	Add(TEXT("Look"), EKeys::Mouse2D, false, false);
	Add(TEXT("Jump"), EKeys::SpaceBar, false, false);
	Add(TEXT("Place"), EKeys::RightMouseButton, false, false);
	Add(TEXT("Remove"), EKeys::LeftMouseButton, false, false);
	Add(TEXT("Remove"), EKeys::Gamepad_RightTrigger, false, false);
	Add(TEXT("ToggleView"), EKeys::V, false, false);
	Add(TEXT("Hotbar"), EKeys::MouseWheelAxis, false, false);
	Add(TEXT("Sprint"), EKeys::LeftShift, false, false);
	Add(TEXT("Dash"), EKeys::LeftControl, false, false);
	Add(TEXT("GrappleSwing"), EKeys::Q, false, false);
	Add(TEXT("GrappleYank"), EKeys::E, false, false);
	Add(TEXT("ShoulderSwap"), EKeys::MiddleMouseButton, false, false);
	Add(TEXT("Claw"), EKeys::C, false, false);
	Add(TEXT("Claw"), EKeys::Gamepad_RightShoulder, false, false);
	Add(TEXT("ToggleElement"), EKeys::R, false, false);
	Add(TEXT("ToggleElement"), EKeys::Gamepad_DPad_Right, false, false);
	Add(TEXT("Overcharge"), EKeys::T, false, false);
	Add(TEXT("Interact"), InteractKey(), false, false);
}

void FGFDefaultInputKeys::HudKeys(TArray<FGFMappedKey>& Out, FKey ReturnKey)
{
	Out.Reset();
	auto Add = [&Out](const TCHAR* Action, FKey Key)
	{
		FGFMappedKey Row;
		Row.Action = Action;
		Row.Key = Key;
		Out.Add(Row);
	};
	Add(TEXT("Chat"), EKeys::Enter);
	Add(TEXT("Hint"), EKeys::H);
	Add(TEXT("CloseChat"), EKeys::Escape);
	Add(TEXT("Shardhop"), EKeys::F);
	Add(TEXT("Villain"), EKeys::G);
	Add(TEXT("Return"), ReturnKey.IsValid() ? ReturnKey : EKeys::Home);
	Add(TEXT("Inventory"), EKeys::Tab);
	Add(TEXT("Inventory"), EKeys::I);
	// Number keys select hotbar slots. While the class choice is open, 1 to 5 pick a class instead
	// (FGFHotbarRules::RouteNumberKey).
	Add(TEXT("Slot1"), EKeys::One);
	Add(TEXT("Slot2"), EKeys::Two);
	Add(TEXT("Slot3"), EKeys::Three);
	Add(TEXT("Slot4"), EKeys::Four);
	Add(TEXT("Slot5"), EKeys::Five);
	Add(TEXT("Slot6"), EKeys::Six);
	Add(TEXT("Slot7"), EKeys::Seven);
	Add(TEXT("Slot8"), EKeys::Eight);
	Add(TEXT("Slot9"), EKeys::Nine);
}

bool FGFDefaultInputKeys::KeyIsOnly(const TArray<FGFMappedKey>& Keys, FKey Key, FName Action)
{
	int32 Hits = 0;
	for (const FGFMappedKey& Row : Keys)
	{
		if (Row.Key == Key)
		{
			if (Row.Action != Action)
			{
				return false;
			}
			++Hits;
		}
	}
	return Hits == 1;
}
