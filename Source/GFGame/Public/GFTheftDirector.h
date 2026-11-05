#pragma once

// Server response to a town crystal break. Witnesses warn, flee to their elder, and the elder hires once.
// AGFGameMode starts it with the other startup directors.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFTheftRules.h"
#include "GFTheftDirector.generated.h"

class AGFDino;

UCLASS()
class GFGAME_API AGFTheftDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFTheftDirector();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The town a world point belongs to (its zone, else the nearest camp in reach). None in the wild. Read only. */
	static FName TownAt(UWorld* World, const FVector& Location);

	/**
	 * The hire spawn this director uses for its own cases: one AGFDino of Species on the ring MinCm..MaxCm around
	 * Center, homed where it lands. Null when the world or species is missing. Starts no case and touches no heat.
	 */
	static AGFDino* SpawnHireBody(UWorld* World, const FVector& Center, FName Species, float MinCm, float MaxCm);
	/** Lets one hire go the way a case does (FGFTheftRules::ReleaseFor): despawn a hire spawn, release a borrowed dino, keep a tame. */
	static void LetGoHire(AGFDino* Dino, bool bSpawned, bool bWorldEnding = false);

private:
	void HandleCrystal(UWorld* World, const FString& NodeKey, FVector Location, const TArray<FString>& ContributorKeys);
	void HandleHireAsk(AActor* Villager);
	void CommitHire(int32 CaseIndex);
	void Refresh(float DeltaSeconds);
	void StandDown(int32 CaseIndex);
	void ClearPlayer(const FString& PlayerKey, FName TownId);
	/** Lets go of every dino the case still holds (see FGFTheftRules::ReleaseFor). */
	void LetGoAll(int32 CaseIndex, bool bWorldEnding = false);
	/** The director is leaving with cases still open: every hire it holds is let go so none stays hired. */
	void LetGoEveryCase(bool bWorldEnding);
	/** A quest left at Escape with no live heat here (a reload, a restart) is cleared rather than stuck. */
	void SettleStrandedEscapes();
	/** Push each heat snapshot to that player's controller. The rules stay on this actor. */
	void PublishHeats();

	struct FCase
	{
		int32 Id = 0;
		FName TownId;
		FString ThiefKey;
		TArray<FString> Players;
		FVector Incident = FVector::ZeroVector;
		double OpenedAt = 0.0;
		bool bHired = false;
		bool bClosed = false;
		struct FHire
		{
			TWeakObjectPtr<AGFDino> Dino;
			/** Spawned for this hire, so the case owns the body and despawns it. */
			bool bSpawned = false;
		};
		TArray<FHire> Hired;
	};

	TArray<FCase> Cases;
	FGFTheftHeatBook Heats;
	FDelegateHandle CrystalHandle;
	FDelegateHandle HireHandle;
	int32 NextCaseId = 1;
};
