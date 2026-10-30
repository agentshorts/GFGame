using UnrealBuildTool;

public class GFGame : ModuleRules
{
	public GFGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GFCore", "GFVoxel", "GFBuild", "GFGarden", "GFCompanion", "GFFashion", "GFMoba", "GFRhythm", "GFSave", "GFNet", "VoxelFinish" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
