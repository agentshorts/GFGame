#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GFDinoTypes.h"
#include "GFTamer.generated.h"

class APawn;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGFTamer : public UInterface
{
	GENERATED_BODY()
};

/**
 * A player as seen by dinos (implemented by the player state in GFNet, so GFCompanion needs no net code).
 * The journal accessors are server-side: the server writes, the owner gets it by owner-only replication.
 */
class GFCOMPANION_API IGFTamer
{
	GENERATED_BODY()

public:
	virtual FString GetTamerKey() const = 0;
	virtual FName GetTamerHeldItem() const = 0;
	virtual APawn* GetTamerPawn() const = 0;
	virtual bool IsTamerCrouching() const { return false; }
	/** Field Journal sharing toggle (discoveries go to friends within 30 m who also share). */
	virtual bool SharesJournal() const { return true; }
	virtual FGFDiscoveredDinoPreferences* GetMutableJournal() = 0;
	/** Called after the journal changed (replicate + save). */
	virtual void JournalChanged() = 0;
};
