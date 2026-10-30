#include "GFTerritoryTypes.h"
#include "GFGameState.h"

int32 FGFTerritoryLimits::FactionSeatCap(const AGFGameState* GS)
{
	return GS && GS->MaxPerFaction > 0 ? GS->MaxPerFaction : 0;
}
