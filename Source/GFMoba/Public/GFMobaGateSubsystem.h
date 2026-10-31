#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFMobaGateSubsystem.generated.h"

/** Gate B. The id stays Moba. The sign reads Crystal Clash. The mode itself is still notes only. */
UCLASS()
class GFMOBA_API UGFMobaGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Moba"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Moba"); }
	virtual FText GetDisplayName() const override { return FText::FromString(TEXT("Crystal Clash")); }
};
