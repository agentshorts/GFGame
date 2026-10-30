#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFDinoTypes.generated.h"

/** The Lab Treat (Overtime lab): every species accepts it, worth 1.5x its mean accepted food. */
namespace GFLabItems
{
	static const FName LabTreat(TEXT("LabTreat"));
	static const FName CalmingMist(TEXT("CalmingMist"));
	static constexpr float LabTreatMultiplier = 1.5f;
}

/** One accepted food and the bond it gives (bond bar is 0..100). */
USTRUCT(BlueprintType)
struct GFCOMPANION_API FGFDietEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet")
	FName Food;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet", meta = (ClampMin = "0"))
	float BondGain = 10.f;
};

/**
 * Row for DT_DinoDiet (PLAN.md M2 diet table). Row name = species id.
 * Accepted foods carry their own bond gain. Favourites are separate (never also accepted) and fill the bond
 * FavouriteMultiplier (3x) faster than the species' average accepted food. Anything else is refused.
 * A wild dino turns hostile at a player inside AggroRadius who isn't holding a food it accepts.
 */
USTRUCT(BlueprintType)
struct GFCOMPANION_API FDinoDietRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet")
	FName Species;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet")
	TArray<FGFDietEntry> Accepted;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet")
	TArray<FName> Favourites;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet", meta = (ClampMin = "1"))
	float FavouriteMultiplier = 3.f;

	/** cm. Hostile radius; WarnRadius (sniff / alert) is 1.25x this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet", meta = (ClampMin = "0"))
	float AggroRadius = 1200.f;

	/** Offers per visit before it is full (a visit ends when everyone leaves). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet", meta = (ClampMin = "1"))
	int32 Fullness = 8;

	/** Seconds between offers (it has to eat). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet", meta = (ClampMin = "0"))
	float OfferCooldown = 2.5f;

	/** Seconds of warning (Threat) before it turns Hostile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet", meta = (ClampMin = "0"))
	float ThreatSeconds = 2.f;

	/** Bond lost per second while nobody is near. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet", meta = (ClampMin = "0"))
	float BondDecayPerSec = 1.f;

	/** Pack species (Nipper): one turning hostile turns the pack hostile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diet")
	bool bPack = false;

	bool IsFavourite(FName Food) const { return !Food.IsNone() && Favourites.Contains(Food); }
	/** Bond gain of an accepted (non-favourite) food, or < 0 if not accepted. */
	float AcceptedGain(FName Food) const;
	bool Accepts(FName Food) const { return IsFavourite(Food) || AcceptedGain(Food) >= 0.f; }
	float MeanAcceptedGain() const;
	/** Gain of any favourite: FavouriteMultiplier x mean accepted gain, rounded. */
	float FavouriteGain() const { return FMath::RoundToFloat(MeanAcceptedGain() * FavouriteMultiplier); }
	float WarnRadius() const { return AggroRadius * 1.25f; }
	/** Leaving this radius ends a visit / calms it. */
	float CalmRadius() const { return WarnRadius() * 1.5f; }
};

/** What one player has learned about one species (Field Journal page: sighted, then check / star / cross). */
USTRUCT(BlueprintType)
struct GFCOMPANION_API FGFDinoPreferenceRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	FName Species;

	/** Silhouette becomes a sketch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	bool bSighted = false;

	/** Check marks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	TArray<FName> KnownAccepted;

	/** Stars. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	TArray<FName> KnownFavorites;

	/** Crosses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	TArray<FName> KnownRejected;

	/** Half-reveals: it sniffed this food with interest (accepted or favourite, not yet known which). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	TArray<FName> Interested;

	/** Clues from NPCs / notes ("Raptors go wild for jerky"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	TArray<FName> Clues;

	/** Bandana cosmetic earned for a complete diet page. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	bool bComplete = false;
};

/** The whole Field Journal (per player, saved in the character, replicated owner-only). */
USTRUCT(BlueprintType)
struct GFCOMPANION_API FGFDiscoveredDinoPreferences
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Diet")
	TArray<FGFDinoPreferenceRecord> Records;

	FGFDinoPreferenceRecord& FindOrAdd(FName Species);
	const FGFDinoPreferenceRecord* Find(FName Species) const;
	/** Total known facts (sightings + marks + clues); handy for tests and the UI counter. */
	int32 CountFacts() const;
};

UENUM(BlueprintType)
enum class EGFFeedResult : uint8
{
	Rejected,
	Accepted,
	Favorite
};

/** What happened to an offer. Only Accepted / Favourite consume the food. */
UENUM(BlueprintType)
enum class EGFOfferResult : uint8
{
	Accepted,
	Favourite,
	Refused,
	/** Still eating (OfferCooldown). */
	Cooldown,
	/** Full for this visit. */
	Full,
	/** Hostile and not a favourite, or out of reach. */
	NotNow,
	/** Already tamed, fled, or no diet row. */
	NotTameable
};

/** Taming flow (PLAN.md M2): Wild -> Alert -> Curious or Threat -> Hostile; Feeding -> Tamed; Fled resets. */
UENUM(BlueprintType)
enum class EGFTameState : uint8
{
	Wild,
	/** Sniffing the nearest player (half-reveals the held food). */
	Alert,
	/** Someone nearby holds a food it accepts. */
	Curious,
	/** Warning: no acceptable food in hand. Hostile after ThreatSeconds. */
	Threat,
	Hostile,
	/** At least one offer accepted this visit. */
	Feeding,
	Tamed,
	/** Ran off; bond reset. Wanders back (Wild) after a while. */
	Fled
};

