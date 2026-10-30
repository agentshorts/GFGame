#pragma once

// Host actor for the giant event: a replicated, always-relevant actor that carries UGFGiantEventComponent (the event
// logic). The giant's body (AGFGiantDino) is a separate, reusable actor the component drives.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFGiantDirector.generated.h"

class UGFGiantEventComponent;

UCLASS()
class GFGIANT_API AGFGiantDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFGiantDirector();

	/** The director in World (server spawns one if asked; clients only find the replicated one). */
	static AGFGiantDirector* Get(UWorld* World, bool bSpawn = true);
	/** Shortcut: the event component of the director in World. */
	static UGFGiantEventComponent* GetEvent(UWorld* World, bool bSpawn = true);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Giant")
	TObjectPtr<UGFGiantEventComponent> Event;
};
