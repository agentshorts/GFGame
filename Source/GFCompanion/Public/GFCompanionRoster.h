#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFDinoTypes.h"
#include "GFCompanionRoster.generated.h"

/** One owned dino (stub; M2 adds level, accessories). */
USTRUCT(BlueprintType)
struct GFCOMPANION_API FGFCompanionEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") FGuid Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") FName Species;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Companion") bool bInParty = true;
};

/** Roster helpers shared by the game-instance roster and the networked player state. */
struct GFCOMPANION_API FGFCompanions
{
	/** PLAN.md: up to 3 followers in the party; the rest are stationed at home. */
	static constexpr int32 MaxParty = 3;
	static FGuid AddTamed(TArray<FGFCompanionEntry>& Roster, FName Species, const FString& Name);
	static int32 PartySize(const TArray<FGFCompanionEntry>& Roster);
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
