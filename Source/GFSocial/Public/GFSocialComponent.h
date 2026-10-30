#pragma once

// Server side of the social features (lives on AGFSocialDirector; mode "Social"):
// - Safe-zone sandbox powers: Grow / Shrink (0.5x..2x) and Grab (tether and pull). Only inside safe territory (asked
//   through UGFSafeZoneSubsystem / IGFSafeZoneQuery), only on a player of a DIFFERENT faction, with cooldowns. Leaving
//   safety (caster or target) reverts the scale and breaks the tether.
// - Jail: chat muted (AGFGameState chat gate), voice muted (stub), teleported to the underground cell, drip loop on
//   the jailed player's client (stub cue). Release teleports them back. Kept for the whole session (also on rejoin).
// - Kick / ban (FGFSocialRules::CanKickOrBan, host only today). Bans are saved with the host's world by stable player
//   key and refused at join (IGFModeHandler::RefuseJoin, AGFGameMode PreLogin / PostLogin). Unban by key.
// - Emotes (Data/CSV/DT_Emotes.csv): anywhere, replicated, short cooldown. Outside safety, in a mode whose
//   DT_ModeRules row has bEmoteAttractsDanger, the emote raises a noise (FGFNoise) that draws nearby wild dinos.
// Verbs (AGFPlayerState::RequestMode, Mode "Social"; target players by Ints[0] = PlayerId):
//   Grow, Shrink, Grab      powers on the target
//   Drop                    let go of whoever the sender is pulling
//   Jail, Unjail            jail / release the target
//   Kick, Ban               the target
//   Unban                   Names[0] = the banned player's key
//   Emote                   Names[0] = DT_Emotes row
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFModeTypes.h"
#include "GFSocialTypes.h"
#include "GFSocialComponent.generated.h"

class AGFGameState;
class AGFPlayerState;
class AGFJailCell;
class USoundBase;
class UAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnEmote, const FGFEmoteEvent&, Emote);

/**
 * Voice-chat mute hook. There is no voice chat yet: this stub records the calls; the voice system replaces
 * UGFSocialComponent::Voice with its own implementation later.
 */
UCLASS()
class GFSOCIAL_API UGFVoiceMuteStub : public UObject
{
	GENERATED_BODY()

public:
	virtual void SetVoiceMuted(const FString& Key, bool bMuted);
	bool IsVoiceMuted(const FString& Key) const { return Muted.Contains(Key); }

	TSet<FString> Muted;
	int32 Calls = 0;
};

UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFSOCIAL_API UGFSocialComponent : public UActorComponent, public IGFModeHandler
{
	GENERATED_BODY()

public:
	UGFSocialComponent();

	static const FName ModeName;
	/** Mode a player is in until a mode tells us otherwise. */
	static const FName DefaultPlayerMode;

	// IGFModeHandler
	virtual FName GetModeName() const override { return ModeName; }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual void OnModePlayerReady(AGFPlayerState* PS) override;
	virtual void OnModePlayerLeft(AGFPlayerState* PS) override;
	virtual void CaptureModeState(TArray<uint8>& Out) const override;
	virtual void RestoreModeState(const TArray<uint8>& In) override;
	virtual FName ChatBlockReason(const AGFPlayerState* By) const override;
	virtual bool RefuseJoin(const FString& PlayerKey, FString& OutWhy) const override;

	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: live tuning (tests edit this). */
	UPROPERTY(EditAnywhere, Category = "GF|Social")
	FGFSocialTuning Tuning;

	// ---------------- Replicated state ----------------
	UPROPERTY(ReplicatedUsing = OnRep_Jailed, BlueprintReadOnly, Category = "GF|Social")
	TArray<FGFJailEntry> Jailed;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Social")
	TArray<FGFBanEntry> Bans;
	/** Players not at 1x (actor scale doesn't replicate on its own). */
	UPROPERTY(ReplicatedUsing = OnRep_Scales, BlueprintReadOnly, Category = "GF|Social")
	TArray<FGFScaleEntry> Scales;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Social")
	TArray<FGFTether> Tethers;
	/** Recent emotes, newest last (last MaxEmotes). Every machine gets OnEmote once per new one. */
	UPROPERTY(ReplicatedUsing = OnRep_Emotes, BlueprintReadOnly, Category = "GF|Social")
	TArray<FGFEmoteEvent> Emotes;
	static constexpr int32 MaxEmotes = 16;

	UPROPERTY(BlueprintAssignable, Category = "GF|Social")
	FGFOnEmote OnEmote;

	// ---------------- Server API (verbs call these; tests too) ----------------
	FName ServerPower(AGFPlayerState* By, AGFPlayerState* Target, EGFSocialPower Power);
	FName ServerDrop(AGFPlayerState* By);
	FName ServerJail(AGFPlayerState* By, AGFPlayerState* Target);
	FName ServerRelease(AGFPlayerState* By, AGFPlayerState* Target);
	FName ServerKick(AGFPlayerState* By, AGFPlayerState* Target);
	FName ServerBan(AGFPlayerState* By, AGFPlayerState* Target);
	FName ServerUnban(AGFPlayerState* By, const FString& Key);
	FName ServerEmote(AGFPlayerState* By, FName EmoteId);
	/** Server: which mode a player is playing (DT_ModeRules key). TODO: set by the mode systems on enter / leave. */
	void ServerSetPlayerMode(const FString& Key, FName Mode);
	FName GetPlayerMode(const FString& Key) const;

	const FGFJailEntry* FindJail(const FString& Key) const;
	bool IsJailed(const FString& Key) const { return FindJail(Key) != nullptr; }
	bool IsBanned(const FString& Key) const;
	float ScaleOf(const FString& Key) const;
	/** Inside safe territory (via UGFSafeZoneSubsystem)? False with no pawn or no safe zone. */
	bool IsPlayerSafe(const AGFPlayerState* PS) const;
	FGFSocialPlayer Describe(const AGFPlayerState* PS) const;

	/** Server: voice mute hook (stub until there's voice chat). */
	UPROPERTY()
	TObjectPtr<UGFVoiceMuteStub> Voice;
	/** Server: the cell actor (spawned on the first jailing). */
	UPROPERTY()
	TObjectPtr<AGFJailCell> Cell;

	// ---------------- Client: drip loop while the local player is jailed ----------------
	/** Optional looping cue; empty = stub (just the flag and a log line). TODO(audio): a soft dripping-water loop. */
	UPROPERTY(EditAnywhere, Category = "GF|Social")
	TSoftObjectPtr<USoundBase> DripLoopCue;
	bool bDripPlaying = false;
	int32 DripStarts = 0;

	/** Stats (tests). Server: noises raised, kicks done. Every machine: emotes seen, bubbles shown. */
	int32 NoisesRaised = 0;
	int32 KicksDone = 0;
	int32 EmotesSeen = 0;
	int32 BubblesShown = 0;
	FGFEmoteEvent LastEmoteSeen;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UFUNCTION()
	void OnRep_Jailed();
	UFUNCTION()
	void OnRep_Scales();
	UFUNCTION()
	void OnRep_Emotes();

private:
	bool IsServer() const;
	AGFGameState* GS() const;
	AGFPlayerState* FindPS(const FString& Key) const;
	void NetUpdate();
	void ApplyScale(const FString& Key);
	void ApplyAllScales();
	void SyncScales();
	void TeleportPawn(AGFPlayerState* PS, const FVector& To);
	FVector CellSpot() const;
	void EnsureCell();
	void ApplyJailTo(AGFPlayerState* PS, bool bJailed);
	void BreakTethersOf(const FString& Key);
	void TickServer(float Dt);
	void UpdateLocalDrip();
	void ShowEmote(const FGFEmoteEvent& E);
	bool DoKick(AGFPlayerState* Target, const FString& Why);

	FGFScaleBook ScaleBook;
	FGFCooldowns Cooldowns;
	TMap<FString, FName> PlayerModes;
	int32 NextEmoteSeq = 1;
	int32 LastEmoteBroadcast = 0;
	float ClientScaleCheckIn = 0.f;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> DripAudio;
};
