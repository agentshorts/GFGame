#pragma once

// Tailor bench on the runway. Craft cost lives on the piece (DT_FashionPieces), not in DT_Recipes.
// Dyes, brands and the atelier stay where they are.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFWardrobe.h"
#include "GFTailorStation.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class APawn;

UCLASS()
class GFFASHION_API AGFTailorStation : public AActor
{
	GENERATED_BODY()

public:
	AGFTailorStation();

	virtual void BeginPlay() override;

	static const FName StationId;
	static constexpr float CraftReachCm = 300.f;

	/** Server. A pawn farther than CraftReachCm is refused and spends nothing. */
	EGFWardrobeResult TryCraft(APawn* Pawn, FName PieceId);

	FString GetSignText() const;

	UPROPERTY(VisibleAnywhere, Category = "GF|Fashion")
	TObjectPtr<UStaticMeshComponent> BenchMesh;

	UPROPERTY(VisibleAnywhere, Category = "GF|Fashion")
	TObjectPtr<UTextRenderComponent> Sign;

	EGFWardrobeResult LastResult = EGFWardrobeResult::Refused;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "GF|Fashion")
	FString LastPrompt;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
};
