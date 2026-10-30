#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GFRhythmSettings.generated.h"

/**
 * Beat Hop player settings (saved per user in GameUserSettings.ini, not in the project config).
 * "Listen to" picks an OUTPUT device for WASAPI loopback. Empty = "System default output".
 * There is deliberately no microphone / input device option.
 */
UCLASS(config = GameUserSettings, meta = (DisplayName = "Beat Hop"))
class GFRHYTHM_API UGFRhythmSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGFRhythmSettings();

	/** Endpoint ID of the render device to listen to. Empty = follow the system default output. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Live mode", meta = (DisplayName = "Listen to"))
	FString ListenDeviceId;

	/** Extra latency per output device (speaker/Bluetooth/display), ms, -400..400. Key = endpoint ID ("" = system default). */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Live mode")
	TMap<FString, float> LiveLatencyMsByDevice;

	/** Songs-mode calibration offset, ms (separate from live). */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Songs mode", meta = (ClampMin = "-400", ClampMax = "400"))
	float SongsOffsetMs = 0.f;

	/** Short quiet clicks on hits in live mode (off by default so loopback does not hear the game). */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Live mode")
	bool bLiveHitSounds = false;

	UFUNCTION(BlueprintPure, Category = "GF|Rhythm")
	bool UsesSystemDefault() const { return ListenDeviceId.IsEmpty(); }

	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	void SetListenDevice(const FString& EndpointIdOrEmptyForDefault);

	UFUNCTION(BlueprintPure, Category = "GF|Rhythm")
	float GetLiveLatencyMs() const;

	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	void SaveSettings();

	static UGFRhythmSettings* GetMutable() { return GetMutableDefault<UGFRhythmSettings>(); }
};
