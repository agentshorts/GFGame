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
	/** Fraction of Freeze a shatter pushes into this block. Ice, Glass and Crystal Vein are 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float FreezeSpread = 0.3f;
	/** Clawcall never breaks this, whichever element the summon is wearing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bClawcallImmune = false;
};

/**
 * One DT_MiningSpells row.
 * Row name = spell id (Sparkcone, MagmaSpray, Rootmaul, Frostbind, Clawcall, Crystal, ...).
 * Shape is Cone, Spray, Melee, Freeze, Claw, Storm or Pool. Numbers the shape does not use stay 0.
 */
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName Shape;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float CostPerSecond = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ConeHalfAngle = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ConePower = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float FalloffStartCm = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float FalloffMin = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 Penetration = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 CellCap = 48;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float GlobRate = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float GlobSpeed = 1600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float GlobGravityScale = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float GlobVolume = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float PuddleDissolve = 80.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float PuddleMaxVolume = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 PuddleMaxDepth = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float PuddleCoolSeconds = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float PuddleSpill = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float PuddleSlow = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 MaxPuddles = 24;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 AreaCap = 128;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 AreaCells = 64;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float SmearSeconds = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float MeleeReachCm = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float HeavyHoldSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float RhythmWindow = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float RhythmStep = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float RhythmMax = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float MeleeSlack = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float FreezeRate = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ContractionSeconds = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 ChainGenerations = 4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 ChainMaxCells = 64;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 QuenchRadius = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float MarkSeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 ConductiveCap = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float LightIntensity = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float RegenDelay = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ChainWindow = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 ChainPeak = 20;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ChainBonus = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float OverchargeHalfAngle = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 MinRays = 19;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 MaxRays = 96;
};

/** One DT_RootmaulMoves row. Row name is Form_MoveId. MoveId is the combo key. */
USTRUCT(BlueprintType)
struct GFMAGIC_API FGFRootmaulMoveRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName Form;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName MoveId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName MontageSection;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName Input;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 Width = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 Height = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") int32 Depth = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float Power = 120.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float ChargeCost = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float LungeCm = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName NextOnLight;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") FName NextOnHeavy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bPitch = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") bool bRooted = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic") float TimeSeconds = 0.4f;
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
