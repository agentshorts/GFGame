#pragma once

// Server jail state for the session. A replicated component on AGFGameState (a world subsystem cannot replicate).
// Holds the jailed set keyed by AGFPlayerState::PlayerKey, checks FGFJailRules, and is the only writer of the
// per-player bJailed flag. Queries only: no voice, no chat send, no teleport.
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Social/GFJailRules.h"

class AGFGameState;
class AGFPlayerState;

#include "GFJailSubsystem.generated.h"

/** One replicated jail bit. Presence with bJailed false means "released", so clients see the flag flip. */
USTRUCT()
struct GFGAME_API FGFJailPlayerFlag
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerId;

	UPROPERTY()
	bool bJailed = false;
};

UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFGAME_API UGFJailSubsystem : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFJailSubsystem();

	/** The jail component on the world's game state, if the game mode (or a test) has created it. */
	static UGFJailSubsystem* Get(const UObject* WorldContext);
	/** Find or create the component on GS. Safe to call more than once. */
	static UGFJailSubsystem* Ensure(AGFGameState* GS);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Server: jail Target if FGFJailRules::CanJail allows it. Without authority returns NotAuthority and
	 * does not change the flag. No timer.
	 */
	FName Jail(AGFPlayerState* Actor, AGFPlayerState* Target);
	/** Server: release Target if FGFJailRules::CanRelease allows it. Same authority rule as Jail. */
	FName Release(AGFPlayerState* Actor, AGFPlayerState* Target);

	/**
	 * Server: remember the first time Player claimed FactionId (claim order for leader handover).
	 * Bound to AGFGameState::OnFactionChanged. A later claim of the same faction does not move the order.
	 */
	void NoteFactionClaim(AGFPlayerState* Player, FName FactionId);
	/**
	 * Server: a login or a rejoin. Does not clear jail. If PlayerKey is already jailed, the new player
	 * state's replicated flag is set again.
	 */
	void NotePlayerJoined(AGFPlayerState* Player);

	bool IsJailed(const FString& PlayerId) const;
	/** Voice and text. True exactly when the player is jailed. */
	bool IsMuted(const FString& PlayerId) const;
	/** False exactly when the player is jailed. */
	bool CanEmote(const FString& PlayerId) const;

	/**
	 * Jail leadership for FactionId (may differ from AGFGameState::FactionLeaderOf once the first claimer
	 * has left or is skipped for being jailed). Empty if nobody remains.
	 */
	FString LeaderOf(FName FactionId);

	/** Replicated per-player flags, including ids whose player state has already left. */
	UPROPERTY(ReplicatedUsing = OnRep_JailedFlags, BlueprintReadOnly, Category = "GF|Jail")
	TArray<FGFJailPlayerFlag> JailedFlags;

	UFUNCTION()
	void OnRep_JailedFlags();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool HasJailAuthority() const;
	void SyncRoster();
	FGFJailContext MakeContext() const;
	FGFJailActor Describe(const AGFPlayerState* PS) const;
	int32 JoinOrderOf(const FString& PlayerId, FName FactionId) const;
	void WriteFlag(const FString& PlayerId, bool bJailed);

	/** "PlayerKey|Faction" -> first claim order this session. Not cleared on logout. */
	TMap<FString, int32> ClaimOrder;
	int32 NextClaimOrder = 1;
	FDelegateHandle FactionHandle;
};
