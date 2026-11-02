#include "GFMagicTables.h"
#include "GFDataTables.h"
#include "GFVoxelTypes.h"
#include "Engine/DataTable.h"

namespace
{
	const UDataTable* Table(const TCHAR* Name, UScriptStruct* Row)
	{
		return FGFDataTables::Get(TSoftObjectPtr<UDataTable>(), Name, Row);
	}
}

const FGFBlockAffinityRow* FGFMagicCatalog::Affinity(FName BlockId)
{
	const UDataTable* Rows = Table(TEXT("DT_BlockAffinity"), FGFBlockAffinityRow::StaticStruct());
	return Rows ? Rows->FindRow<FGFBlockAffinityRow>(BlockId, TEXT("MagicAffinity"), false) : nullptr;
}

FGFBlockAffinityRow FGFMagicCatalog::AffinityOrDefault(FName BlockId)
{
	if (const FGFBlockAffinityRow* Row = Affinity(BlockId))
	{
		return *Row;
	}
	FGFBlockAffinityRow Fallback;
	Fallback.Spark = 1.f;
	Fallback.Magma = 0.5f;
	Fallback.Root = 0.5f;
	Fallback.Frost = 0.5f;
	return Fallback;
}

const FGFMiningSpellRow* FGFMagicCatalog::Spell(FName SpellId)
{
	const UDataTable* Rows = Table(TEXT("DT_MiningSpells"), FGFMiningSpellRow::StaticStruct());
	return Rows ? Rows->FindRow<FGFMiningSpellRow>(SpellId, TEXT("MagicSpell"), false) : nullptr;
}

const FGFMiningSpellRow* FGFMagicCatalog::FieldSpell(EGFMagicElement Element)
{
	const TCHAR* Id = TEXT("Sparkcone");
	const TCHAR* Legacy = TEXT("Sparkfield");
	switch (Element)
	{
	case EGFMagicElement::Magma: Id = TEXT("MagmaSpray"); Legacy = TEXT("Magmafield"); break;
	case EGFMagicElement::Root: Id = TEXT("Rootmaul"); Legacy = TEXT("Rootfield"); break;
	case EGFMagicElement::Frost: Id = TEXT("Frostbind"); Legacy = TEXT("Frostfield"); break;
	default: break;
	}
	if (const FGFMiningSpellRow* Row = Spell(Id))
	{
		return Row;
	}
	return Spell(Legacy);
}

const FGFClawcallFormRow* FGFMagicCatalog::Form(FName FactionId)
{
	const UDataTable* Rows = Table(TEXT("DT_ClawcallForms"), FGFClawcallFormRow::StaticStruct());
	return Rows ? Rows->FindRow<FGFClawcallFormRow>(FactionId, TEXT("MagicForm"), false) : nullptr;
}

const FGFSpellUpgradeRow* FGFMagicCatalog::Upgrade(FName UpgradeId)
{
	const UDataTable* Rows = UpgradeTable();
	return Rows ? Rows->FindRow<FGFSpellUpgradeRow>(UpgradeId, TEXT("MagicUpgrade"), false) : nullptr;
}

const UDataTable* FGFMagicCatalog::UpgradeTable()
{
	return Table(TEXT("DT_SpellUpgrades"), FGFSpellUpgradeRow::StaticStruct());
}

const UDataTable* FGFMagicCatalog::MoveTable()
{
	return Table(TEXT("DT_RootmaulMoves"), FGFRootmaulMoveRow::StaticStruct());
}

const FGFRootmaulMoveRow* FGFMagicCatalog::Move(FName Form, FName MoveId)
{
	const UDataTable* Rows = MoveTable();
	if (!Rows)
	{
		return nullptr;
	}
	const FGFRootmaulMoveRow* Fallback = nullptr;
	for (const FName& Name : Rows->GetRowNames())
	{
		const FGFRootmaulMoveRow* Row = Rows->FindRow<FGFRootmaulMoveRow>(Name, TEXT("MagicMove"), false);
		if (!Row || Row->MoveId != MoveId)
		{
			continue;
		}
		if (Row->Form == Form)
		{
			return Row;
		}
		if (Row->Form == TEXT("Rootmaul"))
		{
			Fallback = Row;
		}
	}
	return Fallback;
}

const UDataTable* FGFMagicCatalog::ComboTable()
{
	return Table(TEXT("DT_MiningCombos"), FGFMiningComboRow::StaticStruct());
}

float FGFMagicCatalog::BlockHardness(FName BlockId)
{
	const UDataTable* Rows = FGFDataTables::Get<FGFBlockRow>(TEXT("DT_Blocks"));
	const FGFBlockRow* Row = Rows ? Rows->FindRow<FGFBlockRow>(BlockId, TEXT("MagicBlock"), false) : nullptr;
	return Row ? Row->Hardness : 1.f;
}

FName FGFMagicCatalog::BlockDrop(FName BlockId)
{
	const UDataTable* Rows = FGFDataTables::Get<FGFBlockRow>(TEXT("DT_Blocks"));
	const FGFBlockRow* Row = Rows ? Rows->FindRow<FGFBlockRow>(BlockId, TEXT("MagicBlock"), false) : nullptr;
	if (!Row)
	{
		return BlockId;
	}
	return Row->DropItem.IsNone() ? BlockId : Row->DropItem;
}

float FGFMagicCatalog::ChargeMax()
{
	const FGFMiningSpellRow* Row = Spell(TEXT("Crystal"));
	return (Row && Row->ChargeMax > 0.f) ? Row->ChargeMax : 100.f;
}

float FGFMagicCatalog::ChargeRegen()
{
	const FGFMiningSpellRow* Row = Spell(TEXT("Crystal"));
	return (Row && Row->ChargeRegen > 0.f) ? Row->ChargeRegen : 25.f;
}
