#include "GFActivityWin.h"

FGFOnActivityWin& FGFActivityWin::OnActivity()
{
	static FGFOnActivityWin Delegate;
	return Delegate;
}

FGFOnNewCharacter& FGFActivityWin::OnNewCharacter()
{
	static FGFOnNewCharacter Delegate;
	return Delegate;
}