/** Companion behaviour states (StateTree later; C++ switch for now). */
UENUM(BlueprintType)
enum class EGFDinoState : uint8
{
	Idle,
	Follow,
	Attack,
	Rest
};

/** What a dino perceives about one player this tick. */
USTRUCT(BlueprintType)
struct GFCOMPANION_API FGFTamerView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Taming") FString Key;
	UPROPERTY(BlueprintReadOnly, Category = "Taming") float Distance = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Taming") FName Held;
	/** Crouching shrinks the warn and aggro radii to 70%. */
	UPROPERTY(BlueprintReadOnly, Category = "Taming") bool bCrouching = false;

	FGFTamerView() = default;
	FGFTamerView(const FString& InKey, float InDistance, FName InHeld, bool bInCrouching = false)
		: Key(InKey), Distance(InDistance), Held(InHeld), bCrouching(bInCrouching) {}
};

/**
 * Pure taming state machine (no world): bond, starter / helpers, refusals, aggro, fullness and cooldown.
 * Driven by UGFTamingComponent on the server; tests drive it directly.
 */
USTRUCT(BlueprintType)
struct GFCOMPANION_API FGFTameMachine
{
	GENERATED_BODY()

	static constexpr float MaxBond = 100.f;
	static constexpr float HelperShare = 0.5f;
	static constexpr float CrouchScale = 0.7f;
	static constexpr float CalmWithFoodSeconds = 3.f;
	static constexpr float CalmAloneSeconds = 5.f;
	static constexpr float FleeAfterHostileSeconds = 20.f;
	static constexpr float FledRecoverSeconds = 10.f;

	UPROPERTY() EGFTameState State = EGFTameState::Wild;
	/** The starter's bond (0..100). Tamed at 100. */
	UPROPERTY() float Bond = 0.f;
	/** First player to feed it; the tame joins their roster. */
	UPROPERTY() FString StarterKey;
	UPROPERTY() FString OwnerKey;
	/** Who it is angry at (Threat / Hostile). */
	UPROPERTY() FString TargetKey;
	UPROPERTY() TMap<FString, float> Contributions;
	UPROPERTY() int32 Aggravation = 0;
	UPROPERTY() int32 OffersThisVisit = 0;
	UPROPERTY() float StateTime = 0.f;
	UPROPERTY() float CalmTime = 0.f;
	UPROPERTY() float LastOfferTime = -1000.f;
	/** Calming Mist: until this time (server seconds) nobody counts as a threat and refusals don't anger it. */
	UPROPERTY() float CalmUntil = -1000.f;

	static constexpr float CalmingMistSeconds = 10.f;

	/** Advances timers and reacts to who is around. Returns true if State changed. */
	bool Tick(const FDinoDietRow& Row, float Now, float Dt, const TArray<FGFTamerView>& Players);
	/** Key offers Food. Caller removes one Food from the player only for Accepted / Favourite. */
	EGFOfferResult Offer(const FDinoDietRow& Row, float Now, const FString& Key, FName Food);
	/** It was hit: resets the calm-down timer (Hostile stays Hostile). */
	void NotifyHit() { CalmTime = 0.f; }
	/** Turn hostile at Key (pack call-in). */
	void ForceHostile(const FString& Key);
	/** Calming Mist: a Threat or Hostile dino settles to Alert and stays calm for Seconds. False if tamed or fled. */
	bool ApplyCalm(float Now, float Seconds = CalmingMistSeconds);
	bool IsCalmed(float Now) const { return Now < CalmUntil; }
	void Flee();
	bool IsTamed() const { return State == EGFTameState::Tamed; }

	static bool IsWildState(EGFTameState S) { return S != EGFTameState::Tamed; }

private:
	void SetState(EGFTameState NewState);
	void EndVisit();
};

/** Pure diet helpers (no world). */
struct GFCOMPANION_API FGFDinoDiet
{
	static EGFFeedResult Classify(const FDinoDietRow& Row, FName Food);
	/** Bond for one offer by the starter (0 when refused). */
	static float BondGain(const FDinoDietRow& Row, FName Food);
	/** Nearest player who is not holding an acceptable food, inside the aggro radius (crouch-scaled). -1 = none. */
	static int32 PickAggroTarget(const FDinoDietRow& Row, const TArray<FGFTamerView>& Players, float RadiusScale = 1.f);
	/** Records a feeding result. Returns true if the journal learned something new. */
	static bool RecordDiscovery(FGFDiscoveredDinoPreferences& Book, FName Species, FName Food, EGFFeedResult Result);
	static bool RecordSighting(FGFDiscoveredDinoPreferences& Book, FName Species);
	/** Sniff half-reveal: it showed interest in Food. No-op if the food is already marked. */
	static bool RecordInterest(FGFDiscoveredDinoPreferences& Book, FName Species, FName Food);
	static bool RecordClue(FGFDiscoveredDinoPreferences& Book, FName Species, FName ClueId);
	/** Merges everything From knows into To (journal sharing). Returns the number of new facts. */
	static int32 MergeInto(FGFDiscoveredDinoPreferences& To, const FGFDinoPreferenceRecord& From);
	/** Every accepted food and favourite is marked: sets bComplete (bandana). Returns true when it just completed. */
	static bool UpdateCompletion(FGFDiscoveredDinoPreferences& Book, const FDinoDietRow& Row);
	static EGFFeedResult ToFeedResult(EGFOfferResult R)
	{
		return R == EGFOfferResult::Favourite ? EGFFeedResult::Favorite : R == EGFOfferResult::Accepted ? EGFFeedResult::Accepted : EGFFeedResult::Rejected;
	}
};
