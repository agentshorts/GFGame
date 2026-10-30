#pragma once

#include "CoreMinimal.h"
#include "GFZoneProp.h"
#include "GFShardstone.generated.h"

struct FGFShardstoneRow;

UCLASS()
class GFGAME_API AGFShardstone : public AGFZoneProp
{
	GENERATED_BODY()

public:
	AGFShardstone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Setup(const FGFShardstoneRow& Row, const FLinearColor& Color);

	FName GetStoneId() const { return StoneId; }

private:
	UPROPERTY(Replicated)
	FName StoneId;
};
