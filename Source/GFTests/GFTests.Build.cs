using UnrealBuildTool;

public class GFTests : ModuleRules
{
	public GFTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "GFVoxel", "GFBuild", "GFGarden", "GFCompanion", "GFRhythm", "GFFashion", "GFSave", "GFNet", "GFGame", "GFArena", "GFMine", "GFLab", "GFDepths", "GFGiant", "GFSocial", "VoxelFinish" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "NetCore", "AssetRegistry", "Json" });
		// GFHUD.h lives in GFGame's private UI folder. Private include paths are not exported with the module.
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "../GFGame/UI"));

	}
}
