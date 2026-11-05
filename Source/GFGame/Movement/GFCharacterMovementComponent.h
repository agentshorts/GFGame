#pragma once

// Server-authoritative stamina on the player character.
// Sprint, dash, swing, and yank ride FSavedMove_Character custom flags
// (FLAG_Custom_0 through FLAG_Custom_3) so the server applies the same presses the client predicted.
// The pool and the traversal step run inside PerformMovement, which is what a correction replays.
// A replicated stamina value pulls the owner back when the two diverge.
// MaxWalkSpeed stays the tuned base run speed. Sprint only changes GetMaxSpeed.

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GFBeatHopNinja.h"
#include "GFAccelHop.h"
#include "GFStaminaSim.h"
#include "GFCharacterMovementComponent.generated.h"

class UGFMovementTuning;
class AGFAccelHopTile;

/** Where this flight comes down (#342), recomputed every frame from the live flight state. */
struct GFGAME_API FGFLandingTarget
{
	bool bValid = false;
	FVector Point = FVector::ZeroVector;
	/** Seconds from now until the landing. */
	float Time = 0.f;
	/** The hop tile under the landing point, if any. */
	TWeakObjectPtr<AGFAccelHopTile> Tile;
	/** That tile is the road's lit target on this machine. */
	bool bLit = false;
};

/** The last refused walking hit the #341 log wrote, so a test reads what a playtest line would say. */
struct GFGAME_API FGFWalkRefusal
{
	FString Reason;
	FString Actor;
	FString Component;
	FString Mesh;
	FVector ImpactPoint = FVector::ZeroVector;
	FVector ImpactNormal = FVector::ZeroVector;
	/** Top of the blocking component just past the face, in cm above the pawn's feet. Negative when no top was found. */
	float FaceTopCm = -1.f;
	bool bFaceTopWalkable = false;
};

/** One judged hop tile landing on the owning machine (#348: the tutorial prompts listen to these). */
struct GFGAME_API FGFAccelHopLandingEvent
{
	TWeakObjectPtr<AGFAccelHopTile> Tile;
	FGFAccelHopHit Hit;
	/** The landing paid the lit target. */
	bool bLit = false;
	/** The boost took it (false for a second landing on the same beat). */
	bool bApplied = false;
};

/** #355: what the wall climb is doing, for anim graphs and remote players (#356). */
UENUM(BlueprintType)
enum class EGFWallClimbPhase : uint8
{
	None = 0,
	/** Straight up the wall the input pushes into. */
	Climbing = 1,
	/** Climb exit: rising to the lip, then stepping onto it. */
	Mantling = 2,
};

class UGFCharacterMovementComponent;
DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnAccelHopLanded, const UGFCharacterMovementComponent* /*Movement*/, const FGFAccelHopLandingEvent& /*Event*/);
/** Server only. One hop tile landing the server applied or rejected, for the host's pawn and for remote players. */
DECLARE_MULTICAST_DELEGATE_FourParams(FGFOnServerAccelHop, const UGFCharacterMovementComponent* /*Movement*/, const FGFAccelHopHit& /*Hit*/, EGFHopTileKind /*TileKind*/, bool /*bApplied*/);
/** Server only. A climb-exit mantle ended this move. Lip is the mantle target. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnServerMantleFinished, const UGFCharacterMovementComponent* /*Movement*/, FVector /*Lip*/);

