using UnrealBuildTool;

public class GFTests : ModuleRules
{
	public GFTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "GFVoxel", "GFBuild", "GFGarden", "GFCompanion", "GFRhythm", "GFSave", "GFNet", "GFGame" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "NetCore" });

	}
}
