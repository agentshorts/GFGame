#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFWasapiLoopback.h"
#include "GFBeatHopGateSubsystem.generated.h"

/** Gate D: Beat Hop rhythm obby (M5). Stub. Also exposes the "Listen to" device list for the settings UI. */
UCLASS()
class GFRHYTHM_API UGFBeatHopGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("BeatHop"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Beat"); }

	/** "System default output" first, then active output devices only. */
	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	TArray<FGFAudioOutputDevice> GetListenToOptions() const { return FGFWasapiLoopback::EnumerateOutputDevices(); }
};
