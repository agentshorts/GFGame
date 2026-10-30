#include "GFGiantDirector.h"
#include "GFGiantEventComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AGFGiantDirector::AGFGiantDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	Event = CreateDefaultSubobject<UGFGiantEventComponent>(TEXT("Event"));
}

AGFGiantDirector* AGFGiantDirector::Get(UWorld* World, bool bSpawn)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFGiantDirector> It(World); It; ++It)
	{
		return *It;
	}
	if (bSpawn && World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGFGiantDirector>(P);
	}
	return nullptr;
}

UGFGiantEventComponent* AGFGiantDirector::GetEvent(UWorld* World, bool bSpawn)
{
	AGFGiantDirector* D = Get(World, bSpawn);
	return D ? D->Event.Get() : nullptr;
}
