#pragma once

// Test-only safe-zone provider: answers with whatever query the test hands it, so tests can register a second
// provider next to the real ones (UGFSafeZoneSubsystem::RegisterProvider).
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GFSafeZone.h"
#include "GFTestZoneProvider.generated.h"

UCLASS(Transient)
class UGFTestZoneProvider : public UObject, public IGFSafeZoneProvider
{
	GENERATED_BODY()

public:
	TSharedPtr<IGFSafeZoneQuery> Query;
	/** How often the zone was asked for (tests check per-step costs). */
	mutable int32 Reads = 0;

	virtual const IGFSafeZoneQuery* GetSafeZoneQuery() const override
	{
		++Reads;
		return Query.Get();
	}
};
