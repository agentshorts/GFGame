#pragma once
// Acceleration Hop (issue 334): one per hop road. While a local player is on the road it runs the music and the beat
// clock on that machine, pulses the tiles' glow on the beat through MPC_GF_HopBeat, and adds the PP_GF_HopOutline ink
// outline. Each machine hears its own music and judges its own landings; the server only checks and applies the boost
// (UGFCharacterMovementComponent).
// v2 (issue 342): the road also owns one lit target tile. The server picks it a few hops ahead of the lead player on
// the road, and replicates it (LitState) so every player sees the same tile. Landing on it in time gives
// LitBoostScale times the normal gain.
// Tempo (issue 343): the pick runs on the server's own steady grid at the road song's committed tempo and counts hops,
// never beats of anyone's music, so no player's local track (a listen host's included) moves it. Each player's own
// music sets only their beat feel, pulses and HUD timing, and their landing is judged on that beat.
// Music (issue 343): the player's own tracks from the local LicensedMusic/AccelHop folder through their Beat Hop
// playlist (GFHopMusic.h), each on its own BPM; with none, Beat Hop's committed fallback song as before.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFRhythmJudge.h"
#include "GFAccelHop.h"
#include "GFHopMusic.h"
#include "GFAccelHopRoad.generated.h"

class UBoxComponent;
class UPostProcessComponent;
class UAudioComponent;
class UMaterialParameterCollection;
class UGFBeatClockSubsystem;
class AGFAccelHopTile;

UCLASS()
class GFRHYTHM_API AGFAccelHopRoad : public AActor
{
	GENERATED_BODY()

public:
	AGFAccelHopRoad();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Half extents of the music box (along the road, across, up). The placement script sets it from the tile run. */
	UFUNCTION(BlueprintCallable, Category = "GF|AccelHop")
	void SetRoadExtent(FVector HalfExtent);

	UFUNCTION(BlueprintPure, Category = "GF|AccelHop")
	bool IsLocalPlayerOnRoad() const { return bLocalInside; }

	/** True while this road started the beat clock (and so stops it again). */
	UFUNCTION(BlueprintPure, Category = "GF|AccelHop")
	bool OwnsClock() const { return bOwnsClock; }

	UBoxComponent* GetBounds() const { return Bounds; }

	/** #347: is a world point inside this road's box (the air steer scope)? */
	bool ContainsPoint(const FVector& Point) const;

	/** Pure: is P inside a box with this transform and half extent (scale ignored)? */
	static bool BoxContains(const FTransform& BoxTransform, const FVector& HalfExtent, const FVector& Point);

	/** Pure: the mode the road plays. Live only when asked for, allowed (solo) and the capture opened. */
	static EGFBeatHopMode PickMode(bool bWantLive, int32 NumPlayers, bool bLiveStarted);

	// ---- v2 (#342): the lit target tile ----

	/**
	 * Collects every hop tile inside the box, sorted along the road (box local X, then Y, then name) so every machine
	 * builds the same list from the same map, and tells each tile its road and index. BeginPlay calls it.
	 */
	void RefreshTiles();

	const TArray<TObjectPtr<AGFAccelHopTile>>& GetTiles() const { return Tiles; }

	/** Distance along the road (box local X, cm) of a world point. */
	float AlongOf(const FVector& World) const;

	/** The tile slots the pure pick reads, in list order. */
	TArray<FGFHopTileSlot> BuildSlots() const;

	const FGFAccelHopLitState& GetLitState() const { return LitState; }
	int32 GetLitIndex() const { return LitState.Index; }

	/** The lit tile as this machine sees it (replicated index), or null. */
	UFUNCTION(BlueprintPure, Category = "GF|AccelHop")
	AGFAccelHopTile* GetLitTile() const;

	/** This machine's view: is Tile the lit target right now? (A client predicts its landing with this.) */
	bool IsTileLit(const AGFAccelHopTile* Tile) const;

	/** Server: did a landing on Tile at Now hit the lit target (now, or within ServerGraceSeconds of it going dark)? */
	bool ServerIsLitLanding(const AGFAccelHopTile* Tile, double Now) const;

