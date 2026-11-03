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

private:
	void HandleCrystal(UWorld* World, const FString& NodeKey, FVector Location, const TArray<FString>& ContributorKeys);
	void HandleHireAsk(AActor* Villager);
	void CommitHire(int32 CaseIndex);
	void Refresh(float DeltaSeconds);
	void StandDown(int32 CaseIndex);
	void ClearPlayer(const FString& PlayerKey, FName TownId);

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
		TArray<TWeakObjectPtr<AGFDino>> Hired;
	};

	TArray<FCase> Cases;
	TMap<FString, FGFTheftHeat> Heats;
	FDelegateHandle CrystalHandle;
	FDelegateHandle HireHandle;
	int32 NextCaseId = 1;
};
