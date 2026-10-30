using System.IO;
using UnrealBuildTool;

public class GFGame : ModuleRules
{
	public GFGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GFCore", "GFVoxel", "GFBuild", "GFGarden", "GFCompanion", "GFFashion", "GFMoba", "GFRhythm", "GFSave", "GFNet", "VoxelFinish" });
		PrivateDependencyModuleNames.AddRange(new string[] { "GFSocial", "Slate", "SlateCore" });
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Territory"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "AI/Roaming"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Dinos/Wild"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "World"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "UI"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Ferry"));

		// Loose copies of Data/CSV next to the staged project. FGFDataTables::CsvPath reads ProjectDir/Data/CSV.
		RuntimeDependencies.Add("$(ProjectDir)/Data/CSV/...", StagedFileType.NonUFS);
	}
}
