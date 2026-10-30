#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFBeatmap.h"
#include "GFRhythmJudge.h"
#include "GFWasapiLoopback.h"
#include "GFBeatHopGateSubsystem.generated.h"

/** Gate D: Beat Hop rhythm obby (M5). Song list, who-can-play-what, and the "Listen to" device list for the settings UI. */
UCLASS()
class GFRHYTHM_API UGFBeatHopGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("BeatHop"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Beat"); }
	virtual FText GetDisplayName() const override { return FText::FromString(TEXT("Beat Hop")); }

	/** "System default output" first, then active output devices only. */
	UFUNCTION(BlueprintCallable, Category = "GF|Rhythm")
	TArray<FGFAudioOutputDevice> GetListenToOptions() const { return FGFWasapiLoopback::EnumerateOutputDevices(); }

	/** Built-in songs (every one has Easy, Normal and Hard). */
	UFUNCTION(BlueprintPure, Category = "GF|Rhythm")
	TArray<FName> GetBuiltInSongIds() const;

	/** Co-op runs are built-in songs only; live mode ("Play to my music") is solo. */
	UFUNCTION(BlueprintPure, Category = "GF|Rhythm")
	bool CanStartRun(EGFBeatHopMode Mode, int32 NumPlayers, FName SongId, EGFBeatDifficulty Difficulty, FString& OutReason) const;
};
