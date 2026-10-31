#pragma once

// Rows for the magic-mining CSVs. Data/CSV is the source of truth; the DT_* assets are optional.

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFMagicTypes.generated.h"

/** Spark is everyone's field. The other three are the faction elements. */
UENUM(BlueprintType)
enum class EGFMagicElement : uint8
{
	Spark,
	Magma,
	Root,
	Frost
};

/** One DT_BlockAffinity row. Row name = block id. */
USTRUCT(BlueprintType)
struct GFMAGIC_API FGFBlockAffinityRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float Spark = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float Magma = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float Root = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float Frost = 0.5f;
	/** Fabric, and anything else a spell must never break. Built cells are spell-proof on top of this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bSpellProof = false;
	/** Spark jumps to orthogonal neighbours of the same block. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bConductive = false;
	/** Rootfield fells a connected run of these (logs and leaves). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bTree = false;
	/** Soil the root field eats, and that Root Network can chain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bSoil = false;
	/** Ore a jackpot upgrade can pay twice. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bOre = false;
	/** When Frost breaks this, the cell becomes this block instead of a drop. Lava quenches to Basalt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName FrostTransform;
	/** Drop used when Magma breaks this. Empty keeps ItemForBlock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName MagmaDrop;
};

/** One DT_MiningSpells row. Row name = spell id (Sparkfield, Clawcall, Crystal, ...). */
USTRUCT(BlueprintType)
struct GFMAGIC_API FGFMiningSpellRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") EGFMagicElement Element = EGFMagicElement::Spark;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 RadiusCells = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float RangeCm = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float IntegrityPerSecond = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float FlatIntegrity = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 TunnelLength = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 TunnelWidth = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 TunnelHeight = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 MaxCharges = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ChargeCost = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float RechargeSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bChannel = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ChargeMax = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ChargeRegen = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float OverchargeSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float OverchargeRate = 1.f;
};

/** One DT_ClawcallForms row. Row name = faction id. */
USTRUCT(BlueprintType)
struct GFMAGIC_API FGFClawcallFormRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName Form;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") EGFMagicElement Element = EGFMagicElement::Spark;
};

/** One DT_SpellUpgrades row. Row name = upgrade id. First tier only in this slice. */
USTRUCT(BlueprintType)
struct GFMAGIC_API FGFSpellUpgradeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** "Faction" or "Path". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName Tree;
	/** Faction id, or a path id (Delver, Wrangler, Scavenger, UnnamedFourth, UnnamedFifth). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName Owner;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 Tier = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName CostItemA;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 CostCountA = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName CostItemB;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 CostCountB = 1;
	/** RangeCm, ClawLength, IntegrityRate, SoilChain, Stairwell, ExtraDrop, OreGlint, BondCall, WorkingDinos, Jackpot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName Effect;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float Magnitude = 0.f;
};

/** One DT_MiningCombos row. Data is in place; the reactions themselves are not in this slice. */
USTRUCT(BlueprintType)
struct GFMAGIC_API FGFMiningComboRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FString Elements;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName Result;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bImplemented = false;
};

namespace GFMagic
{
	/** Server field step. Ten times a second. */
	static constexpr float TickSeconds = 0.1f;
	/** Cells a tick will damage, across every caster. */
	static constexpr int32 CellsPerTick = 64;
	/** A shorter press is a tap (punch, or pick up a build), not a mine. */
	static constexpr float TapSeconds = 0.2f;

	static constexpr int32 InputField = 1;
	static constexpr int32 InputClaw = 2;
	static constexpr int32 InputToggle = 3;
	static constexpr int32 InputOvercharge = 4;
}
