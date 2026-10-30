#pragma once

// "In the belly" view for an eaten player: a dark interior camera, relevant only to its owner (the eaten player's
// controller), shown for BellyCamSeconds before the faint and respawn. Greybox stub: a camera with a dark, warm grade.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFGiantBellyCam.generated.h"

class UCameraComponent;

UCLASS()
class GFGIANT_API AGFGiantBellyCam : public AActor
{
	GENERATED_BODY()

public:
	AGFGiantBellyCam();

	UPROPERTY(VisibleAnywhere, Category = "GF|Giant")
	TObjectPtr<UCameraComponent> Camera;

	/** True on the machine whose local player is looking through it. */
	bool IsShowingLocally() const { return bShowingLocally; }

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_Owner() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void TryShow();
	bool bShowingLocally = false;
};
