#pragma once

// The town elder. Same townsfolk as a villager, with the elder role, so a later menu can tell them apart.

#include "CoreMinimal.h"
#include "GFVillager.h"
#include "GFElder.generated.h"

UCLASS()
class GFGAME_API AGFElder : public AGFVillager
{
	GENERATED_BODY()

public:
	AGFElder();
};
