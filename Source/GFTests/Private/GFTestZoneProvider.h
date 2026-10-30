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

	virtual const IGFSafeZoneQuery* GetSafeZoneQuery() const override { return Query.Get(); }
};
