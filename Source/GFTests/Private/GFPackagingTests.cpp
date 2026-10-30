// Guards the Win64 package: every CSV FGFDataTables loads is on disk at CsvPath(),
// and the stage list covers every file in Data/CSV.
#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFPackagingTest
{
	static bool IsGeneratedPath(const FString& Path)
	{
		const FString Slash = Path.Replace(TEXT("\\"), TEXT("/"));
		return Slash.Contains(TEXT("/Intermediate/")) || Slash.Contains(TEXT("/Binaries/"));
	}

	static bool ExtractCsvName(const FString& Statement, FString& OutName)
	{
		const int32 Marker = Statement.Find(TEXT("TEXT(\""), ESearchCase::CaseSensitive);
		if (Marker == INDEX_NONE)
		{
			return false;
		}
		const int32 Value = Marker + 6;
		const int32 End = Statement.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Value);
		if (End == INDEX_NONE)
		{
			return false;
		}
		OutName = Statement.Mid(Value, End - Value);
		return OutName.StartsWith(TEXT("DT_")) && !OutName.Contains(TEXT("/")) && !OutName.Contains(TEXT(" "));
	}

	static void CollectLoadedCsvNames(TSet<FString>& OutNames)
	{
		IFileManager& Files = IFileManager::Get();
		TArray<FString> Sources;
		const FString Roots[] = {
			FPaths::ProjectDir() / TEXT("Source"),
			FPaths::ProjectDir() / TEXT("Plugins"),
		};
		for (const FString& Root : Roots)
		{
			Files.FindFilesRecursive(Sources, *Root, TEXT("*.cpp"), true, false, false);
			Files.FindFilesRecursive(Sources, *Root, TEXT("*.h"), true, false, false);
		}

		for (const FString& Path : Sources)
		{
			if (IsGeneratedPath(Path) || FPaths::GetCleanFilename(Path) == TEXT("GFPackagingTests.cpp"))
			{
				continue;
			}
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				continue;
			}
			int32 From = 0;
			while (From < Text.Len())
			{
				const int32 At = Text.Find(TEXT("FGFDataTables::"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (At == INDEX_NONE)
				{
					break;
				}
				const int32 WindowEnd = FMath::Min(Text.Len(), At + 320);
				FString Statement = Text.Mid(At, WindowEnd - At);
				const int32 Semi = Statement.Find(TEXT(";"));
				if (Semi != INDEX_NONE)
				{
					Statement = Statement.Left(Semi);
				}
				FString Name;
				if (ExtractCsvName(Statement, Name))
				{
					OutNames.Add(Name);
				}
				From = At + 15;
			}
		}
	}

	static void ListCsvBaseNames(TArray<FString>& OutNames)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(FPaths::ProjectDir() / TEXT("Data/CSV/*.csv")), true, false);
		for (const FString& FileName : Found)
		{
			OutNames.Add(FPaths::GetBaseFilename(FileName));
		}
	}

	static bool LineStagesCsvDirectory(const FString& BuildCs)
	{
		TArray<FString> Lines;
		BuildCs.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			if (Line.TrimStart().StartsWith(TEXT("//")))
			{
				continue;
			}
			if (Line.Contains(TEXT("RuntimeDependencies.Add"))
				&& Line.Contains(TEXT("$(ProjectDir)/Data/CSV/..."))
				&& Line.Contains(TEXT("StagedFileType.NonUFS")))
			{
				return true;
			}
		}
		return false;
	}

	static bool IniStagesCsvDirectory(const FString& GameIni)
	{
		TArray<FString> Lines;
		GameIni.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStart();
			if (Trimmed.StartsWith(TEXT(";")))
			{
				continue;
			}
			if (!Trimmed.Contains(TEXT("DirectoriesToAlwaysStageAsNonUFS")) && !Trimmed.Contains(TEXT("DirectoriesToAlwaysStageAsUFS")))
			{
				continue;
			}
			if (Trimmed.Contains(TEXT("../Data/CSV")) || Trimmed.Contains(TEXT("..\\Data\\CSV")))
			{
				return true;
			}
		}
		return false;
	}

	static bool StagingNamesFile(const FString& BuildCs, const FString& GameIni, const FString& BaseName)
	{
		const FString Token = FString::Printf(TEXT("Data/CSV/%s.csv"), *BaseName);
		auto NamedOnLiveLine = [&Token](const FString& Text, const TCHAR* CommentPrefix) -> bool
		{
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				if (Line.TrimStart().StartsWith(CommentPrefix))
				{
					continue;
				}
				if (Line.Contains(Token))
				{
					return true;
				}
			}
			return false;
		};
		return NamedOnLiveLine(BuildCs, TEXT("//")) || NamedOnLiveLine(GameIni, TEXT(";"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGFPackagingCsvPathsTest,
	"GFGame.Packaging.CsvPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGFPackagingCsvPathsTest::RunTest(const FString&)
{
	TSet<FString> Names;
	GFPackagingTest::CollectLoadedCsvNames(Names);
	TestTrue(TEXT("found CSV names FGFDataTables loads"), Names.Num() > 0);

	const TCHAR* Anchors[] = {
		TEXT("DT_Blocks"),
		TEXT("DT_Recipes"),
		TEXT("DT_DinoDiet"),
		TEXT("DT_AssetMap"),
		TEXT("DT_AnimMap"),
		TEXT("DT_GiantDinoLines"),
		TEXT("DT_Emotes"),
	};
	for (const TCHAR* Anchor : Anchors)
	{
		TestTrue(FString::Printf(TEXT("code loads %s"), Anchor), Names.Contains(FString(Anchor)));
	}

	for (const FString& Name : Names)
	{
		const FString Path = FGFDataTables::CsvPath(*Name);
		const FString Slash = Path.Replace(TEXT("\\"), TEXT("/"));
		TestTrue(FString::Printf(TEXT("%s exists at CsvPath"), *Name), FPaths::FileExists(Path));
		TestTrue(
			FString::Printf(TEXT("%s stays under Data/CSV"), *Name),
			Slash.Contains(TEXT("/Data/CSV/")) && Slash.EndsWith(Name + TEXT(".csv")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGFPackagingCsvStagingTest,
	"GFGame.Packaging.CsvStaging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGFPackagingCsvStagingTest::RunTest(const FString&)
{
	const FString BuildPath = FPaths::ProjectDir() / TEXT("Source/GFGame/GFGame.Build.cs");
	const FString IniPath = FPaths::ProjectDir() / TEXT("Config/DefaultGame.ini");
	FString BuildCs;
	FString GameIni;
	TestTrue(TEXT("read GFGame.Build.cs"), FFileHelper::LoadFileToString(BuildCs, *BuildPath));
	TestTrue(TEXT("read DefaultGame.ini"), FFileHelper::LoadFileToString(GameIni, *IniPath));

	TestTrue(TEXT("MapsToCook includes L_Hub"), GameIni.Contains(TEXT("+MapsToCook=(FilePath=\"/Game/Maps/L_Hub\")")));
	TestTrue(TEXT("DirectoriesToAlwaysCook includes /Game/Art"), GameIni.Contains(TEXT("+DirectoriesToAlwaysCook=(Path=\"/Game/Art\")")));
	TestTrue(TEXT("BuildConfiguration is Development"), GameIni.Contains(TEXT("BuildConfiguration=PPBC_Development")));
	TestTrue(TEXT("bUseIoStore"), GameIni.Contains(TEXT("bUseIoStore=True")));
	TestTrue(TEXT("bShareMaterialShaderCode"), GameIni.Contains(TEXT("bShareMaterialShaderCode=True")));

	TArray<FString> Files;
	GFPackagingTest::ListCsvBaseNames(Files);
	TestTrue(TEXT("Data/CSV has csv files"), Files.Num() > 0);

	const bool bDirectory = GFPackagingTest::LineStagesCsvDirectory(BuildCs) || GFPackagingTest::IniStagesCsvDirectory(GameIni);
	for (const FString& Name : Files)
	{
		const bool bCovered = bDirectory || GFPackagingTest::StagingNamesFile(BuildCs, GameIni, Name);
		TestTrue(FString::Printf(TEXT("staging list covers %s.csv"), *Name), bCovered);
	}
	return true;
}

#endif
