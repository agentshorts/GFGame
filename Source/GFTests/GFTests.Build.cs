using UnrealBuildTool;

public class GFTests : ModuleRules
{
	public GFTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "GFVoxel", "GFBuild", "GFGarden", "GFCompanion", "GFRhythm", "GFFashion", "GFSave", "GFNet", "GFGame", "GFArena", "GFMine", "GFLab", "GFDepths", "GFGiant", "GFSocial" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "NetCore", "AssetRegistry" });

	}
}
