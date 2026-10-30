#pragma once

#include "CoreMinimal.h"
#include "GFFashionTypes.h"
#include "GFRunwayJudge.h"
#include "GFEconomy.h"
#include "GFRunwayShow.generated.h"

UENUM(BlueprintType)
enum class EGFRunwayPhase : uint8
{
	Idle,
	/** Everyone picks an outfit (75 s, or until all are ready). */
	Dressing,
	/** One contestant at a time walks the runway (8 s each). */
	Walking,
	/** Cards go up. */
	Judging,
	/** Scores and rewards are final. */
	Results
};

USTRUCT(BlueprintType)
struct GFFASHION_API FGFRunwayContestant
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") FString PlayerKey;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") FGFOutfit Outfit;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") bool bReady = false;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") FGFRunwayScore Score;
	/** 1 = best look. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") int32 Rank = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Fashion") TArray<FGFItemStack> Rewards;
};

/**
 * One fashion show, as a pure state machine (no world needed, so tests drive it with Advance()).
 * Idle -> Dressing -> Walking -> Judging -> Results. Rewards are minted once per contestant with a TxId derived from
 * the show id and player key, so granting twice (a retry, a reconnect) never pays twice.
 */
struct GFFASHION_API FGFRunwayShow
{
	float DressingSeconds = 75.f;
	float WalkSeconds = 8.f;
	float JudgingSeconds = 4.f;
	FGFRunwayRewardRules Rewards;

	/** Starts dressing. False if the theme is unknown, nobody joined, or a show is already on. */
	bool Start(const FGuid& ShowId, FName ThemeId, int32 Seed, const TArray<FString>& PlayerKeys);
	/** Changes a contestant's outfit (dressing only). Refuses outfits that fail FGFOutfit::Validate. */
	bool SetOutfit(const FString& PlayerKey, const FGFOutfit& Outfit);
	bool SetReady(const FString& PlayerKey, bool bReady = true);
	/** Earlier looks per player for the novelty score. */
	void SetHistory(const FString& PlayerKey, const TArray<FGFOutfit>& Past) { History.Add(PlayerKey, Past); }
	void Advance(float DeltaSeconds);
	void Reset();

	/** Mints each contestant's rewards into the ledger. Idempotent per show. Returns how many were newly applied. */
	int32 GrantRewards(IGFLedgerAccounts& Accounts, FGFTransferLedger& Ledger) const;
	/** Deterministic transaction id for a contestant's show reward. */
	FGuid RewardTxId(const FString& PlayerKey) const { return MakeTxId(ShowId, PlayerKey, TEXT("Reward")); }
	/**
	 * Deterministic transaction id for dyeing one slot one colour in this show: show + player + slot + dye item +
	 * colour (8-bit RGB). Resending the same colour is free; re-dyeing the slot a new colour is a new charge.
	 */
	FGuid DyeTxId(const FString& PlayerKey, EGFOutfitSlot Slot, const FLinearColor& Color) const;
	/** MD5 of "GF.Fashion|show|player|what" as a guid. */
	static FGuid MakeTxId(const FGuid& ShowId, const FString& PlayerKey, const FString& What);

	EGFRunwayPhase GetPhase() const { return Phase; }
	float GetPhaseTimeLeft() const { return PhaseTimeLeft; }
	/** Contestant on the runway while Walking, else INDEX_NONE. */
	int32 GetWalkerIndex() const { return Phase == EGFRunwayPhase::Walking ? Walker : INDEX_NONE; }
	const TArray<FGFRunwayContestant>& GetContestants() const { return Contestants; }
	const FGFRunwayContestant* FindContestant(const FString& PlayerKey) const;
	FName GetThemeId() const { return ThemeId; }
	const FGuid& GetShowId() const { return ShowId; }

private:
	void EnterWalking();
	void JudgeAll();

	EGFRunwayPhase Phase = EGFRunwayPhase::Idle;
	float PhaseTimeLeft = 0.f;
	int32 Walker = 0;
	FGuid ShowId;
	FName ThemeId;
	int32 Seed = 0;
	TArray<FGFRunwayContestant> Contestants;
	TMap<FString, TArray<FGFOutfit>> History;
};
