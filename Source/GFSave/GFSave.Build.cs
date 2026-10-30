using UnrealBuildTool;

public class GFSave : ModuleRules
{
	public GFSave(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "GFVoxel", "GFGarden", "GFCompanion", "GFNet", "VoxelFinish" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
