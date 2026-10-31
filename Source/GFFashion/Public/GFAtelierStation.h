#pragma once

// Placeable Atelier. Every fashion recipe is Station = Atelier. Standing here, on the server,
// calls UGFCraftingSubsystem::Craft(Id, "Atelier", Inventory). A client does not change the bag.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFCraftingSubsystem.h"
#include "GFAtelierStation.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class APawn;

UCLASS(Blueprintable)
class GFFASHION_API AGFAtelierStation : public AActor
{
	GENERATED_BODY()

public:
	AGFAtelierStation();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The station id written on fashion rows in DT_Recipes. */
	static const FName StationId;

	/** How close a pawn must stand (cm, horizontal) before the bench will sew. */
	static constexpr float CraftReachCm = 300.f;

	/**
	 * Server. Crafts RecipeId at this bench through UGFCraftingSubsystem::Craft.
	 * A client gets NotAuthority and the bag is left alone.
	 * A pawn farther than CraftReachCm gets WrongStation and the bag is left alone.
	 */
	EGFCraftResult TryCraft(APawn* Pawn, FName RecipeId);

	/**
	 * Server. Pawns standing at the bench craft BenchRecipeId once per visit.
	 * Empty BenchRecipeId does nothing. Safe to call from tests; Tick calls it too.
	 */
	void PollBench();

	/**
	 * Automation. A world created for a test is always authority, so a client pass sets this false.
	 * The bag must stay unchanged when it is false.
	 */
	void SetCraftAuthorityForTests(bool bAuthority);

	UStaticMeshComponent* GetBenchMesh() const { return BenchMesh; }
	FString GetSignText() const;

	/** Recipe sewn when someone stands in the volume. None means the bench waits for TryCraft. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Atelier")
	FName BenchRecipeId;

	/** Last TryCraft result. The prompt string is what replicates. */
	EGFCraftResult LastResult = EGFCraftResult::UnknownRecipe;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Atelier")
	FString LastPrompt;

protected:
	bool HasCraftAuthority() const;
	bool IsPawnAtBench(const APawn* Pawn) const;

	UPROPERTY(VisibleAnywhere, Category = "GF|Atelier")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "GF|Atelier")
	TObjectPtr<UStaticMeshComponent> BenchMesh;

	UPROPERTY(VisibleAnywhere, Category = "GF|Atelier")
	TObjectPtr<UTextRenderComponent> Sign;

	UPROPERTY(VisibleAnywhere, Category = "GF|Atelier")
	TObjectPtr<UBoxComponent> BenchVolume;

private:
	UPROPERTY(Transient)
	TObjectPtr<UGFCraftingSubsystem> FallbackCrafting;

	bool bCraftAuthorityOverride = false;
	bool bForcedCraftAuthority = true;

	/** Pawns already sewn for this stay, so a tick does not sew the same visit twice. */
	TSet<TWeakObjectPtr<APawn>> AtBench;

	void ApplyBenchTint();
	UGFCraftingSubsystem* ResolveCrafting();
	EGFCraftResult Note(EGFCraftResult Result, const TCHAR* Prompt = nullptr);
};
