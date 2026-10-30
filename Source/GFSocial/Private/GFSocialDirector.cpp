#include "GFSocialDirector.h"
#include "GFSocialComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AGFSocialDirector::AGFSocialDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	Social = CreateDefaultSubobject<UGFSocialComponent>(TEXT("Social"));
}

AGFSocialDirector* AGFSocialDirector::Get(UWorld* World, bool bSpawn)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFSocialDirector> It(World); It; ++It)
	{
		return *It;
	}
	if (bSpawn && World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGFSocialDirector>(P);
	}
	return nullptr;
}

UGFSocialComponent* AGFSocialDirector::GetSocial(UWorld* World, bool bSpawn)
{
	AGFSocialDirector* D = Get(World, bSpawn);
	return D ? D->Social.Get() : nullptr;
}
