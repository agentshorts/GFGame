#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFDinoTypes.h"
#include "GFTamingComponent.generated.h"

class UDataTable;
class APawn;
class IGFTamer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGFOnTameStateChanged, EGFTameState, NewState);

/**
 * Server-authoritative taming (PLAN.md M2). Runs FGFTameMachine on the server against every player in the
 * game state (IGFTamer), writes Field Journal discoveries (sightings, sniff half-reveals, offers) and shares
 * them with friends within 30 m. State, bond, starter, owner and target replicate to everyone.
 */
UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFCOMPANION_API UGFTamingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFTamingComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Rows: FDinoDietRow. Empty = Data/CSV/DT_DinoDiet.csv. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Taming")
	TObjectPtr<UDataTable> DietTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "GF|Taming")
	FName Species;

	/** Dinos sharing a PackId (and a pack species) turn hostile together. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Taming")
	FName PackId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Taming")
	float PackCallRadius = 2000.f;

	/** cm. Offers from farther away are refused as NotNow (server check, with a little slack for lag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Taming")
	float OfferRange = 300.f;

	static constexpr float JournalShareRadius = 3000.f;
	static constexpr float OfferRangeSlack = 100.f;

	/** Server: Tamer offers Food. Validates reach; the caller checks and removes the item. Writes journals. */
	EGFOfferResult ServerOffer(UObject* Tamer, FName Food);
	/** Server: hostile at Key (pack call-in). */
	void ServerForceHostile(const FString& Key);

	UFUNCTION(BlueprintPure, Category = "GF|Taming")
	EGFTameState GetTameState() const { return RepState; }
	UFUNCTION(BlueprintPure, Category = "GF|Taming")
	float GetBond() const { return RepBond; }
	UFUNCTION(BlueprintPure, Category = "GF|Taming")
	FString GetStarterKey() const { return RepStarterKey; }
	UFUNCTION(BlueprintPure, Category = "GF|Taming")
	FString GetOwnerKey() const { return RepOwnerKey; }
	UFUNCTION(BlueprintPure, Category = "GF|Taming")
	FString GetTargetKey() const { return RepTargetKey; }

	/** Companion entry id in the owner's roster, once tamed. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Taming")
	FGuid CompanionId;

	const FGFTameMachine& GetMachine() const { return Machine; }
	const FDinoDietRow* GetDietRow() const;
	void SetDietRowOverride(const FDinoDietRow& Row) { DietOverride = Row; bHasDietOverride = true; }

	UPROPERTY(BlueprintAssignable, Category = "GF|Taming")
	FGFOnTameStateChanged OnTameStateChanged;

	/** Every IGFTamer in this world's game state. */
	static TArray<IGFTamer*> GetTamers(const UWorld* World);
	static IGFTamer* FindTamer(const UWorld* World, const FString& Key);
	static APawn* FindTamerPawn(const UWorld* World, const FString& Key);

protected:
	void SyncReplicated();
	void TickServer(float DeltaTime);
	void CallPack(const FString& Key);
	/** Applies Fn to Source's journal; if it learned something, the same is merged into nearby sharers. */
	void RecordAndShare(IGFTamer* Source, TFunctionRef<bool(FGFDiscoveredDinoPreferences&)> Fn);

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(ReplicatedUsing = OnRep_State)
	EGFTameState RepState = EGFTameState::Wild;
	UPROPERTY(Replicated)
	float RepBond = 0.f;
	UPROPERTY(Replicated)
	FString RepStarterKey;
	UPROPERTY(Replicated)
	FString RepOwnerKey;
	UPROPERTY(Replicated)
	FString RepTargetKey;

	UPROPERTY(Transient)
	FGFTameMachine Machine;

	FDinoDietRow DietOverride;
	bool bHasDietOverride = false;
};
