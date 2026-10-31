#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFDinoTypes.h"
#include "GFCompanionRoster.generated.h"

/** One owned dino. Level and XP come from Dino Arena matches (ability 3 unlocks at level 3). */
USTRUCT(BlueprintType)
struct GFCOMPANION_API FGFCompanionEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") FGuid Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") FName Species;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") bool bInParty = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") int32 Level = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") int32 XP = 0;
	/** Elders watch. They do not take a spot on the sand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") bool bElder = false;
	/** Rented for this bout. At 0 HP it quits instead of fainting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") bool bRental = false;
	/** Family id for rep. None means no family band applies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") FName Family;
	/** Factory wages and arena purse share this number. First pick still follows stats and HP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") int32 LifetimeEarnings = 0;
};

/** Roster helpers shared by the game-instance roster and the networked player state. */
struct GFCOMPANION_API FGFCompanions
{
	/** PLAN.md: up to 3 followers in the party; the rest are stationed at home. */
	static constexpr int32 MaxParty = 3;
	static FGuid AddTamed(TArray<FGFCompanionEntry>& Roster, FName Species, const FString& Name);
	static int32 PartySize(const TArray<FGFCompanionEntry>& Roster);
	static constexpr int32 MaxLevel = 10;
	static constexpr int32 XPPerLevel = 5;
	/** Adds XP and recomputes the level (1 + XP / 5, capped at 10). Returns true if it levelled up. */
	static bool AddXP(FGFCompanionEntry& Entry, int32 Amount);
	static FGFCompanionEntry* Find(TArray<FGFCompanionEntry>& Roster, const FGuid& Id);
	static const FGFCompanionEntry* Find(const TArray<FGFCompanionEntry>& Roster, const FGuid& Id);
};

/** Cross-mode companion state (PLAN.md section 4) plus the per-save diet knowledge book. */
UCLASS()
class GFCOMPANION_API UGFCompanionRoster : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "GF|Companion")
	TArray<FGFCompanionEntry> Companions;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "GF|Companion")
	FGFDiscoveredDinoPreferences DiscoveredPreferences;

	UFUNCTION(BlueprintCallable, Category = "GF|Companion")
	FGuid AddCompanion(FName Species, const FString& Name);

	void RecordFeeding(FName Species, FName Food, EGFFeedResult Result)
	{
		FGFDinoDiet::RecordDiscovery(DiscoveredPreferences, Species, Food, Result);
	}
};
