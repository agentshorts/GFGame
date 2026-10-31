#include "GFMovementWorldSubsystem.h"
#include "Engine/World.h"

UGFMovementWorldSubsystem* UGFMovementWorldSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGFMovementWorldSubsystem>() : nullptr;
}

void UGFMovementWorldSubsystem::SetStaminaOptIn(bool bEnable)
{
	bHasOptIn = true;
	bOptIn = bEnable;
}