UCLASS()
class GFGAME_API UGFCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGFCharacterMovementComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void PerformMovement(float DeltaTime) override;
	virtual float GetMaxSpeed() const override;
	virtual float GetGravityZ() const override;
	virtual bool StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& Hit, FStepDownResult* OutStepDownResult = nullptr) override;

	/**
	 * Riser assist (#341). When the engine StepUp refuses a riser that is plainly a stair (a walkable tread within
	 * MaxStepHeight just past the face, with room for the capsule on top), lift the capsule onto the tread anyway.
	 * Off falls back to the stock engine step. Tests flip it to compare the two.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Movement|Stairs")
	bool bRiserAssist = true;

	/** #355: tests and modes that must not climb can clear this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Movement|WallClimb")
	bool bWallClimbAllowed = true;

	/** How many times the riser assist lifted this pawn since spawn. Each count is a step the engine refused. */
	int32 GetRiserAssistCount() const { return RiserAssistCount; }

	/**
	 * How many blocking hits stopped this pawn while it walked, since spawn (#341). Every non-walkable blocking hit that
	 * reaches HandleImpact on the ground counts: a stair the engine step and the assist both refused, a wall, a hull, a
	 * pawn. Hits inside a step attempt that then succeeds do not count. Each one may write a LogGF "StairRefused:" line,
	 * rate limited per hit component, naming the actor, component, static mesh, hit point, impact normal and the height
	 * of the blocking face, so a playtest names the piece that felt like an invisible wall.
	 */
	int32 GetStairRefusalCount() const { return StairRefusalCount; }

	/** What the last logged StairRefused line said. Empty until one is written. */
	const FGFWalkRefusal& GetLastWalkRefusal() const { return LastWalkRefusal; }

	/** Copies BaseRunSpeed onto MaxWalkSpeed. Does not include the sprint multiplier. */
	void ApplyBaseRunSpeed();

	/** Copies jump speed and the airtime jump count from the tuning asset. */
	void ApplyJumpTuning();

	/**
	 * Copies the stair step height and walkable floor angle onto this component.
	 * Players use AGFCharacter::PlayerMaxStepHeight. Everyone else uses the tuning asset step height.
	 */
	void ApplyStairTuning();

	void SetSprintHeld(bool bHeld);
	void NotifyDashPressed();
	void SetGrappleSwingHeld(bool bHeld);
	void NotifyGrappleYank();

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	float GetStamina() const { return Sim.Stamina; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	float GetMaxStamina() const;

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsStaminaEnabled() const { return bStaminaEnabled; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsSprinting() const { return Sim.bSprinting; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsDashing() const { return Sim.bDashing; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsGripping() const { return Sim.bGripping; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsWallRunning() const { return Sim.bWallRunning; }

	/** #355: climbing straight up a wall. Remote players read the replicated phase. */
	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsWallClimbing() const { return GetWallClimbPhase() == EGFWallClimbPhase::Climbing; }

	/** #355: the climb-exit mantle onto a ledge. Remote players read the replicated phase. */
	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsMantling() const { return GetWallClimbPhase() == EGFWallClimbPhase::Mantling; }

	/** #355: None, Climbing or Mantling. The owner and the server read the sim; simulated proxies read the replicated copy. */
	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	EGFWallClimbPhase GetWallClimbPhase() const;

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsSwiveling() const { return Sim.bSwiveling; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsSwinging() const { return Sim.bSwinging; }

	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	bool IsYanking() const { return Sim.bYanking; }

	/** Ballistic ground point. False when the marker should stay hidden or the trace misses. */
	bool GetLandingMarker(FVector& OutPoint) const;

	/**
	 * v2 (#342): the landing target, worked out from this frame's position, velocity, steering and gravity, so it
	 * tracks the whole flight. Names the hop tile it lands on and whether that tile is the road's lit target.
	 */
	FGFLandingTarget GetLandingTarget() const;

	/**
	 * v3 (#347): the aim steering this pawn flies with when the camera looks along Aim. Off unless it is a player's
	 * pawn this machine moves (owner or server), the tuning has it on, no swing, dash or override is driving the
	 * velocity, and (bAirSteerOnlyOnRoad) the pawn is inside a hop road's box. The move and the landing probe share it.
	 */
	FGFAirSteer BuildAirSteer(const FVector& Aim) const;

	/** v3 (#347): Location is inside some hop road's box (its top face raised by HeadroomUpCm). */
	bool IsOnAccelHopRoad(const FVector& Location, double HeadroomUpCm = 0.0) const;

	/** #347: Location is inside the air steer scope: a hop road's box plus AccelHop.AirSteerRoadHeadroomCm on top. */
	bool IsInAirSteerRoadScope(const FVector& Location) const;

	/** #347: the current (or last) flight took off inside the air steer scope. Set by every grounded move. */
	bool DidAirSteerTakeOffOnRoad() const { return bAirSteerTakeoffOnRoad; }

	/** v3 (#347): the steering the last move flew with (tests and the HUD read it). */
	const FGFAirSteer& GetMoveAirSteer() const { return MoveAirSteer; }

	/** v3 (#347): degrees the aim steering turned the flight in the last falling velocity update. */
	float GetLastAirSteerTurnDeg() const { return LastAirSteerTurnDeg; }

	/** Looks for a Beat Hop run that opted into the traversal kit (#277). */
	void RefreshBeatHop();

	/**
	 * Drops presses the running difficulty does not allow.
	 * Dash gates stay a dash (ground and air) on every difficulty that includes them.
	 * No-op outside a run, so the hub kit is unchanged.
	 */
	void ApplyBeatHopMoveMask(FGFStaminaCommand& Command) const;

	bool IsBeatHopRun() const { return bBeatHopRun; }

	/** Extra spring-arm length while a Beat Hop wall run or grapple is playing. Zero otherwise. */
	float GetBeatHopCameraPullback() const;

	const UGFMovementTuning& GetTuning() const;

	/** Acceleration Hop (#334): run speed multiplier from hop tiles right now (1 = no boost). */
	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	float GetAccelHopMultiplier() const;

	/** In-time hop tiles in a row (for the HUD). */
	UFUNCTION(BlueprintPure, Category = "GF|Movement")
	int32 GetAccelHopChain() const { return AccelHop.Chain; }

	const FGFAccelHopHit& GetLastAccelHopHit() const { return LastAccelHopHit; }
	/** The last applied hop landed on the road's lit target (#342). */
	bool WasLastAccelHopLit() const { return bLastAccelHopLit; }
	const FGFAccelHopBoost& GetAccelHopBoost() const { return AccelHop; }

	/**
	 * The owning machine landed on a hop tile: judge it on the local beat clock (NowBeat, Bpm), apply the boost, and
	 * tell the server. Returns true when the boost changed. TickComponent calls it; tests call it directly.
	 */
	bool HandleAccelHopLanding(AGFAccelHopTile* Tile, double NowBeat, float Bpm, EGFBeatHopMode Mode);

	/**
	 * #348: fires on the owning machine after every judged hop tile landing (HandleAccelHopLanding), for every
	 * character. Listeners filter by world and pawn. Never fires for a server's copy of a remote player's hop.
	 */
	static FGFOnAccelHopLanded OnAccelHopLanded;

	/**
	 * Fires on the server after every judged landing it handled: the host's own (HandleAccelHopLanding with authority)
	 * and every remote player's report (ApplyServerAccelHop past the tile and rate checks). Side quest credit listens.
	 */
	static FGFOnServerAccelHop OnServerAccelHop;

	/** Fires on the server when a mantle ends in an authoritative move (arrival or the mantle timer). */
	static FGFOnServerMantleFinished OnServerMantleFinished;

	/** Server side of a reported hop: checks the tile is under the pawn and the rate limit, then applies it. */
	bool ApplyServerAccelHop(AGFAccelHopTile* Tile, const FGFAccelHopHit& Hit, float BeatSeconds);

	void RefreshKindleOverlaps();

	/** Saved-move replay. Not for gameplay code. */
	void SetDashFromReplay(bool bDash) { bDashThisMove = bDash; }
	void RestoreSim(const FGFStaminaState& InSim) { Sim = InSim; }
	void SetReplayCamera(const FVector& Forward);
	void SetReplayTraversal(bool bJump, bool bYank, bool bSwing, const FGFTraversalContact& Contact);
	bool WantsSprint() const { return bWantsToSprint; }
	bool WantsGrappleSwing() const { return bWantsGrappleSwing; }
	bool WasDashRecorded() const { return bDashRecorded; }
	bool WasYankRecorded() const { return bYankRecorded; }
	bool WasJumpUsed() const { return bJumpUsedThisMove; }
	void ClearDashRecorded() { bDashRecorded = false; }
	void ClearYankRecorded() { bYankRecorded = false; }
	const FGFStaminaState& GetSimAtMoveStart() const { return SimAtMoveStart; }
	const FVector& GetLastCameraForward() const { return LastCameraForward; }
	const FGFTraversalContact& GetContactAtMoveStart() const { return ContactAtMoveStart; }

protected:
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta) override;

	UFUNCTION()
	void OnRep_AuthoritativeStamina();

	UFUNCTION()
	void OnRep_StaminaGate();

	UFUNCTION()
	void OnRep_FlipTime();

	/** A client landed on a hop tile. The server cannot hear the client's music, so it takes the judgement and checks the rest. */
	UFUNCTION(Server, Reliable)
	void ServerAccelHop(AGFAccelHopTile* Tile, const FGFAccelHopHit& Hit, float BeatSeconds);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GF|Movement")
	TObjectPtr<UGFMovementTuning> TuningAsset;

	UPROPERTY(ReplicatedUsing = OnRep_AuthoritativeStamina)
	float AuthoritativeStamina = 200.f;

	/**
	 * 0 until the server has said. 1 on, 2 off.
	 * A bool would not replicate when the server value matches the default, so an opted-in mini-game
	 * (server on, client map-guess off) would stay off. Zero always differs from the server's choice.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_StaminaGate)
	uint8 ReplicatedStaminaGate = 0;

	UPROPERTY(Replicated)
	bool bInsideKindleRing = false;

	/** Jump-3 flip seconds left. Simulated proxies play this. The owner predicts Sim.FlipTimeLeft instead. */
	UPROPERTY(ReplicatedUsing = OnRep_FlipTime)
	float ReplicatedFlipTimeLeft = 0.f;

	/** #355: EGFWallClimbPhase for simulated proxies. The owner predicts Sim instead. */
	UPROPERTY(Replicated)
	uint8 ReplicatedWallClimbPhase = 0;

private:
	void RefreshStaminaGate();
	void ApplyStaminaGate();
	/** Possess never ran, so the mode is still None. None does not walk. Hub speed stays on. */
	void PrepareControllerlessWalk();
	FVector LiveCameraForward() const;
	void GatherLiveContact(FGFTraversalContact& Contact);
	/** #355: wall straight ahead of the input (or the climbed wall), and a walkable lip above it with room to stand. */
	void GatherClimbContact(FGFTraversalContact& Contact) const;
	/** #355: a climb or mantle owns this move. Leaves the floor and hands the sim's velocity (Z included) to PhysFalling. */
	void ApplyWallClimbMove();
	void ApplyFlipPose(float TimeLeft);
	void PollAccelHopLanding();
	/** The #341 fallback behind StepUp. Same up, forward, down shape as the engine step, sized to the tread it found. */
	/** OutWhy names a stair-like refusal for the #341 log (null when the assist had no view); OutRise is the riser it measured. */
	bool TryRiserAssist(const FVector& GravDir, const FVector& Delta, const FHitResult& Hit, FStepDownResult* OutStepDownResult,
		const TCHAR*& OutWhy, float& OutRise);
	/** Counts and maybe logs one refused walking hit. Called from HandleImpact, which every refused walking hit reaches. */
	void NoteStairRefusal(const FHitResult& Hit);
	/** Top of the hit component just past the face, cm above the feet. False when the trace found no top. */
	bool MeasureFaceTop(const FHitResult& Hit, float& OutTopCm, bool& bOutWalkable) const;
	double AccelHopNow() const;

	FGFStaminaState Sim;
	FGFStaminaState SimAtMoveStart;
	FGFTraversalContact ContactAtMoveStart;
	FGFTraversalContact ReplayContact;
	FVector LastCameraForward = FVector::ForwardVector;
	FVector ReplayCameraForward = FVector::ForwardVector;
	bool bUseReplayCamera = false;
	bool bUseReplayTraversal = false;
	bool bWantsToSprint = false;
	bool bWantsGrappleSwing = false;
	bool bDashThisMove = false;
	bool bDashRecorded = false;
	/** True only while PerformMovement runs the hop its stamina step accepted. The engine's own early jump check is refused. */
	bool bJumpFromStep = false;
	bool bYankThisMove = false;
	bool bYankRecorded = false;
	bool bJumpUsedThisMove = false;
	bool bReplayJump = false;
	bool bReplayYank = false;
	bool bReplaySwing = false;
	bool bPendingDashHit = false;
	float StepDeltaSeconds = 0.f;
	FVector LocationAtStepStart = FVector::ZeroVector;
	float PendingSpeedBefore = 0.f;
	float PendingSpeedAfter = 0.f;
	float DashPlanarSpeed = 0.f;
	FVector PendingImpactNormal = FVector::UpVector;
	bool bLocalInsideKindle = false;
	bool bStaminaEnabled = true;
	bool bBeatHopRun = false;
	FGFBeatHopTraversalProfile BeatHopProfile;
	float BeatHopRefSpeed = 0.f;
	EGFBeatDifficulty BeatHopDifficulty = EGFBeatDifficulty::Easy;
	bool bGotAuthoritativeStamina = false;
	float ProxyFlipTimeLeft = 0.f;
	FRotator MeshRestRotation = FRotator::ZeroRotator;
	bool bMeshRestCaptured = false;
	FGFAccelHopBoost AccelHop;
	FGFAccelHopHit LastAccelHopHit;
	int32 RiserAssistCount = 0;
	double RiserAssistLoggedAt = -1000.0;
	int32 StairRefusalCount = 0;
	/** When each hit component last wrote a StairRefused line, so one wall does not hide the next. */
	TMap<TWeakObjectPtr<UPrimitiveComponent>, double> StairRefusalLoggedAt;
	/** What the last refused StepUp learned, read by the HandleImpact that follows it for the same component. */
	TWeakObjectPtr<UPrimitiveComponent> PendingRefusalComponent;
	const TCHAR* PendingRefusalWhy = nullptr;
	float PendingRefusalRise = -1.f;
	/** True while StepUp runs. The engine step calls HandleImpact on its forward leg, which is not a refusal yet. */
	bool bInsideStepUp = false;
	FGFWalkRefusal LastWalkRefusal;
	bool bAccelHopWasFalling = false;
	bool bLastAccelHopLit = false;
	/** #347: built once per move in PerformMovement (from the move's camera, so a replay steers the same way). */
	FGFAirSteer MoveAirSteer;
	float LastAirSteerTurnDeg = 0.f;
	/** #347: one log line per flight when aim steering drops because the pawn left the road scope. */
	bool bAirSteerScopeLossLogged = false;
	/**
	 * #347: recomputed by every move that starts on the ground, read by the moves (and the landing probe) in the air.
	 * A correction replay recomputes it from the replayed grounded move, so owner and server agree.
	 */
	bool bAirSteerTakeoffOnRoad = false;
};
