#pragma once

// Test-only copy of the "Quest" blob as it was saved before the side quest board (QL-a): errand records only.
// Bytes written from this struct stand in for an old save when checking that FGFQuestSave still reads them.
#include "CoreMinimal.h"
#include "GFQuestTypes.h"
#include "GFSideQuestTestTypes.generated.h"

USTRUCT()
struct FGFQuestSaveBeforeSideBoard
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) TArray<FGFQuestRecord> Records;
};
