#include "GFLocalProfileSubsystem.h"

void UGFLocalProfileSubsystem::StoreFromServer(const FGFCharacterData& Data)
{
	if (Data.Revision < Character.Revision)
	{
		return;
	}
	Character = Data;
	++WriteBacks;
	OnCharacterStored.Broadcast(Character);
}
