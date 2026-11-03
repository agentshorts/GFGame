#pragma once

#include "CoreMinimal.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFFashionTypes.h"
#include "GFFashionQuest.h"
#include "GFFashionGateSubsystem.generated.h"

/** Fired while the world being left is still up. The quest travel listener stashes the carry. */
DECLARE_MULTICAST_DELEGATE_OneParam(FGFOnFashionMapTravel, UWorld* /*Leaving*/);

/** Gate C: fashion show (M3). Wardrobe and theme lists for the UI; runs live on AGFRunwayStage. */
UCLASS()
class GFFASHION_API UGFFashionGateSubsystem : public UGFMiniGameGateSubsystem
{
	GENERATED_BODY()

public:
	virtual FName GetGateId() const override { return TEXT("Fashion"); }
	virtual FName GetMapName() const override { return TEXT("/Game/Maps/L_Runway"); }
	virtual FText GetDisplayName() const override { return FText::FromString(TEXT("Runway")); }

	UFUNCTION(BlueprintPure, Category = "GF|Fashion")
	TArray<FGFFashionTheme> GetThemes() const { return FGFFashionCatalog::Themes(); }

	UFUNCTION(BlueprintPure, Category = "GF|Fashion")
	TArray<FGFFashionPiece> GetWardrobe() const { return FGFFashionCatalog::Pieces(); }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The runway gate and the hub return both call this before the map load. */
	void NotifyMapTravel(UWorld* Leaving);

	void StashQuestCarry(TArray<uint8> QuestBytes, TArray<FGFFactionLeader> Leaders, TMap<FString, FName> Sessions);
	bool TakeQuestCarry(TArray<uint8>& QuestBytes, TArray<FGFFactionLeader>& Leaders, TMap<FString, FName>& Sessions);
	EGFFashionCarryState GetQuestCarryState() const { return Carry.State; }

	FGFOnFashionMapTravel OnMapTravel;

private:
	void HandleBeforeEnter(FName GateId);

	FGFFashionCarry Carry;
	FDelegateHandle BeforeEnterHandle;
};
