#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

/**
 * DataTable access with a CSV fallback. Data/CSV/*.csv is the source of truth (PLAN.md section 5).
 * If the DT_* asset isn't imported yet (or a soft reference is empty), the CSV is read at runtime into a
 * transient table, so greybox builds and headless tests work before any asset import.
 * TODO(M0 packaging): import the DT_* assets (Tools/import_datatables.py) or stage Data/CSV as non-UFS.
 */
struct GFCORE_API FGFDataTables
{
	/** Asset if set and loadable, else Data/CSV/<CsvName>.csv parsed with RowStruct. Cached per process. */
	static UDataTable* Get(const TSoftObjectPtr<UDataTable>& Asset, const TCHAR* CsvName, UScriptStruct* RowStruct);
	static UDataTable* FromCsv(const TCHAR* CsvName, UScriptStruct* RowStruct);
	static FString CsvPath(const TCHAR* CsvName);
	/** Drops the CSV cache (tests that rewrite CSVs). */
	static void ResetCache();

	template <typename RowType>
	static UDataTable* Get(const TCHAR* CsvName)
	{
		return Get(TSoftObjectPtr<UDataTable>(), CsvName, RowType::StaticStruct());
	}
};
