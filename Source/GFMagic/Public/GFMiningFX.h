#pragma once

// Mining FX v1 (magic mining v3, section 8.10).
// One cosmetic event covers a hit, a crack stage, and a break. The server always raises
// OnPlayed. Clients hear the same event through an unreliable multicast and spawn Niagara
// from DT_MiningFX. Rows point at the Niagara systems in /Game/FX/Mining.
// A missing system logs once and leaves the previous mesh in place.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFMagicTypes.h"
#include "UObject/SoftObjectPath.h"
#include "GFMiningFX.generated.h"

class UNiagaraSystem;
class UWorld;

UENUM()
enum class EGFMiningFXKind : uint8
{
	Hit,
	Crack,
	Break,
	Beam
};

/** Tree and crystal pick the chip and shard rows. None is a plain voxel. Any is a table wildcard. */
UENUM()
enum class EGFMiningFXKit : uint8
{
	None,
	Tree,
	Crystal,
	Any
};

USTRUCT(BlueprintType)
struct GFMAGIC_API FGFMiningFXRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic")
	EGFMiningFXKind Kind = EGFMiningFXKind::Hit;

	/** Spark, Magma, Root, Frost, or Any. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic")
	FName Element;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic")
	EGFMiningFXKit Kit = EGFMiningFXKit::Any;

	/** A /Game or /Plugin soft object path, or a project-relative file. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic")
	FString SystemPath;
};

USTRUCT()
struct GFMAGIC_API FGFMiningFXEvent
{
	GENERATED_BODY()

	UPROPERTY()
	EGFMiningFXKind Kind = EGFMiningFXKind::Hit;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FVector Normal = FVector::UpVector;

	UPROPERTY()
	EGFMagicElement Element = EGFMagicElement::Spark;

	UPROPERTY()
	EGFMiningFXKit Kit = EGFMiningFXKit::None;

	/** 1, 2, or 3 when Kind is Crack (75%, 50%, 25% of integrity left). */
	UPROPERTY()
	int32 CrackStage = 0;

	/** Player key of the caster. Empty when the event has no owner, so remote clients do not shake for it. */
	UPROPERTY()
	FName CasterId;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FGFOnMiningFX, const FGFMiningFXEvent& /*Event*/);

/** Replicates the cosmetic event. Visuals spawn on each machine. */
UCLASS()
class GFMAGIC_API AGFMiningFXRelay : public AActor
{
	GENERATED_BODY()

public:
	AGFMiningFXRelay();

	/** Authority: spawn here when this machine has visuals, and multicast to remote clients. */
	void Dispatch(const FGFMiningFXEvent& Event);

private:
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCosmetic(FGFMiningFXEvent Event);
};

struct GFMAGIC_API FGFMiningFX
{
	static FGFOnMiningFX& OnPlayed();
	/** Fired where the cosmetic is drawn (listen host and clients). The server break does not use this. */
	static FGFOnMiningFX& OnCosmetic();

	static FName ElementName(EGFMagicElement Element);
	static int32 CrackStage(float Remaining, float Full);
	static EGFMiningFXKit KitForBlock(FName BlockId);

	/** Hit, and Crack when a stage is crossed. A break emits only Break. */
	static void Collect(float BeforeRemaining, float AfterRemaining, float Full, bool bBroke, EGFMagicElement Element, EGFMiningFXKit Kit, const FVector& Location, const FVector& Normal, TArray<FGFMiningFXEvent>& Out);

	static const UDataTable* Table();
	static FString ResolveFile(const FString& SystemPath);
	static bool RowResolves(const FGFMiningFXRow& Row);
	static FSoftObjectPath SoftPathFor(const FString& SystemPath);

	/** Rows whose kind, element and kit match, including Any wildcards. */
	static void Gather(EGFMiningFXKind Kind, EGFMagicElement Element, EGFMiningFXKit Kit, TArray<const FGFMiningFXRow*>& Out);

	/** Null when the path is not a Niagara system. Logs once per path. */
	static UNiagaraSystem* LoadSystem(const FString& SystemPath);
	static UNiagaraSystem* LoadFor(EGFMiningFXKind Kind, EGFMagicElement Element, EGFMiningFXKit Kit);

	static void SpawnCosmetic(UWorld* World, const FGFMiningFXEvent& Event);
};
