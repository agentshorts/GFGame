#include "GFFaintHandler.h"
#include "GFPlayerState.h"
#include "GFLog.h"
#include "GameFramework/Pawn.h"

void UGFFaintStub::HandleOpenWorldFaint(AGFPlayerState* PS, const FVector& RespawnAt, FName Cause)
{
	if (!PS)
	{
		return;
	}
	Calls.Add({ PS->PlayerKey, RespawnAt, Cause });
	if (APawn* Pawn = PS->GetPawn())
	{
		Pawn->TeleportTo(RespawnAt + FVector(0.f, 0.f, 100.f), Pawn->GetActorRotation(), false, true);
	}
	UE_LOG(LogGF, Log, TEXT("Faint (stub): %s fainted (%s), respawned at %s. Material drop is up to the faint system."), *PS->PlayerKey, *Cause.ToString(), *RespawnAt.ToString());
}
