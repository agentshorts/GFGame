#pragma once
// Acceleration Hop (issue 334): one per hop road. While a local player is on the road it runs the music and the beat
// clock on that machine (Beat Hop's built-in fallback song by default), pulses the tiles' glow on the beat through
// MPC_GF_HopBeat, and adds the PP_GF_HopOutline ink outline. Nothing here replicates: each machine hears its own music
// and judges its own landings; the server only checks and applies the boost (UGFCharacterMovementComponent).
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFRhythmJudge.h"
#include "GFAccelHopRoad.generated.h"

class UBoxComponent;
class UPostProcessComponent;
class UAudioComponent;
class UMaterialParameterCollection;
class UGFBeatClockSubsystem;

UCLASS()
class GFRHYTHM_API AGFAccelHopRoad : public AActor
{
	GENERATED_BODY()

public:
	AGFAccelHopRoad();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Half extents of the music box (along the road, across, up). The placement script sets it from the tile run. */
	UFUNCTION(BlueprintCallable, Category = "GF|AccelHop")
	void SetRoadExtent(FVector HalfExtent);

	UFUNCTION(BlueprintPure, Category = "GF|AccelHop")
	bool IsLocalPlayerOnRoad() const { return bLocalInside; }

	/** True while this road started the beat clock (and so stops it again). */
	UFUNCTION(BlueprintPure, Category = "GF|AccelHop")
	bool OwnsClock() const { return bOwnsClock; }

	UBoxComponent* GetBounds() const { return Bounds; }

	/** Pure: is P inside a box with this transform and half extent (scale ignored)? */
	static bool BoxContains(const FTransform& BoxTransform, const FVector& HalfExtent, const FVector& Point);

	/** Pure: the mode the road plays. Live only when asked for, allowed (solo) and the capture opened. */
	static EGFBeatHopMode PickMode(bool bWantLive, int32 NumPlayers, bool bLiveStarted);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	TObjectPtr<UBoxComponent> Bounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	TObjectPtr<UPostProcessComponent> Outline;

	/** Built-in song for the road; None plays Beat Hop's fallback song. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	FName SongId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	EGFBeatDifficulty SongDifficulty = EGFBeatDifficulty::Normal;

	/** Solo only (Beat Hop's session rule): follow the PC's own music by loopback instead of the built-in song. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	bool bListenToMyMusic = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0", ClampMax = "1"))
	float MusicVolume = 0.8f;

	/** Keep the music going this long after leaving the box, so a hop off the side does not cut the song. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop", meta = (ClampMin = "0"))
	float LeaveGraceSeconds = 3.f;

private:
	bool bLocalInside = false;
	bool bOwnsClock = false;
	/** The music could not start this visit; try again after the player leaves and comes back. */
	bool bStartFailed = false;
	double LastInsideTime = -1.0e9;
	float SongSeconds = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Music;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> BeatCollection;

	TWeakObjectPtr<const class UDirectionalLightComponent> SunLight;
	double NextSunSearch = 0.0;

	UGFBeatClockSubsystem* Clock() const;
	bool FindLocalPawnLocation(FVector& Out) const;
	int32 PlayerCount() const;
	void StartRoadMusic();
	void StopRoadMusic();
	void UpdateCollection(float Pulse);
};
