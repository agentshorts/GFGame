#include "GFDataTables.h"
#include "GFLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace GFDataTablesPrivate
{
	static TMap<FString, TObjectPtr<UDataTable>>& Cache()
	{
		static TMap<FString, TObjectPtr<UDataTable>> Map;
		return Map;
	}
}

FString FGFDataTables::CsvPath(const TCHAR* CsvName)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/CSV") / (FString(CsvName) + TEXT(".csv")));
}

void FGFDataTables::ResetCache()
{
	for (TPair<FString, TObjectPtr<UDataTable>>& Pair : GFDataTablesPrivate::Cache())
	{
		if (Pair.Value)
		{
			Pair.Value->RemoveFromRoot();
		}
	}
	GFDataTablesPrivate::Cache().Reset();
}

UDataTable* FGFDataTables::FromCsv(const TCHAR* CsvName, UScriptStruct* RowStruct)
{
	if (!RowStruct)
	{
		return nullptr;
	}
	const FString Key = FString(CsvName) + TEXT("|") + RowStruct->GetPathName();
	if (TObjectPtr<UDataTable>* Found = GFDataTablesPrivate::Cache().Find(Key))
	{
		return *Found;
	}
	FString Csv;
	if (!FFileHelper::LoadFileToString(Csv, *CsvPath(CsvName)))
	{
		UE_LOG(LogGF, Warning, TEXT("GFDataTables: no asset and no CSV at %s"), *CsvPath(CsvName));
		return nullptr;
	}
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDataTable::StaticClass(), FName(CsvName)));
	Table->RowStruct = RowStruct;
	const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
	for (const FString& P : Problems)
	{
		UE_LOG(LogGF, Warning, TEXT("GFDataTables %s: %s"), CsvName, *P);
	}
	Table->AddToRoot();
	GFDataTablesPrivate::Cache().Add(Key, Table);
	return Table;
}

UDataTable* FGFDataTables::Get(const TSoftObjectPtr<UDataTable>& Asset, const TCHAR* CsvName, UScriptStruct* RowStruct)
{
	if (!Asset.IsNull())
	{
		if (UDataTable* Loaded = Asset.LoadSynchronous())
		{
			return Loaded;
		}
	}
	return FromCsv(CsvName, RowStruct);
}
