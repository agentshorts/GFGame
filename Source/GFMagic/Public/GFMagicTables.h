#pragma once

// CSV lookups. FGFDataTables reads Data/CSV when the DT_* asset is missing.

#include "CoreMinimal.h"
#include "GFMagicTypes.h"

class UDataTable;

struct GFMAGIC_API FGFMagicCatalog
{
	static const FGFBlockAffinityRow* Affinity(FName BlockId);
	static FGFBlockAffinityRow AffinityOrDefault(FName BlockId);
	static const FGFMiningSpellRow* Spell(FName SpellId);
	static const FGFMiningSpellRow* FieldSpell(EGFMagicElement Element);
	static const FGFClawcallFormRow* Form(FName FactionId);
	static const FGFSpellUpgradeRow* Upgrade(FName UpgradeId);
	static const FGFRootmaulMoveRow* Move(FName Form, FName MoveId);
	static const UDataTable* UpgradeTable();
	static const UDataTable* MoveTable();
	static const UDataTable* ComboTable();
	static float BlockHardness(FName BlockId);
	static FName BlockDrop(FName BlockId);

	/** Crystal pool from the Crystal spell row, or 100 / 25 when that row is missing. */
	static float ChargeMax();
	static float ChargeRegen();
};