	/** Server: a lit landing was paid out. The tile goes dark and the next beat picks a new one. */
	void ServerConsumeLit(const AGFAccelHopTile* Tile, double Now);

	/** Server: one pick step for an anchor at AnchorAlong moving in Direction. Tick calls it on each new beat. */
	void ServerPickFrom(float AnchorAlong, float Direction, double Now);

	/** Server: light Index (or -1 for none). Bumps the serial, keeps the old one for the grace window. */
	void ServerSetLit(int32 Index, double Now);

	/**
	 * Server: the lit pick's beat counter at Now (server seconds); a new value means a new pick step. A steady grid at
	 * GetLitPickBeatSeconds on the server's own clock, whatever music any machine (this one included) is playing.
	 */
	int64 GetLitPickBeatIndex(double Now);

	/** Seconds per lit pick step: the road song's committed beat (the fallback song's 130 BPM by default). */
	float GetLitPickBeatSeconds();

	/** Server tick picks on the beat when true. Tests turn it off and drive ServerPickFrom themselves. */
	void SetAutoPick(bool bOn) { bAutoPick = bOn; }

	const FGFAccelHopLitTuning& GetLitTuning() const { return LitTuning; }

	/** The road the local player is on in World, or null (the HUD reads the lit tile through it). */
	static AGFAccelHopRoad* FindLocalRoad(const UWorld* World);

	/** True while a track from the local music folder is playing (false on the committed fallback song). */
	UFUNCTION(BlueprintPure, Category = "GF|AccelHop")
	bool IsPlayingLocalTrack() const { return !LocalKey.IsEmpty(); }

	/** File name of the local track playing, or empty. */
	const FString& GetLocalTrackKey() const { return LocalKey; }

	/** Seconds into the local track's audio (0 when none plays). */
	float GetLocalTrackPosition() const;

	/** The session's music state (UGFHopMusicSubsystem), or this road's own when there is no game instance (tests). */
	FGFHopMusicDirector& GetMusicDirector();

	/** Start or stop the road music now, as entering or leaving the road does (tests and the console). */
	void StartMusicNow() { StartRoadMusic(); }
	void StopMusicNow() { StopRoadMusic(); }

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

	/** v2 (#342): how the lit target is picked and how bright it is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|AccelHop")
	FGFAccelHopLitTuning LitTuning;

	UPROPERTY(ReplicatedUsing = OnRep_LitState)
	FGFAccelHopLitState LitState;

	UFUNCTION()
	void OnRep_LitState();

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<AGFAccelHopTile>> Tiles;

	/** Which tile this machine has drawn lit (follows LitState, kept apart so the old one can be put out). */
	TWeakObjectPtr<AGFAccelHopTile> ShownLit;
	bool bAutoPick = true;
	int64 LastPickBeat = MIN_int64;
	float LastDirection = 1.f;
	int32 PreviousLitIndex = INDEX_NONE;
	double PreviousLitUntil = -1.0e9;
	float GridBeatSeconds = 0.f;

	void ServerTickPick(double Now);
	/** Server: the current beat index from this machine's clock, or a song-tempo grid when it has none (dedicated). */
	/** Server: the first player pawn inside the box (join order), with its along and direction. */
	bool FindAnchor(float& OutAlong, float& OutDirection);
	void ShowLit(float Pulse);

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

	/** The local track playing (issue 343): its key, where it started, when, and its length. */
	FString LocalKey;
	float LocalStartSec = 0.f;
	double LocalStartedAt = 0.0;
	float LocalDurationSec = 0.f;
	TUniquePtr<FGFHopMusicDirector> OwnDirector;

	TWeakObjectPtr<const class UDirectionalLightComponent> SunLight;
	double NextSunSearch = 0.0;

	UGFBeatClockSubsystem* Clock() const;
	bool FindLocalPawnLocation(FVector& Out) const;
	int32 PlayerCount() const;
	void StartRoadMusic();
	/** Plays Choice from the local folder on the clock. False (nothing started) when it is not local or won't load. */
	bool StartLocalTrack(UGFBeatClockSubsystem* Beat, const FGFHopChoice& Choice);
	void StopRoadMusic();
	void UpdateCollection(float Pulse);
};
