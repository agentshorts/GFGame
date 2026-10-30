#pragma once

// One unbound post-process volume at priority 1. Lerps saturation, tint, vignette, and a small
// exposure offset. No lights, no fog, no sky. The environment director is left alone.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFZoneAmbienceDirector.generated.h"

class APostProcessVolume;

UCLASS()
class GFGAME_API AGFZoneAmbienceDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFZoneAmbienceDirector();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FBlend
	{
		float Saturation = 1.f;
		FLinearColor Tint = FLinearColor::White;
		float Vignette = 0.f;
		float Exposure = 0.f;
	};

	void Apply(const FBlend& Value) const;

	TObjectPtr<APostProcessVolume> Volume;
	FBlend From;
	FBlend Current;
	FName TargetId;
	float TargetBlendSeconds = 1.5f;
	float BlendElapsed = 0.f;
	bool bHasTarget = false;
};
