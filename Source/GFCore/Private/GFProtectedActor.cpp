#include "GFProtectedActor.h"
#include "GameFramework/Actor.h"

const FName FGFProtectedActor::Tag(TEXT("GF.FactionProtected"));

bool FGFProtectedActor::Is(const AActor* Actor)
{
	return Actor && Actor->ActorHasTag(Tag);
}
