#pragma once

// Host actor for the social features: replicated, always relevant, carries UGFSocialComponent. AGFGameMode spawns
// one with the game state (StartupDirectorClasses); Get() finds it or spawns one on the server.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFSocialDirector.generated.h"

class UGFSocialComponent;

UCLASS()
class GFSOCIAL_API AGFSocialDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFSocialDirector();

	static AGFSocialDirector* Get(UWorld* World, bool bSpawn = true);
	static UGFSocialComponent* GetSocial(UWorld* World, bool bSpawn = true);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Social")
	TObjectPtr<UGFSocialComponent> Social;
};
