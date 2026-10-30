#include "GFTerritoryTypes.h"
#include "GFGameState.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"

const FName FGFTerritoryFactions::Cinderwrights(TEXT("Cinderwrights"));
const FName FGFTerritoryFactions::Verdant(TEXT("Verdant"));
const FName FGFTerritoryFactions::Rime(TEXT("Rime"));

int32 FGFTerritoryLimits::CapMembers(int32 SessionMaxPlayers, int32 MaxPerFaction)
{
	const int32 Session = FMath::Max(0, SessionMaxPlayers);
	const int32 Seats = FMath::Max(0, MaxPerFaction);
	if (Session > 0 && Seats > 0)
	{
		return FMath::Min(Session, Seats);
	}
	return Session > 0 ? Session : Seats;
}

int32 FGFTerritoryLimits::SessionMaxPlayers(const UWorld* World)
{
	const AGameModeBase* Mode = World ? World->GetAuthGameMode() : nullptr;
	const AGameSession* Session = Mode ? Mode->GameSession : nullptr;
	return Session && Session->MaxPlayers > 0 ? Session->MaxPlayers : 0;
}

int32 FGFTerritoryLimits::FactionSeatCap(const AGFGameState* GS)
{
	return GS && GS->MaxPerFaction > 0 ? GS->MaxPerFaction : 0;
}
