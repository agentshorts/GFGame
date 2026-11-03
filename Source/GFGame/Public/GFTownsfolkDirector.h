#pragma once

// Server spawner for the three towns. Tagged TargetPoints win. Otherwise config offsets around each faction anchor.
// AGFGameMode starts it with the other startup directors. It only fills L_World_Factions_A.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFTownsfolkRules.h"
#include "GFTownsfolkDirector.generated.h"

class AGFVillager;

UCLASS(Config = Game)
class GFGAME_API AGFTownsfolkDirector : public AActor
{
	GENERATED_BODY()

public:
	AGFTownsfolkDirector();

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Townsfolk", meta = (ClampMin = "0"))
	int32 VillagersPerTown = FGFTownsfolkRules::VillagersPerTown;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Townsfolk", meta = (ClampMin = "0"))
	int32 EldersPerTown = FGFTownsfolkRules::EldersPerTown;

	/** From the faction anchor. Short lists grow a small ring so the count still fills. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Townsfolk")
	TArray<FVector> VillagerOffsets;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Townsfolk")
	FVector ElderOffset = FVector(160.f, 160.f, 88.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Townsfolk", meta = (ClampMin = "0"))
	float InteractReach = FGFTownsfolkRules::InteractReachCm;

	/** 0 uses UGFWanderComponent's wander radius. The town disc still clamps the walk. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Townsfolk", meta = (ClampMin = "0"))
	float WanderRadius = 0.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Townsfolk", meta = (ClampMin = "0"))
	float WalkSpeed = 140.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "GF|Townsfolk", meta = (ClampMin = "0"))
	float TalkSeconds = 4.f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SpawnAll();
	void GatherTagged(TArray<FGFTownsfolkSpot>& Out) const;
	const TArray<FGFTerritoryFaction>* Layout(FGFTerritoryCurve& OutCurve) const;

	UPROPERTY()
	TArray<TObjectPtr<AGFVillager>> Townsfolk;
};
