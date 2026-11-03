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

	/** Ledger account for this town's elder. One account per town. */
	FString LedgerKey() const;

	virtual void BeginPlay() override;

private:
	void HandleTalk(AActor* Townsfolk, APawn* Pawn);
};
