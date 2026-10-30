#pragma once

// The giant dino event logic, as a component (lives on AGFGiantDirector). It drives a reusable AGFGiantDino body
// (spawn, MoveBody, OnAttackedBy) but owns none of its code, so another driver (e.g. a tamed companion) can reuse
// the body later.
// Server side of the giant dino event (Big Grumble; name and look in Data/CSV/DT_GiantDino.csv).
// - Trigger: every MinRealSecondsBetween of real time, once players have played MinPlaySecondsSinceLast in total
//   since the last event, and only while someone is exposed (FGFGiantRules::IsExposed: unprotected where they stand,
//   including a player in a no-entry box). Clock and play time are saved with
//   the world (mode blob "Giant").
// - While it runs, FGFGiantSim decides everything; this actor applies it: moves the replicated AGFGiantDino, shoves
//   pawns (LaunchCharacter, never damage), posts lines to the replicated Chat log, and handles a snack (belly cam
//   for BellyCamSeconds, then IGFFaintHandler). Nothing here touches the ledger.
// - A handoff (protection just turned off, or a grab still holding) is preferred, and he thanks that player once.
//   A taunt emote (FGFAttention) pulls him toward that spot while he is already out. He still never takes a
//   protected player or enters a no-entry box. A jailed player can be the target; he stops at the box edge.
// - Safety is only ever asked through IGFSafeZoneQuery (GetSafeZone; also published to UGFSafeZoneSubsystem): by default one circle region at
//   DefaultSafeCenter growing with the player count; the territory system replaces it with SetSafeRegions (any number
//   of circle regions, each growing with its own member count) or SetSafeZone (any implementation).
// Verbs (AGFPlayerState::RequestMode, Mode "Giant"):
//   Hit    the sender attacks the giant (must be within AttackRange) -> it eats them and leaves. Ints = [1 if eaten]
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFModeTypes.h"
#include "GFGiantTypes.h"
#include "GFFaintHandler.h"
#include "GFGiantEventComponent.generated.h"

class AGFGameState;
class AGFPlayerState;
class AGFGiantDino;
class AGFGiantBellyCam;
struct FGFGiantTaunt;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnGiantChat, const FGFGiantChatLine&, Line);

UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFGIANT_API UGFGiantEventComponent : public UActorComponent, public IGFModeHandler, public IGFSafeZoneProvider
{
	GENERATED_BODY()

public:
	UGFGiantEventComponent();

	static const FName ModeName;

	virtual FName GetModeName() const override { return ModeName; }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual void OnModePlayerLeft(AGFPlayerState* PS) override;
	virtual void CaptureModeState(TArray<uint8>& Out) const override;
	virtual void RestoreModeState(const TArray<uint8>& In) override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Optional tunables asset; without one the FGFGiantTuning defaults are used. Copied into Tuning at BeginPlay. */
	UPROPERTY(EditAnywhere, Category = "GF|Giant")
	TSoftObjectPtr<UGFGiantTunables> TunablesAsset;

	/** Server: live tuning (tests edit this directly). */
	FGFGiantTuning Tuning;

	/** Safe regions used by the built-in circle implementation (replicated so clients can draw/query them). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Giant")
	TArray<FGFSafeRegion> SafeRegions;

	/** Server: centre of the default region (the hub plot is centred on the origin). */
	FVector DefaultSafeCenter = FVector::ZeroVector;

	/** Server: replace the default region with these (e.g. one per group's territory; Members drives each radius). */
	void SetSafeRegions(const TArray<FGFSafeRegion>& Regions);
	/** Server: use any safe-zone implementation (null = back to the circle regions). */
	void SetSafeZone(TSharedPtr<IGFSafeZoneQuery> Zone);
	/** The safe-zone query everything here uses (custom one if set, else the circle regions). */
	const IGFSafeZoneQuery& GetSafeZone() const;
	/**
	 * What the sim decides with: GetSafeZone's shapes, with per-player safety and owners asked of the land owner
	 * through UGFSafeZoneSubsystem first (FGFGiantLandZone), so faction protection counts.
	 */
	const IGFSafeZoneQuery& GetLandZone() const;
	/** The world's no-entry boxes (UGFSafeZoneSubsystem; e.g. jail cells). */
	TArray<FBox> GetNoEntry() const;
	/** IGFSafeZoneProvider (UGFSafeZoneSubsystem): the same zone, for other systems (null while there are no regions). */
	virtual const IGFSafeZoneQuery* GetSafeZoneQuery() const override;
	bool IsUsingDefaultRegion() const { return bUseDefaultRegion; }

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Giant")
	bool bEventActive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Giant")
	TObjectPtr<AGFGiantDino> Giant;

	/** All-chat lines from the giant, newest last (last MaxChat). Clients get OnGiantChat for each new one. */
	UPROPERTY(ReplicatedUsing = OnRep_Chat, BlueprintReadOnly, Category = "GF|Giant")
	TArray<FGFGiantChatLine> Chat;
	static constexpr int32 MaxChat = 32;

	UPROPERTY(BlueprintAssignable, Category = "GF|Giant")
	FGFOnGiantChat OnGiantChat;

	/** Server: open-world faint hook (defaults to UGFFaintStub). */
	UPROPERTY()
	TScriptInterface<IGFFaintHandler> FaintHandler;
	UPROPERTY()
	TObjectPtr<UGFFaintStub> DefaultFaint;

	/** Server: players in a menu or cutscene (never eaten for standing still). TODO: fed by the menu/cutscene systems once they exist. */
	TSet<FString> BusyKeys;

	/** Server: automatic trigger on/off (tests start events by hand). */
	bool bAutoTrigger = true;
	/** Server: seconds added to the real UTC clock (tests). */
	double DebugSecondsOffset = 0.0;
	/** Server: fixed sim seed (0 = random). */
	int32 ForcedSeed = 0;

	/** Server: starts an event now (ignores the trigger clock). False if one is running or nobody is in. */
	bool ServerStartEvent();
	/** Server: a player attacked the giant. True if they got eaten. */
	bool ServerOnAttacked(AGFPlayerState* By);
	/** Server: per-player and accumulated play time, trigger clock (saved). */
	const FGFGiantSave& GetSave() const { return Save; }
	FGFGiantSave& EditSaveForTests() { return Save; }
	const FGFGiantSim& GetSim() const { return Sim; }
	int64 NowUtcTicks() const;
	/** Server: what the sim sees of each player right now. */
	TArray<FGFGiantPlayerView> GatherPlayers() const;

	/** Server stats (tests): shoves applied, players eaten, belly cams spawned. */
	int32 ShovesApplied = 0;
	TArray<FString> EatenKeys;
	int32 BellyCamsSpawned = 0;
	int32 EventsStarted = 0;
	/** Damage events on players whose causer or instigator was the giant during an event. Must stay 0. */
	int32 DamageFromGiant = 0;

	/** The loaded lines and info row (CSV fallback). */
	FGFGiantLines Lines;
	FGFGiantInfoRow Info;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void OnGiantAttacked(AGFGiantDino* Dino, AGFPlayerState* By);
	void OnTaunt(UWorld* World, const FGFGiantTaunt& Taunt);
	bool IsServer() const;
	void NetUpdate();

	UFUNCTION()
	void OnRep_Chat();
	/** Watchdog bound to player pawns during an event (the giant never damages; this proves it in tests). */
	UFUNCTION()
	void OnPlayerPawnDamaged(AActor* DamagedActor, float Damage, const class UDamageType* DamageType, class AController* InstigatedBy, AActor* DamageCauser);
	void WatchPawns();

private:
	struct FBelly
	{
		TWeakObjectPtr<AGFPlayerState> PS;
		TWeakObjectPtr<AGFGiantBellyCam> Cam;
		FVector From = FVector::ZeroVector;
		float Left = 0.f;
	};

	AGFGameState* GS() const;
	void Apply(const FGFGiantStepOut& Out);
	void BeginBelly(AGFPlayerState* PS);
	void FinishBelly(FBelly& B, bool bFaint);
	void EndEvent();
	AGFPlayerState* FindPS(const FString& Key) const;
	void RefreshSafeZone(int32 NumPlayers);

	FGFGiantSave Save;
	FGFGiantSim Sim;
	TArray<FBelly> Bellies;
	float TriggerCheckIn = 0.f;
	int32 LastBroadcastSeq = 0;
	bool bUseDefaultRegion = true;
	TSharedPtr<IGFSafeZoneQuery> ZoneOverride;
	mutable FGFSafeCircles Circles;
	mutable FGFGiantLandZone LandZone;
	FDelegateHandle TauntHandle;
};
