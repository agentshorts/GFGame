#include "GFHopMusicSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

FGFHopMusicDirector* UGFHopMusicSubsystem::FindDirector(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UGFHopMusicSubsystem* Subsystem = GI ? GI->GetSubsystem<UGFHopMusicSubsystem>() : nullptr;
	return Subsystem ? &Subsystem->GetDirector() : nullptr;
}
